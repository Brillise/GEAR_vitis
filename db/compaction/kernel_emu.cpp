#include "db/compaction/kernel_emu.h"

namespace ROCKSDB_NAMESPACE {

void gmem_to_stream(uint32_t block_num, uint512_t *in,
                    hls::stream<uint512_t> &inKeyStream,
                    hls::stream<vint3_t> &inKeyFlag,
                    hls::stream<uint512_t> &inValueStream512) {
  uint512_t buf0[BLOCK_SIZE_512];
  uint512_t buf1[BLOCK_SIZE_512];
  bool first_flag = 1;
  uint32_t j = 0;
  uint512_t tmp;
  vint3_t tmpFlag = 0b100;

  for (int idx = 0; idx < BLOCK_SIZE_512; idx++) {
#pragma HLS PIPELINE II = 1
    buf0[idx] = in[idx];
  }

  for (int idx = 0; idx < (block_num - 1) * BLOCK_SIZE_512; idx++) {
#pragma HLS PIPELINE II = 1
    tmp = in[idx + BLOCK_SIZE_512];

    if (first_flag) {
      buf1[j] = tmp;
      if (j < BURST_KEY_SIZE_512) {
        inKeyStream << buf0[BLOCK_SIZE_512 - 1 - j];
        inKeyFlag << tmpFlag;
      } else if (j < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
        inValueStream512 << buf0[j - BURST_KEY_SIZE_512 + 1];
      }
    } else {
      buf0[j] = tmp;
      if (j < BURST_KEY_SIZE_512) {
        inKeyStream << buf1[BLOCK_SIZE_512 - 1 - j];
        inKeyFlag << tmpFlag;
      } else if (j < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
        inValueStream512 << buf1[j - BURST_KEY_SIZE_512 + 1];
      }
    }

    j++;
    if (j == BLOCK_SIZE_512) {
      j = 0;
      first_flag ^= 1;
    }
  }

  uint512_t tmp_header;
  uint32_t entry_num, key_size_512, value_size_512, block_size_512;
  uint32_t last_key_size_unit;
  if (first_flag) {
    tmp_header = buf0[0];
    entry_num = tmp_header(63, 32);
    last_key_size_unit = entry_num % Unit;
    if (last_key_size_unit == 0) last_key_size_unit = Unit;
    key_size_512 = Align(entry_num * KeySize, 64);
    value_size_512 = Align(entry_num * ValueSize, 64);
    block_size_512 = key_size_512 + value_size_512;
    inValueStream512 << entry_num;
    for (int j = 0; j < block_size_512; j++) {
#pragma HLS PIPELINE II = 1
      if (j < key_size_512) {
        inKeyStream << buf0[BLOCK_SIZE_512 - 1 - j];
        if (j == key_size_512 - 1) {
          tmpFlag = last_key_size_unit;
        }
        inKeyFlag << tmpFlag;
      } else {
        inValueStream512 << buf0[j - BURST_KEY_SIZE_512 + 1];
      }
    }
  } else {
    tmp_header = buf1[0];
    entry_num = tmp_header(63, 32);
    last_key_size_unit = entry_num % Unit;
    if (last_key_size_unit == 0) last_key_size_unit = Unit;
    key_size_512 = Align(entry_num * KeySize, 64);
    value_size_512 = Align(entry_num * ValueSize, 64);
    block_size_512 = key_size_512 + value_size_512;
    inValueStream512 << entry_num;
    for (int j = 0; j < block_size_512; j++) {
#pragma HLS PIPELINE II = 1
      if (j < key_size_512) {
        inKeyStream << buf1[BLOCK_SIZE_512 - 1 - j];
        if (j == key_size_512 - 1) {
          tmpFlag = last_key_size_unit;
        }
        inKeyFlag << tmpFlag;
      } else {
        inValueStream512 << buf1[j - BURST_KEY_SIZE_512 + 1];
      }
    }
  }
  inKeyFlag << 0;
  // inKeyStream << 0;
}

void value512_to_value(uint32_t block_num,
                       hls::stream<uint512_t> &inValueStream512,
                       hls::stream<univalue_t> &inValueStream) {
  for (int i = 0; i < block_num; i++) {
    uint32_t byteOff = 0;
    ap_uint<2 * 512> inBuffer;
    uint32_t cnt = 0;
    uint32_t cnt_512 = 1;
    uint32_t value_size_vec, value_size_512;

    if (i == block_num - 1) {
      uint32_t last_entry_num = inValueStream512.read();
      value_size_512 = Align(last_entry_num * ValueSize, 64);
      value_size_vec = Align(last_entry_num, Unit);
    }
    inBuffer(511, 0) = inValueStream512.read();

    while (1) {
#pragma HLS PIPELINE II = 1
      uint32_t newbyteOff = byteOff + Unit * ValueSize;
      if (newbyteOff >= 64 && (i < block_num - 1 || cnt_512 < value_size_512)) {
        inBuffer(1023, 512) = inValueStream512.read();
        cnt_512++;
      }
      univalue_t output =
          inBuffer(byteOff * 8 + uniValueWidth - 1, byteOff * 8);
      inValueStream << output;
      cnt++;
      if (cnt == EntryNum / Unit ||
          (i == block_num - 1 && cnt == value_size_vec)) {
        break;
      }
      byteOff = newbyteOff;
      if (newbyteOff >= 64) {
        byteOff = newbyteOff - 64;
        inBuffer >>= 512;
      }
    }
  }
  inValueStream << 0;  // fill the stream, useless
}

typedef uint64_t SequenceNumber;
static const SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);
enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };

void ParseKey(uint128_t key, SequenceNumber &sequence, ValueType &type,
              uint64_t &user_key) {
#pragma HLS INLINE
  user_key = key.range(63, 0);
  uint64_t num = key.range(127, 64);
  sequence = num >> 8;
  uint8_t c = num & 0xff;
  type = (ValueType)c;
}

int comparator(uint128_t a, uint128_t b[4], int offset, int end, bool &valid) {
#pragma HLS INLINE
  int pass = 0;
  bool local_valid = false;

  SequenceNumber a_sequence;
  ValueType a_type;
  uint64_t a_user_key;
  ParseKey(a, a_sequence, a_type, a_user_key);
  for (int j = 3; j >= 0; j--) {
#pragma HLS unroll
    SequenceNumber j_sequence;
    ValueType j_type;
    uint64_t j_user_key;
    ParseKey(b[j], j_sequence, j_type, j_user_key);
    if (a_user_key >= j_user_key && j >= offset && j < end) {
      if (a_user_key > j_user_key || a_sequence > j_sequence) {
        pass = j;
        local_valid = true;
        break;
      }
    }
  }
  valid = local_valid;
  return pass;
}

uint64_t smallest_snapshot;
void check_valid(uint512_t key, int offset, int end, bitmap_t &bitmap) {
#pragma HLS INLINE
check_loop:
  for (int j = 0; j < 4; j++) {
#pragma HLS unroll
    SequenceNumber j_sequence;
    ValueType j_type;
    uint64_t j_user_key;
    ParseKey(key(128 * j + 127, 128 * j), j_sequence, j_type, j_user_key);
    if ((j_type == kTypeValue || j_sequence > smallest_snapshot) &&
        j >= offset && j < end) {
      bitmap(j, j) = 1;
    }
  }
}

void extract_valid_key(uint512_t input, bitmap_t bitmap, int &len,
                       uint512_t &valid_input) {
#pragma HLS INLINE
  len = 0;
  for (int i = 0; i < 4; i++) {
#pragma HLS unroll
    if (bitmap(i, i)) {
      valid_input(128 * len + 127, 128 * len) = input(128 * i + 127, 128 * i);
      len++;
    }
  }
}

void extract_valid_value(univalue_t input, bitmap_t bitmap, int &len,
                         univalue_t &valid_input) {
#pragma HLS INLINE
  len = 0;
  for (int i = 0; i < 4; i++) {
#pragma HLS unroll
    if (bitmap(i, i)) {
      valid_input(ValueWidth * len + ValueWidth - 1, ValueWidth * len) =
          input(ValueWidth * i + ValueWidth - 1, ValueWidth * i);
      len++;
    }
  }
}

void key_reverse(uint128_t key1, uint128_t &key2) {
#pragma HLS INLINE
  for (int i = 0; i < KeySize / 2; i++) {
#pragma HLS unroll
    key2.range((KeySize / 2 - i - 1) * 8 + 7, (KeySize / 2 - i - 1) * 8) =
        key1.range(i * 8 + 7, i * 8);
  }
  key2.range(127, 64) = key1.range(127, 64);
}

void uint_to_string_4(uint512_t uint_data, uint128_t a[4]) {
#pragma HLS INLINE
  key_reverse(uint_data(127, 0), a[3]);
  key_reverse(uint_data(255, 128), a[2]);
  key_reverse(uint_data(383, 256), a[1]);
  key_reverse(uint_data(511, 384), a[0]);
}

void string_4_to_uint(uint128_t a[4], uint512_t &uint_data) {
#pragma HLS INLINE
  uint_data(127, 0) = a[0];
  uint_data(255, 128) = a[1];
  uint_data(383, 256) = a[2];
  uint_data(511, 384) = a[3];
}

void uint_reverse(uint512_t uint_data1, uint512_t &uint_data2) {
#pragma HLS INLINE
  for (int i = 0; i < 4; i++) {
#pragma HLS unroll
    uint128_t tmp_unit;
    key_reverse(uint_data1(i * 128 + 127, i * 128), tmp_unit);
    uint_data2.range((4 - i - 1) * 128 + 127, (4 - i - 1) * 128) = tmp_unit;
  }
}

void key_merge(hls::stream<uint512_t> &inKeyStream0,
               hls::stream<uint512_t> &inKeyStream1,
               hls::stream<vint3_t> &inKeyFlag0,
               hls::stream<vint3_t> &inKeyFlag1,
               hls::stream<uint512_t> &keyStream,
               hls::stream<bitmap_t> &bitmapStreamKey,
               hls::stream<bitmap_t> &bitmapStreamValue) {
  bool streamEmpty0 = false;  // stream 0 is empty or not
  bool streamEmpty1 = false;  // stream 1 is empty or not
  bool readValid0 = true;     // array a is empty or not
  bool readValid1 = true;     // array b is empty or not
  uint128_t a[4];
  uint128_t b[4];
#pragma HLS ARRAY_PARTITION variable = a complete
#pragma HLS ARRAY_PARTITION variable = b complete
  int a_offset = 0;
  int b_offset = 0;
  int a_len = 4;
  int b_len = 4;
  uint512_t KeyStream0;
  uint512_t KeyStream1;
  uint128_t a_cur, b_cur;

  KeyStream0 = inKeyStream0.read();
  KeyStream1 = inKeyStream1.read();
  a_len = inKeyFlag0.read();
  b_len = inKeyFlag1.read();

  uint_to_string_4(KeyStream0, a);
  a_cur = a[0];
  uint_to_string_4(KeyStream1, b);
  b_cur = b[0];
  uint512_t tmpKeyStream;

compare_loop:
  for (; !streamEmpty0 || !streamEmpty1;) {
#pragma HLS LATENCY min = 3
#pragma HLS LATENCY max = 3
#pragma HLS PIPELINE II = 1
    if (!readValid0 && !streamEmpty0) {
      a_len = inKeyFlag0.read();
      if (a_len == 0) {
        streamEmpty0 = 1;
      } else {
        KeyStream0 = inKeyStream0.read();
        streamEmpty0 = 0;
        uint_to_string_4(KeyStream0, a);
        a_cur = a[0];
        readValid0 = 1;
        a_offset = 0;
      }
    } else if (!readValid1 && !streamEmpty1) {
      b_len = inKeyFlag1.read();
      if (b_len == 0) {
        streamEmpty1 = 1;
      } else {
        KeyStream1 = inKeyStream1.read();
        streamEmpty1 = 0;
        uint_to_string_4(KeyStream1, b);
        b_cur = b[0];
        readValid1 = 1;
        b_offset = 0;
      }
    }

    bool a_valid_flag,
        b_valid_flag;  // whether we should output array a(or array b)

    int pass_b = comparator(a_cur, b, b_offset, b_len, b_valid_flag) + 1;
    int pass_a = comparator(b_cur, a, a_offset, a_len, a_valid_flag) + 1;
    int start = 0;
    int end = 4;
    int path = 0;
    if (streamEmpty1 && readValid0)  // instream1 is empty
    {
      string_4_to_uint(a, tmpKeyStream);  // temporary key in order
      start = a_offset;
      end = a_len;
      path = 0;
      readValid0 = 0;
    } else if (streamEmpty0 && readValid1)  // instream0 is empty
    {
      string_4_to_uint(b, tmpKeyStream);
      start = b_offset;
      end = b_len;
      path = 1;
      readValid1 = 0;
    } else if (a_valid_flag && readValid0 && readValid1) {
      string_4_to_uint(a, tmpKeyStream);

      start = a_offset;
      end = pass_a;
      path = 0;

      a_offset = pass_a;
      if (a_offset == a_len)
        readValid0 = 0;
      else
        a_cur = a[a_offset];
    } else if (b_valid_flag && readValid0 && readValid1) {
      string_4_to_uint(b, tmpKeyStream);

      start = b_offset;
      end = pass_b;
      path = 1;

      b_offset = pass_b;
      if (b_offset == b_len)
        readValid1 = 0;
      else
        b_cur = b[b_offset];
    }

    if (!streamEmpty0 || !streamEmpty1) {
      keyStream << tmpKeyStream;
      /*****bitmap******/
      // 0-3    vector bitmap
      // 4      path
      // 5      vector end flag
      // 6      stream end flag
      bitmap_t bitmap = 0;
      check_valid(tmpKeyStream, start, end, bitmap);
      bitmap(4, 4) = path;
      if (end == 4) bitmap(5, 5) = 1;
      bitmapStreamKey << bitmap;
      bitmapStreamValue << bitmap;
    }
  }
  bitmapStreamKey << 0x7f;
  bitmapStreamValue << 0xff;
}

void value_merge(hls::stream<univalue_t> &inValueStream0,
                 hls::stream<univalue_t> &inValueStream1,
                 hls::stream<bitmap_t> &bitmapStream,
                 hls::stream<uint512_t> &outValueStream) {
  ap_uint<2 * 512> outBuffer = 0;  // Declaring double buffers
  uint32_t byteOff = 0;
  uint32_t entry_cnt = 0;
  uint32_t value_cnt_512 = 0;
  bool block_end = 0;
  univalue_t valid_input;
  int len;

  univalue_t inValue0 = inValueStream0.read();
  univalue_t inValue1 = inValueStream1.read();
  bitmap_t bitmap = bitmapStream.read();

  for (; bitmap != 0x7f;) {
#pragma HLS PIPELINE II = 1
    if (block_end) {
      uint32_t byteleft = byteOff - entry_cnt * ValueSize;
      if (byteleft > 0) {
        outValueStream << outBuffer(511, 0);
        value_cnt_512++;
      }
      value_cnt_512 = 0;
      outBuffer >>= byteleft * 8;
      byteOff = entry_cnt * ValueSize;
      block_end = 0;
    } else {
      int path = bitmap(4, 4);
      bool vector_end = bitmap(5, 5);

      if (path == 0) {
        extract_valid_value(inValue0, bitmap, len, valid_input);
        if (vector_end) {
          inValue0 = inValueStream0.read();
        }
      } else {
        extract_valid_value(inValue1, bitmap, len, valid_input);
        if (vector_end) {
          inValue1 = inValueStream1.read();
        }
      }

      int lenWidth = len * ValueWidth;
      if (len > 0) {
        outBuffer(byteOff * 8 + lenWidth - 1, byteOff * 8) =
            valid_input(lenWidth - 1, 0);
        byteOff += len * ValueSize;
        if (byteOff >= 64 && value_cnt_512 < (BURST_VALUE_SIZE_512 - 1)) {
          byteOff -= 64;
          outValueStream << outBuffer(511, 0);
          value_cnt_512++;
          outBuffer >>= 512;
        }
        entry_cnt += len;
        if (entry_cnt >= EntryNum)  // block finished
        {
          entry_cnt -= EntryNum;
          block_end = 1;
        }
      }

      bitmap = bitmapStream.read();
    }
  }

  if (byteOff > 0) {
    outValueStream << outBuffer(511, 0);
    value_cnt_512++;
  }
  if (!inValueStream0.empty()) inValueStream0.read();
  if (!inValueStream1.empty()) inValueStream1.read();
}

void key_to_key512(hls::stream<uint512_t> &keyStream,
                   hls::stream<bitmap_t> &bitmapStream,
                   hls::stream<uint512_t> &outKeyStream,
                   hls::stream<vint3_t> &outKeyFlag) {
  bitmap_t bitmap = bitmapStream.read();
  ap_uint<2 * 512> outBuffer = 0;  // Declaring double buffers
  int unitOff = 0;

  for (; bitmap != 0x7f;) {
#pragma HLS PIPELINE II = 1
    uint512_t input = keyStream.read();
    uint512_t valid_input;
    int len;
    extract_valid_key(input, bitmap, len, valid_input);

    if (len > 0) {
      outBuffer(128 * (len + unitOff) - 1, 128 * unitOff) =
          valid_input(128 * len - 1, 0);
      unitOff += len;
      if (unitOff >= 4) {
        uint512_t tmp_out;
        uint_reverse(outBuffer(511, 0), tmp_out);
        outKeyStream << tmp_out;
        outKeyFlag << Unit;
        outBuffer >>= 512;
        unitOff -= 4;
      }
    }

    bitmap = bitmapStream.read();
  }

  if (unitOff) {
    uint512_t tmp_out;
    uint_reverse(outBuffer(511, 0), tmp_out);
    outKeyStream << tmp_out;
    outKeyFlag << unitOff;
  }
  outKeyFlag << 0;
}

void stream_to_gmem(uint32_t block_num, hls::stream<uint512_t> &outKeyStream,
                    hls::stream<vint3_t> &outKeyFlag,
                    hls::stream<uint512_t> &outValueStream, uint512_t *out) {
  uint512_t buf0[BLOCK_SIZE_512];
  uint512_t buf1[BLOCK_SIZE_512];
  bool first_flag = 1;
  uint32_t idx = 1;
  uint32_t block_id = 1;
  bool finished = 0;
  uint32_t entry_num = 0;
  uint512_t tmp_header;

  for (int i = 0; i < BLOCK_SIZE_512; i++) {
#pragma HLS PIPELINE II = 1
    if (i < BURST_KEY_SIZE_512) {
      buf0[BLOCK_SIZE_512 - 1 - i] = outKeyStream.read();
      int len = outKeyFlag.read();
    } else if (i < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
      buf0[i - BURST_KEY_SIZE_512 + 1] = outValueStream.read();
    } else {
      tmp_header(31, 0) = 0;
      tmp_header(63, 32) = EntryNum;
      tmp_header(95, 64) = KeySize * EntryNum;
      tmp_header(127, 96) = ValueSize * EntryNum;
      tmp_header(159, 128) =
          BLOCK_SIZE - ValueSize * EntryNum - KeySize * EntryNum - 64;
      buf0[0] = tmp_header;
    }
  }

  for (int i = 0; i < block_num - 1; i++) {
    entry_num = 0;
    if (first_flag) {
      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf0[j];
        if (!finished) {
          if (j < BURST_KEY_SIZE_512) {
            int len = outKeyFlag.read();
            entry_num += len;
            if (len == 0) {
              finished = 1;
            } else
              buf1[BLOCK_SIZE_512 - 1 - j] = outKeyStream.read();
          } else if (j < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
            buf1[j - BURST_KEY_SIZE_512 + 1] = outValueStream.read();
          } else {
            tmp_header(31, 0) = block_id;
            buf1[0] = tmp_header;
          }
        }
      }
    } else {
      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf1[j];
        if (!finished) {
          if (j < BURST_KEY_SIZE_512) {
            int len = outKeyFlag.read();
            entry_num += len;
            if (len == 0) {
              finished = 1;
            } else
              buf0[BLOCK_SIZE_512 - 1 - j] = outKeyStream.read();
          } else if (j < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
            buf0[j - BURST_KEY_SIZE_512 + 1] = outValueStream.read();
          } else {
            tmp_header(31, 0) = block_id;
            buf0[0] = tmp_header;
          }
        }
      }
    }
    idx += BLOCK_SIZE_512;
    block_id++;
    if (finished) {
      break;
    }
    first_flag ^= 1;
  }

  if (!finished)  // no drop entry for all sstables
  {
    int len = outKeyFlag.read();
    entry_num = EntryNum;
  }
  uint32_t key_size_512, value_size_512;
  key_size_512 = Align(entry_num * KeySize, 64);
  value_size_512 = Align(entry_num * ValueSize, 64);
  tmp_header(31, 0) = block_id;
  tmp_header(63, 32) = entry_num;
  tmp_header(95, 64) = KeySize * entry_num;
  tmp_header(127, 96) = ValueSize * entry_num;
  tmp_header(159, 128) =
      BLOCK_SIZE - ValueSize * entry_num - KeySize * entry_num - 64;
  if (first_flag) {
    if (finished) {
      buf1[0] = tmp_header;
      for (int j = 0; j < value_size_512; j++) {
#pragma HLS PIPELINE II = 1
        buf1[j + 1] = outValueStream.read();
      }

      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf1[j];
      }
    } else {
      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf0[j];
      }
    }
  } else {
    if (finished) {
      buf0[0] = tmp_header;
      for (int j = 0; j < value_size_512; j++) {
#pragma HLS PIPELINE II = 1
        buf0[j + 1] = outValueStream.read();
      }

      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf0[j];
      }
    } else {
      for (int j = 0; j < BLOCK_SIZE_512; j++) {
#pragma HLS PIPELINE II = 1
        out[idx + j] = buf1[j];
      }
    }
  }

  uint512_t out_header;
  out_header(31, 0) = block_id;
  out[0] = out_header;
}

void compaction_emu(uint512_t *in0, uint512_t *in1, uint512_t *out,
                    uint64_t block_num_info, uint64_t smallest_snapshot_c) {
#pragma HLS INTERFACE m_axi depth = 102400 port = in0 offset = slave bundle = \
    gmem0
#pragma HLS INTERFACE m_axi depth = 102400 port = in1 offset = slave bundle = \
    gmem1
#pragma HLS INTERFACE m_axi depth = 204801 port = out offset = slave bundle = \
    gmem2

#pragma HLS INTERFACE s_axilite port = in0 bundle = control
#pragma HLS INTERFACE s_axilite port = in1 bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = block_num_info bundle = control
#pragma HLS INTERFACE s_axilite port = smallest_snapshot_c bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

  hls::stream<uint512_t> inKeyStream0("inKeyStream0");
  hls::stream<uint512_t> inKeyStream1("inKeyStream1");
  hls::stream<vint3_t> inKeyFlag0("inKeyFlag0");
  hls::stream<vint3_t> inKeyFlag1("inKeyFlag1");
  hls::stream<uint512_t> inValueStream512_0("inValueStream512_0");
  hls::stream<uint512_t> inValueStream512_1("inValueStream512_1");
  hls::stream<univalue_t> inValueStream0("inValueStream0");
  hls::stream<univalue_t> inValueStream1("inValueStream1");
  hls::stream<uint512_t> keyStream("keyStream");
  hls::stream<bitmap_t> bitmapStreamKey("bitmapStreamKey");
  hls::stream<bitmap_t> bitmapStreamValue("bitmapStreamValue");
  hls::stream<uint512_t> outKeyStream("outKeyStream");
  hls::stream<vint3_t> outKeyFlag("outKeyFlag");
  hls::stream<uint512_t> outValueStream("outValueStream");

#pragma HLS STREAM variable = inKeyStream0 depth = 128
#pragma HLS STREAM variable = inKeyStream1 depth = 128
#pragma HLS STREAM variable = inKeyFlag0 depth = 128
#pragma HLS STREAM variable = inKeyFlag1 depth = 128
#pragma HLS STREAM variable = inValueStream512_0 depth = 128
#pragma HLS STREAM variable = inValueStream512_1 depth = 128
#pragma HLS STREAM variable = inValueStream0 depth = 128
#pragma HLS STREAM variable = inValueStream1 depth = 128
#pragma HLS STREAM variable = keyStream depth = 128
#pragma HLS STREAM variable = bitmapStreamKey depth = 128
#pragma HLS STREAM variable = bitmapStreamValue depth = 128
#pragma HLS STREAM variable = outKeyStream depth = 128
#pragma HLS STREAM variable = outKeyFlag depth = 128
#pragma HLS STREAM variable = outValueStream depth = 128

#pragma HLS DATAFLOW
  smallest_snapshot = smallest_snapshot_c;
  uint32_t block_num0 = block_num_info & 0xffffffff;
  uint32_t block_num1 = block_num_info >> 32;
  gmem_to_stream(block_num0, in0, inKeyStream0, inKeyFlag0, inValueStream512_0);
  gmem_to_stream(block_num1, in1, inKeyStream1, inKeyFlag1, inValueStream512_1);
  value512_to_value(block_num0, inValueStream512_0, inValueStream0);
  value512_to_value(block_num1, inValueStream512_1, inValueStream1);
  key_merge(inKeyStream0, inKeyStream1, inKeyFlag0, inKeyFlag1, keyStream,
            bitmapStreamKey, bitmapStreamValue);
  key_to_key512(keyStream, bitmapStreamKey, outKeyStream, outKeyFlag);
  value_merge(inValueStream0, inValueStream1, bitmapStreamValue,
              outValueStream);
  stream_to_gmem(block_num0 + block_num1, outKeyStream, outKeyFlag,
                 outValueStream, out);
}

}  // namespace ROCKSDB_NAMESPACE