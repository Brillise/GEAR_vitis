#include "db/compaction/kernel_emu.h"

namespace ROCKSDB_NAMESPACE {

uint64_t smallest_snapshot;

typedef uint64_t SequenceNumber;
static const SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);
enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };

void ParseKey(my_key_t key, SequenceNumber &sequence, ValueType &type,
              uint64_t &user_key) {
#pragma HLS INLINE
  user_key = key.range(63, 0);
  uint64_t num = key.range(127, 64);
  sequence = num >> 8;
  uint8_t c = num & 0xff;
  type = (ValueType)c;
}

bool comparator(my_key_t a, my_key_t b) {
#pragma HLS INLINE
  bool pass = 0;

  SequenceNumber a_sequence, b_sequence;
  ValueType a_type, b_type;
  uint64_t a_user_key, b_user_key;
  ParseKey(a, a_sequence, a_type, a_user_key);
  ParseKey(b, b_sequence, b_type, b_user_key);
  if (a_user_key < b_user_key) {
    pass = 0;
  } else if (a_user_key > b_user_key) {
    pass = 1;
  } else {
    if (a_sequence >= b_sequence) {
      pass = 0;
    } else {
      pass = 1;
    }
  }
  return pass;
}

vint2_t key_compare_unit(bool transValid0, bool transValid1, my_key_t Key0,
                         my_key_t Key1, bool &transValid_r, my_key_t &key_r) {
#pragma HLS INLINE
  vint2_t cur;
  transValid_r = 1;

  if (transValid0 && !transValid1)  // instream1 is empty
  {
    cur = 1;
    key_r = Key0;
  } else if (!transValid0 && transValid1)  // instream0 is empty
  {
    cur = 2;
    key_r = Key1;
  } else if (transValid0 && transValid1) {
    bool pass = comparator(Key0, Key1);

    if (pass == 0) {
      cur = 1;
      key_r = Key0;
    } else {
      cur = 2;
      key_r = Key1;
    }
  } else  // invalid
  {
    cur = 0;
    transValid_r = 0;
  }

  return cur;
}

void key_merge_drop(hls::stream<my_key_t> &inKeyStream0,
                    hls::stream<my_key_t> &inKeyStream1,
                    hls::stream<my_key_t> &keyStream,
                    hls::stream<vint3_t> &pathDropFlag) {
  uint64_t last_sequence_for_key = kMaxSequenceNumber;
  uint64_t cur_user_key = ULLONG_MAX;
  my_key_t Key0, Key1, tmpKey;
  bool transValid0, transValid1;

compare_loop:
  for (ap_uint<2> cur = 0x3; cur > 0;) {
#pragma HLS PIPELINE II = 1
    bool path = 0;

    if (cur.range(0, 0)) {
      transValid0 = 0;
      Key0 = inKeyStream0.read();
      if (Key0 != 0) {
        transValid0 = 1;
      }
    }
    if (cur.range(1, 1)) {
      transValid1 = 0;
      Key1 = inKeyStream1.read();
      if (Key1 != 0) {
        transValid1 = 1;
      }
    }

    if (transValid0 && !transValid1)  // instream1 is empty
    {
      cur = 1;
    } else if (!transValid0 && transValid1)  // instream0 is empty
    {
      cur = 2;
    } else if (transValid0 && transValid1) {
      bool pass = comparator(Key0, Key1);

      if (pass == 0) {
        cur = 1;
      } else {
        cur = 2;
      }
    } else  // invalid
    {
      cur = 0;
    }

    if (cur == 1) {
      path = 0;
      tmpKey = Key0;
    } else if (cur == 2) {
      path = 1;
      tmpKey = Key1;
    }

    if (cur != 0) {
      bool drop = false;
      uint64_t tmp_user_key;
      SequenceNumber tmp_sequence;
      ValueType tmp_type;

      ParseKey(tmpKey, tmp_sequence, tmp_type, tmp_user_key);
      if (tmp_user_key != cur_user_key) {
        cur_user_key = tmp_user_key;
        last_sequence_for_key = kMaxSequenceNumber;
      }

      if ((last_sequence_for_key <=
           smallest_snapshot) ||  // Hidden by an newer entry for same user key
          (tmp_type == kTypeDeletion && tmp_sequence <= smallest_snapshot)) {
        drop = true;
      }
      last_sequence_for_key = tmp_sequence;

      /*****bitmap******/
      // 0      path bitmap
      // 1      drop flag
      // 2      stream end flag
      vint3_t bitmap = 0;
      bitmap(0, 0) = path;
      bitmap(1, 1) = drop;
      pathDropFlag << bitmap;
      if (!drop) keyStream << tmpKey;
    }
  }
  pathDropFlag << 0x7;
  keyStream << 0;
}

void key_merge(hls::stream<my_key_t> &inKeyStream0,
               hls::stream<my_key_t> &inKeyStream1,
               hls::stream<my_key_t> &keyStream,
               hls::stream<vint2_t> &pathFlag) {
  my_key_t Key0, Key1;
  bool transValid0, transValid1;

compare_loop:
  for (ap_uint<2> cur = 0x3; cur > 0;) {
#pragma HLS PIPELINE II = 1
    bool path = 0;

    if (cur.range(0, 0)) {
      transValid0 = 0;
      Key0 = inKeyStream0.read();
      if (Key0 != 0) {
        transValid0 = 1;
      }
    }
    if (cur.range(1, 1)) {
      transValid1 = 0;
      Key1 = inKeyStream1.read();
      if (Key1 != 0) {
        transValid1 = 1;
      }
    }

    if (transValid0 && !transValid1)  // instream1 is empty
    {
      cur = 1;
    } else if (!transValid0 && transValid1)  // instream0 is empty
    {
      cur = 2;
    } else if (transValid0 && transValid1) {
      bool pass = comparator(Key0, Key1);

      if (pass == 0) {
        cur = 1;
      } else {
        cur = 2;
      }
    } else  // invalid
    {
      cur = 0;
    }

    if (cur == 1) {
      path = 0;
      pathFlag << 0x0;
      keyStream << Key0;
    } else if (cur == 2) {
      path = 1;
      pathFlag << 0x1;
      keyStream << Key1;
    }
  }
  pathFlag << 0x3;
  keyStream << 0;
}

void gmem_to_stream(uint32_t block_num, uint512_t *in,
                    hls::stream<uint512_t> &inKeyStream512,
                    hls::stream<uint512_t> &inValueStream512) {
  uint512_t buf0[BLOCK_SIZE_512];
  uint512_t buf1[BLOCK_SIZE_512];
  bool first_flag = 1;
  uint32_t j = 0;
  uint512_t tmp;

  if (block_num) {
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
          inKeyStream512 << buf0[BLOCK_SIZE_512 - 1 - j];
        } else if (j < (BURST_KEY_SIZE_512 + BURST_VALUE_SIZE_512)) {
          inValueStream512 << buf0[j - BURST_KEY_SIZE_512 + 1];
        }
      } else {
        buf0[j] = tmp;
        if (j < BURST_KEY_SIZE_512) {
          inKeyStream512 << buf1[BLOCK_SIZE_512 - 1 - j];
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
      inKeyStream512 << entry_num;
      inValueStream512 << entry_num;
      for (int j = 0; j < block_size_512; j++) {
#pragma HLS PIPELINE II = 1
        if (j < key_size_512) {
          inKeyStream512 << buf0[BLOCK_SIZE_512 - 1 - j];
        } else {
          inValueStream512 << buf0[j - key_size_512 + 1];
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
      inKeyStream512 << entry_num;
      inValueStream512 << entry_num;
      for (int j = 0; j < block_size_512; j++) {
#pragma HLS PIPELINE II = 1
        if (j < key_size_512) {
          inKeyStream512 << buf1[BLOCK_SIZE_512 - 1 - j];
        } else {
          inValueStream512 << buf1[j - key_size_512 + 1];
        }
      }
    }
  }
}

void key_reverse(my_key_t key1, my_key_t &key2) {
#pragma HLS INLINE
  for (int i = 0; i < KeySize / 2; i++) {
#pragma HLS unroll
    key2.range((KeySize / 2 - i - 1) * 8 + 7, (KeySize / 2 - i - 1) * 8) =
        key1.range(i * 8 + 7, i * 8);
  }
  key2.range(127, 64) = key1.range(127, 64);
}

void uint_to_string_4(uint512_t uint_data, my_key_t a[4]) {
#pragma HLS INLINE
  key_reverse(uint_data(127, 0), a[3]);
  key_reverse(uint_data(255, 128), a[2]);
  key_reverse(uint_data(383, 256), a[1]);
  key_reverse(uint_data(511, 384), a[0]);
}

void string_4_to_uint(my_key_t a[4], uint512_t &uint_data) {
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
    my_key_t tmp_unit;
    key_reverse(uint_data1(i * 128 + 127, i * 128), tmp_unit);
    uint_data2.range((4 - i - 1) * 128 + 127, (4 - i - 1) * 128) = tmp_unit;
  }
}

void key512_to_key(uint32_t block_num, hls::stream<uint512_t> &inKeyStream512,
                   hls::stream<my_key_t> &inKeyStream) {
  for (int i = 0; i < block_num; i++) {
    uint32_t cnt = 0;
    int len = 0;
    uint512_t tmp_key, inBuffer;
    uint32_t key_size_512, last_entry_num;

    if (i == block_num - 1) {
      last_entry_num = inKeyStream512.read();
      key_size_512 = Align(last_entry_num * KeySize, 64);
    }

    tmp_key = inKeyStream512.read();
    uint_reverse(tmp_key, inBuffer);
    while (1) {
#pragma HLS PIPELINE II = 1
      my_key_t output = inBuffer(128 * len + 127, 128 * len);
      inKeyStream << output;
      cnt++;
      if (cnt == EntryNum || (i == block_num - 1 && cnt == last_entry_num)) {
        break;
      }
      len++;
      if (len == Unit) {
        len = 0;
        tmp_key = inKeyStream512.read();
        uint_reverse(tmp_key, inBuffer);
      }
    }
  }
  inKeyStream << 0;
}

void key_to_key512(hls::stream<my_key_t> &keyStream,
                   hls::stream<uint512_t> &outKeyStream,
                   hls::stream<vint3_t> &outKeyFlag) {
  uint512_t outBuffer = 0;
  int len = 0;

  my_key_t tmp_key = keyStream.read();
  for (; tmp_key != 0;) {
#pragma HLS PIPELINE II = 1
    outBuffer(128 * len + 127, 128 * len) = tmp_key;
    len++;
    if (len == Unit) {
      len = 0;
      uint512_t tmp_out;
      uint_reverse(outBuffer, tmp_out);
      outKeyStream << tmp_out;
      outKeyFlag << Unit;
    }

    tmp_key = keyStream.read();
  }

  if (len) {
    uint512_t tmp_out;
    uint_reverse(outBuffer, tmp_out);
    outKeyStream << tmp_out;
    outKeyFlag << len;
  }
  outKeyFlag << 0;
}

void value512_to_value(uint32_t block_num,
                       hls::stream<uint512_t> &inValueStream512,
                       hls::stream<value_t> &inValueStream) {
  for (int i = 0; i < block_num; i++) {
    uint32_t byteOff = 0;
    uint32_t cnt = 0;
    uint32_t cnt_512 = 1;
    ap_uint<2 * 512> inBuffer;
    uint32_t value_size_512, last_entry_num;

    if (i == block_num - 1) {
      last_entry_num = inValueStream512.read();
      value_size_512 = Align(last_entry_num * ValueSize, 64);
    }
    inBuffer(511, 0) = inValueStream512.read();

    while (1) {
#pragma HLS PIPELINE II = 1
      if ((byteOff + ValueSize >= 64) &&
          (i < block_num - 1 || cnt_512 < value_size_512)) {
        inBuffer(1023, 512) = inValueStream512.read();
        cnt_512++;
      }
      value_t output = inBuffer(byteOff * 8 + ValueWidth - 1, byteOff * 8);
      inValueStream << output;
      cnt++;
      if (cnt == EntryNum || (i == block_num - 1 && cnt == last_entry_num)) {
        break;
      }
      byteOff += ValueSize;
      if (byteOff >= 64) {
        byteOff -= 64;
        inBuffer >>= 512;
      }
    }
  }
}

void value_merge_drop(hls::stream<value_t> &inValueStream0,
                      hls::stream<value_t> &inValueStream1,
                      hls::stream<vint3_t> &pathDropFlag,
                      hls::stream<value_t> &valueStream) {
  value_t inValue;

  vint3_t bitmap = pathDropFlag.read();
  for (; bitmap != 0x7;) {
#pragma HLS PIPELINE II = 1
    bool path = bitmap(0, 0);
    bool drop = bitmap(1, 1);

    switch (path) {
      case 0:
        inValue = inValueStream0.read();
        break;
      case 1:
        inValue = inValueStream1.read();
        break;
    }

    if (!drop) {
      valueStream << inValue;
    }

    bitmap = pathDropFlag.read();
  }
}

void value_merge(hls::stream<value_t> &inValueStream0,
                 hls::stream<value_t> &inValueStream1,
                 hls::stream<vint2_t> &pathFlag,
                 hls::stream<value_t> &valueStream) {
  value_t inValue;

  vint2_t bitmap = pathFlag.read();
  for (; bitmap != 0x3;) {
#pragma HLS PIPELINE II = 1
    bool path = bitmap(0, 0);

    switch (path) {
      case 0:
        inValue = inValueStream0.read();
        break;
      case 1:
        inValue = inValueStream1.read();
        break;
    }

    valueStream << inValue;

    bitmap = pathFlag.read();
  }
}

void value_merge_last(hls::stream<value_t> &inValueStream0,
                      hls::stream<value_t> &inValueStream1,
                      hls::stream<vint2_t> &pathFlag,
                      hls::stream<value_t> &valueStream,
                      hls::stream<bool> &valueFlag) {
  value_t inValue;

  vint2_t bitmap = pathFlag.read();
  for (; bitmap != 0x3;) {
#pragma HLS PIPELINE II = 1
    bool path = bitmap(0, 0);

    switch (path) {
      case 0:
        inValue = inValueStream0.read();
        break;
      case 1:
        inValue = inValueStream1.read();
        break;
    }

    valueStream << inValue;
    valueFlag << 0;

    bitmap = pathFlag.read();
  }
  valueFlag << 1;
}

void value_to_value512(hls::stream<value_t> &valueStream,
                       hls::stream<bool> &valueFlag,
                       hls::stream<uint512_t> &outValueStream) {
  ap_uint<2 * 512> outBuffer = 0;
  uint32_t entry_cnt = 0;
  uint32_t byteOff = 0;
  bool block_end = 0;

  bool value_end = valueFlag.read();
  while (!value_end) {
#pragma HLS PIPELINE II = 1
    if (block_end) {
      if (byteOff) {
        outValueStream << outBuffer(511, 0);
      }
      byteOff = 0;
      block_end = 0;
    } else {
      value_t tmp_value = valueStream.read();
      outBuffer(byteOff * 8 + ValueWidth - 1, byteOff * 8) = tmp_value;
      byteOff += ValueSize;
      if (byteOff >= 64) {
        byteOff -= 64;
        outValueStream << outBuffer(511, 0);
        outBuffer >>= 512;
      }

      entry_cnt++;
      if (entry_cnt == EntryNum) {
        entry_cnt = 0;
        block_end = 1;
      }
      value_end = valueFlag.read();
    }
  }

  if (byteOff) {
    outValueStream << outBuffer(511, 0);
  }
}

void stream_to_gmem(uint32_t block_num, hls::stream<uint512_t> &outKeyStream,
                    hls::stream<vint3_t> &outKeyFlag,
                    hls::stream<uint512_t> &outValueStream, uint512_t *out) {
  uint512_t buf0[BLOCK_SIZE_512];
  uint512_t buf1[BLOCK_SIZE_512];
  bool first_flag = 1;
  uint32_t idx = 0;
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

  out[0](511, 480) = block_id;
}

void compaction(uint512_t *in0, uint512_t *in1, uint512_t *in2, uint512_t *in3,
                uint512_t *in4, uint512_t *in5, uint512_t *in6, uint512_t *in7,
                uint512_t *out, uint32_t block_num0, uint32_t block_num1,
                uint32_t block_num2, uint32_t block_num3, uint32_t block_num4,
                uint32_t block_num5, uint32_t block_num6, uint32_t block_num7,
                uint64_t smallest_snapshot_c) {
#pragma HLS INTERFACE m_axi depth = 12800 port = in0 offset = slave bundle = \
    gmem0
#pragma HLS INTERFACE m_axi depth = 12800 port = in1 offset = slave bundle = \
    gmem1
#pragma HLS INTERFACE m_axi depth = 12800 port = in2 offset = slave bundle = \
    gmem2
#pragma HLS INTERFACE m_axi depth = 12800 port = in3 offset = slave bundle = \
    gmem3
#pragma HLS INTERFACE m_axi depth = 12800 port = in4 offset = slave bundle = \
    gmem4
#pragma HLS INTERFACE m_axi depth = 12800 port = in5 offset = slave bundle = \
    gmem5
#pragma HLS INTERFACE m_axi depth = 12800 port = in6 offset = slave bundle = \
    gmem6
#pragma HLS INTERFACE m_axi depth = 12800 port = in7 offset = slave bundle = \
    gmem7
#pragma HLS INTERFACE m_axi depth = 102400 port = out offset = slave bundle = \
    gmem8

#pragma HLS INTERFACE s_axilite port = in0 bundle = control
#pragma HLS INTERFACE s_axilite port = in1 bundle = control
#pragma HLS INTERFACE s_axilite port = in2 bundle = control
#pragma HLS INTERFACE s_axilite port = in3 bundle = control
#pragma HLS INTERFACE s_axilite port = in4 bundle = control
#pragma HLS INTERFACE s_axilite port = in5 bundle = control
#pragma HLS INTERFACE s_axilite port = in6 bundle = control
#pragma HLS INTERFACE s_axilite port = in7 bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = block_num0 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num1 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num2 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num3 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num4 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num5 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num6 bundle = control
#pragma HLS INTERFACE s_axilite port = block_num7 bundle = control
#pragma HLS INTERFACE s_axilite port = smallest_snapshot_c bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

  hls::stream<uint512_t> inKeyStream512_0("inKeyStream512_0");
  hls::stream<uint512_t> inKeyStream512_1("inKeyStream512_1");
  hls::stream<uint512_t> inKeyStream512_2("inKeyStream512_2");
  hls::stream<uint512_t> inKeyStream512_3("inKeyStream512_3");
  hls::stream<uint512_t> inKeyStream512_4("inKeyStream512_4");
  hls::stream<uint512_t> inKeyStream512_5("inKeyStream512_5");
  hls::stream<uint512_t> inKeyStream512_6("inKeyStream512_6");
  hls::stream<uint512_t> inKeyStream512_7("inKeyStream512_7");
  hls::stream<uint512_t> inValueStream512_0("inValueStream512_0");
  hls::stream<uint512_t> inValueStream512_1("inValueStream512_1");
  hls::stream<uint512_t> inValueStream512_2("inValueStream512_2");
  hls::stream<uint512_t> inValueStream512_3("inValueStream512_3");
  hls::stream<uint512_t> inValueStream512_4("inValueStream512_4");
  hls::stream<uint512_t> inValueStream512_5("inValueStream512_5");
  hls::stream<uint512_t> inValueStream512_6("inValueStream512_6");
  hls::stream<uint512_t> inValueStream512_7("inValueStream512_7");
  hls::stream<my_key_t> inKeyStream0("inKeyStream0");
  hls::stream<my_key_t> inKeyStream1("inKeyStream1");
  hls::stream<my_key_t> inKeyStream2("inKeyStream2");
  hls::stream<my_key_t> inKeyStream3("inKeyStream3");
  hls::stream<my_key_t> inKeyStream4("inKeyStream4");
  hls::stream<my_key_t> inKeyStream5("inKeyStream5");
  hls::stream<my_key_t> inKeyStream6("inKeyStream6");
  hls::stream<my_key_t> inKeyStream7("inKeyStream7");
  hls::stream<value_t> inValueStream0("inValueStream0");
  hls::stream<value_t> inValueStream1("inValueStream1");
  hls::stream<value_t> inValueStream2("inValueStream2");
  hls::stream<value_t> inValueStream3("inValueStream3");
  hls::stream<value_t> inValueStream4("inValueStream4");
  hls::stream<value_t> inValueStream5("inValueStream5");
  hls::stream<value_t> inValueStream6("inValueStream6");
  hls::stream<value_t> inValueStream7("inValueStream7");
  hls::stream<my_key_t> keyStream_00("keyStream_00");
  hls::stream<my_key_t> keyStream_01("keyStream_01");
  hls::stream<my_key_t> keyStream_02("keyStream_02");
  hls::stream<my_key_t> keyStream_03("keyStream_03");
  hls::stream<my_key_t> keyStream_10("keyStream_10");
  hls::stream<my_key_t> keyStream_11("keyStream_11");
  hls::stream<my_key_t> keyStream("keyStream");
  hls::stream<vint3_t> pathDropFlag0("pathDropFlag0");
  hls::stream<vint3_t> pathDropFlag1("pathDropFlag1");
  hls::stream<vint3_t> pathDropFlag2("pathDropFlag2");
  hls::stream<vint3_t> pathDropFlag3("pathDropFlag3");
  hls::stream<vint2_t> pathFlag_00("pathFlag_00");
  hls::stream<vint2_t> pathFlag_01("pathFlag_01");
  hls::stream<vint2_t> pathFlag("pathFlag");
  hls::stream<value_t> valueStream_00("valueStream_00");
  hls::stream<value_t> valueStream_01("valueStream_01");
  hls::stream<value_t> valueStream_02("valueStream_02");
  hls::stream<value_t> valueStream_03("valueStream_03");
  hls::stream<value_t> valueStream_10("valueStream_10");
  hls::stream<value_t> valueStream_11("valueStream_11");
  hls::stream<value_t> valueStream("valueStream");
  hls::stream<bool> valueFlag("valueFlag");
  hls::stream<uint512_t> outKeyStream("outKeyStream");
  hls::stream<vint3_t> outKeyFlag("outKeyFlag");
  hls::stream<uint512_t> outValueStream("outValueStream");

#pragma HLS STREAM variable = inKeyStream512_0 depth = 128
#pragma HLS STREAM variable = inKeyStream512_1 depth = 128
#pragma HLS STREAM variable = inKeyStream512_2 depth = 128
#pragma HLS STREAM variable = inKeyStream512_3 depth = 128
#pragma HLS STREAM variable = inKeyStream512_4 depth = 128
#pragma HLS STREAM variable = inKeyStream512_5 depth = 128
#pragma HLS STREAM variable = inKeyStream512_6 depth = 128
#pragma HLS STREAM variable = inKeyStream512_7 depth = 128
#pragma HLS STREAM variable = inValueStream512_0 depth = 128
#pragma HLS STREAM variable = inValueStream512_1 depth = 128
#pragma HLS STREAM variable = inValueStream512_2 depth = 128
#pragma HLS STREAM variable = inValueStream512_3 depth = 128
#pragma HLS STREAM variable = inValueStream512_4 depth = 128
#pragma HLS STREAM variable = inValueStream512_5 depth = 128
#pragma HLS STREAM variable = inValueStream512_6 depth = 128
#pragma HLS STREAM variable = inValueStream512_7 depth = 128
#pragma HLS STREAM variable = inKeyStream0 depth = 128
#pragma HLS STREAM variable = inKeyStream1 depth = 128
#pragma HLS STREAM variable = inKeyStream2 depth = 128
#pragma HLS STREAM variable = inKeyStream3 depth = 128
#pragma HLS STREAM variable = inKeyStream4 depth = 128
#pragma HLS STREAM variable = inKeyStream5 depth = 128
#pragma HLS STREAM variable = inKeyStream6 depth = 128
#pragma HLS STREAM variable = inKeyStream7 depth = 128
#pragma HLS STREAM variable = inValueStream0 depth = 128
#pragma HLS STREAM variable = inValueStream1 depth = 128
#pragma HLS STREAM variable = inValueStream2 depth = 128
#pragma HLS STREAM variable = inValueStream3 depth = 128
#pragma HLS STREAM variable = inValueStream4 depth = 128
#pragma HLS STREAM variable = inValueStream5 depth = 128
#pragma HLS STREAM variable = inValueStream6 depth = 128
#pragma HLS STREAM variable = inValueStream7 depth = 128
#pragma HLS STREAM variable = keyStream_00 depth = 128
#pragma HLS STREAM variable = keyStream_01 depth = 128
#pragma HLS STREAM variable = keyStream_02 depth = 128
#pragma HLS STREAM variable = keyStream_03 depth = 128
#pragma HLS STREAM variable = keyStream_10 depth = 128
#pragma HLS STREAM variable = keyStream_11 depth = 128
#pragma HLS STREAM variable = keyStream depth = 128
#pragma HLS STREAM variable = pathDropFlag0 depth = 128
#pragma HLS STREAM variable = pathDropFlag1 depth = 128
#pragma HLS STREAM variable = pathDropFlag2 depth = 128
#pragma HLS STREAM variable = pathDropFlag3 depth = 128
#pragma HLS STREAM variable = pathFlag_00 depth = 128
#pragma HLS STREAM variable = pathFlag_01 depth = 128
#pragma HLS STREAM variable = pathFlag depth = 128
#pragma HLS STREAM variable = valueStream_00 depth = 128
#pragma HLS STREAM variable = valueStream_01 depth = 128
#pragma HLS STREAM variable = valueStream_02 depth = 128
#pragma HLS STREAM variable = valueStream_03 depth = 128
#pragma HLS STREAM variable = valueStream_10 depth = 128
#pragma HLS STREAM variable = valueStream_11 depth = 128
#pragma HLS STREAM variable = valueStream depth = 128
#pragma HLS STREAM variable = valueFlag depth = 128
#pragma HLS STREAM variable = outKeyStream depth = 128
#pragma HLS STREAM variable = outKeyFlag depth = 128
#pragma HLS STREAM variable = outValueStream depth = 128

#pragma HLS DATAFLOW
  smallest_snapshot = smallest_snapshot_c;
  uint32_t block_num_sum = block_num0 + block_num1 + block_num2 + block_num3 +
                           block_num4 + block_num5 + block_num6 + block_num7;

  gmem_to_stream(block_num0, in0, inKeyStream512_0, inValueStream512_0);
  gmem_to_stream(block_num1, in1, inKeyStream512_1, inValueStream512_1);
  gmem_to_stream(block_num2, in2, inKeyStream512_2, inValueStream512_2);
  gmem_to_stream(block_num3, in3, inKeyStream512_3, inValueStream512_3);
  gmem_to_stream(block_num4, in4, inKeyStream512_4, inValueStream512_4);
  gmem_to_stream(block_num5, in5, inKeyStream512_5, inValueStream512_5);
  gmem_to_stream(block_num6, in6, inKeyStream512_6, inValueStream512_6);
  gmem_to_stream(block_num7, in7, inKeyStream512_7, inValueStream512_7);
  key512_to_key(block_num0, inKeyStream512_0, inKeyStream0);
  key512_to_key(block_num1, inKeyStream512_1, inKeyStream1);
  key512_to_key(block_num2, inKeyStream512_2, inKeyStream2);
  key512_to_key(block_num3, inKeyStream512_3, inKeyStream3);
  key512_to_key(block_num4, inKeyStream512_4, inKeyStream4);
  key512_to_key(block_num5, inKeyStream512_5, inKeyStream5);
  key512_to_key(block_num6, inKeyStream512_6, inKeyStream6);
  key512_to_key(block_num7, inKeyStream512_7, inKeyStream7);
  key_merge_drop(inKeyStream0, inKeyStream1, keyStream_00, pathDropFlag0);
  key_merge_drop(inKeyStream2, inKeyStream3, keyStream_01, pathDropFlag1);
  key_merge_drop(inKeyStream4, inKeyStream5, keyStream_02, pathDropFlag2);
  key_merge_drop(inKeyStream6, inKeyStream7, keyStream_03, pathDropFlag3);
  key_merge(keyStream_00, keyStream_01, keyStream_10, pathFlag_00);
  key_merge(keyStream_02, keyStream_03, keyStream_11, pathFlag_01);
  key_merge(keyStream_10, keyStream_11, keyStream, pathFlag);
  key_to_key512(keyStream, outKeyStream, outKeyFlag);
  value512_to_value(block_num0, inValueStream512_0, inValueStream0);
  value512_to_value(block_num1, inValueStream512_1, inValueStream1);
  value512_to_value(block_num2, inValueStream512_2, inValueStream2);
  value512_to_value(block_num3, inValueStream512_3, inValueStream3);
  value512_to_value(block_num4, inValueStream512_4, inValueStream4);
  value512_to_value(block_num5, inValueStream512_5, inValueStream5);
  value512_to_value(block_num6, inValueStream512_6, inValueStream6);
  value512_to_value(block_num7, inValueStream512_7, inValueStream7);
  value_merge_drop(inValueStream0, inValueStream1, pathDropFlag0,
                   valueStream_00);
  value_merge_drop(inValueStream2, inValueStream3, pathDropFlag1,
                   valueStream_01);
  value_merge_drop(inValueStream4, inValueStream5, pathDropFlag2,
                   valueStream_02);
  value_merge_drop(inValueStream6, inValueStream7, pathDropFlag3,
                   valueStream_03);
  value_merge(valueStream_00, valueStream_01, pathFlag_00, valueStream_10);
  value_merge(valueStream_02, valueStream_03, pathFlag_01, valueStream_11);
  value_merge_last(valueStream_10, valueStream_11, pathFlag, valueStream,
                   valueFlag);
  value_to_value512(valueStream, valueFlag, outValueStream);
  stream_to_gmem(block_num_sum, outKeyStream, outKeyFlag, outValueStream, out);
}

}  // namespace ROCKSDB_NAMESPACE