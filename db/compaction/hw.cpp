#include "db/compaction/hw.h"

#include "rocksdb/env.h"

namespace ROCKSDB_NAMESPACE {

HW::HW() {
#ifdef HARDWARE
  std::string binaryPath = "/home/centos/Documents/vitis_compaction_path8/";
  std::string binaryFileName =
      binaryPath + "compaction/Hardware/compaction_path8.awsxclbin";
#ifndef EMU
  init_device(context, q, krnl_compact, binaryFileName, err);
  kernel_events.resize(2);
  read_events.resize(2);
  input_buf_ptr0.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr1.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr2.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr3.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr4.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr5.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr6.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr7.resize(8 * (uint64_t)SST_SIZE / 4);
  input_buf_ptr0.resize(8 * (uint64_t)SST_SIZE / 4);
  output_buf_ptr.resize(64 * (uint64_t)SST_SIZE / 4);
  output_buf_ptr_last.resize(64 * (uint64_t)SST_SIZE / 4);
#endif

#endif
  printf("--- HW engine successfully initialized -------------------------\n");
}

HW::~HW() { printf("--- HW shutdown-------------------------\n"); }

#ifdef EMU
void HW::compaction_emu(int input_num, std::string *input_file_name,
                        uint64_t smallest_snapshot, int idx) {
  printf("\n");
  if (input_num > 1) {
    uint512_t *input_buf_ptr[NumInput] = {NULL};
    uint32_t input_block_num[NumInput] = {0};
    uint64_t cl_input_size[NumInput] = {0};
    uint64_t cl_output_size = 0;

    for (int i = 0; i < NumInput; i++) {
      if (i < input_num) {
        FILE *pFile = NULL;
        pFile = fopen(input_file_name[i].c_str(), "rb");
        if (pFile == NULL) {
          printf("open input file %d failed\n", i);
        }
        fseek(pFile, 0, SEEK_END);
        uint32_t file_size = ftell(pFile);
        input_block_num[i] = file_size / BLOCK_SIZE;
        printf("input %d block num: %u\n", i, input_block_num[i]);
        fseek(pFile, 0, SEEK_SET);

        cl_input_size[i] = input_block_num[i] * BLOCK_SIZE;
        cl_output_size += cl_input_size[i];
        input_buf_ptr[i] = (uint512_t *)malloc(cl_input_size[i]);
        if (input_buf_ptr[i] == NULL) {
          printf("malloc file %d input buffer failed\n", i);
        }
        fread(input_buf_ptr[i], 1, cl_input_size[i], pFile);
        fclose(pFile);
        pFile = NULL;
      } else {
        input_block_num[i] = 0;
        cl_input_size[i] = 4096;
        input_buf_ptr[i] = (uint512_t *)malloc(cl_input_size[i]);
        if (input_buf_ptr[i] == NULL) {
          printf("malloc file %d input buffer failed\n", i);
        }
      }
    }

    output_buf_ptr[idx] = (uint512_t *)malloc(cl_output_size);
    if (output_buf_ptr[idx] == NULL) {
      printf("malloc file output buffer failed\n");
    }

    compaction(input_buf_ptr[0], input_buf_ptr[1], input_buf_ptr[2],
               input_buf_ptr[3], input_buf_ptr[4], input_buf_ptr[5],
               input_buf_ptr[6], input_buf_ptr[7], output_buf_ptr[idx],
               input_block_num[0], input_block_num[1], input_block_num[2],
               input_block_num[3], input_block_num[4], input_block_num[5],
               input_block_num[6], input_block_num[7], smallest_snapshot);

    uint512_t output_header = output_buf_ptr[idx][0];
    uint32_t output_block_num = output_header(511, 480);
    printf("\noutput block num: %u\n", output_block_num);
    tmp_output_size[idx] = (uint64_t)BLOCK_SIZE * output_block_num;
  } else {  // for single file
    FILE *pFile = fopen(input_file_name[0].c_str(), "rb");
    if (pFile == NULL) {
      printf("open input file %d failed\n", idx * NumInput);
    }
    fseek(pFile, 0, SEEK_END);
    uint32_t file_size = ftell(pFile);
    uint32_t input_block_num = file_size / BLOCK_SIZE;
    printf("input %d block num: %u\n", idx * NumInput, input_block_num);
    fseek(pFile, 0, SEEK_SET);
    uint64_t cl_input_size = input_block_num * BLOCK_SIZE;

    output_buf_ptr[idx] = (uint512_t *)malloc(cl_input_size);
    if (output_buf_ptr[idx] == NULL) {
      printf("malloc file %d input buffer failed\n", idx * NumInput);
    }
    fread(output_buf_ptr[idx], 1, cl_input_size, pFile);
    tmp_output_size[idx] = cl_input_size;

    fclose(pFile);
    pFile = NULL;
  }
}

void HW::compaction_last_emu(int input_num, uint64_t smallest_snapshot) {
  uint32_t input_block_num[NumInput] = {0};
  uint64_t cl_input_size[NumInput] = {0};
  uint64_t cl_output_size = 0;

  printf("\n");
  for (int i = 0; i < NumInput; i++) {
    if (i < input_num) {
      cl_input_size[i] = tmp_output_size[i];
      input_block_num[i] = cl_input_size[i] / BLOCK_SIZE;
      printf("last input %d block num: %u\n", i, input_block_num[i]);
      cl_output_size += cl_input_size[i];
    } else  // empty path
    {
      input_block_num[i] = 0;
      cl_input_size[i] = 4096;
    }
  }
  cl_output_offset = cl_output_size;

  output_buf_ptr_last = (uint512_t *)malloc(cl_output_size);
  if (output_buf_ptr_last == NULL) {
    printf("malloc file output buffer failed\n");
  }

  compaction(output_buf_ptr[0], output_buf_ptr[1], output_buf_ptr[2],
             output_buf_ptr[3], output_buf_ptr[4], output_buf_ptr[5],
             output_buf_ptr[6], output_buf_ptr[7], output_buf_ptr_last,
             input_block_num[0], input_block_num[1], input_block_num[2],
             input_block_num[3], input_block_num[4], input_block_num[5],
             input_block_num[6], input_block_num[7], smallest_snapshot);
}
#else
void HW::compaction_pre(int input_num, std::string *input_file_name,
                        uint64_t smallest_snapshot, int flag) {
  FILE *pFile[NumInput] = {NULL};
  uint32_t input_block_num[NumInput] = {0};
  uint64_t cl_input_size[NumInput] = {0};
  uint64_t cl_output_size = 0;

  printf("\n");
  for (int i = 0; i < NumInput; i++) {
    if (i < input_num) {
      pFile[i] = fopen(input_file_name[i].c_str(), "rb");
      if (pFile[i] == NULL) {
        printf("open input file %d failed\n", i);
      }
      fseek(pFile[i], 0, SEEK_END);
      uint32_t file_size = ftell(pFile[i]);
      input_block_num[i] = file_size / BLOCK_SIZE;
      printf("input %d block num: %u\n", i, input_block_num[i]);
      fseek(pFile[i], 0, SEEK_SET);

      cl_input_size[i] = input_block_num[i] * BLOCK_SIZE;
      cl_output_size += cl_input_size[i];
    } else  // empty path
    {
      input_block_num[i] = 0;
      cl_input_size[i] = 4096;
    }
  }

  if (input_num >= 2) {
    fread(input_buf_ptr0.data() + cl_input_offset[0], 1, cl_input_size[0],
          pFile[0]);
    fread(input_buf_ptr1.data() + cl_input_offset[1], 1, cl_input_size[1],
          pFile[1]);
  }
  if (input_num >= 3) {
    fread(input_buf_ptr2.data() + cl_input_offset[2], 1, cl_input_size[2],
          pFile[2]);
  }
  if (input_num >= 4) {
    fread(input_buf_ptr3.data() + cl_input_offset[3], 1, cl_input_size[3],
          pFile[3]);
  }
  if (input_num >= 5) {
    fread(input_buf_ptr4.data() + cl_input_offset[4], 1, cl_input_size[4],
          pFile[4]);
  }
  if (input_num >= 6) {
    fread(input_buf_ptr5.data() + cl_input_offset[5], 1, cl_input_size[5],
          pFile[5]);
  }
  if (input_num >= 7) {
    fread(input_buf_ptr6.data() + cl_input_offset[6], 1, cl_input_size[6],
          pFile[6]);
  }
  if (input_num == 8) {
    fread(input_buf_ptr7.data() + cl_input_offset[7], 1, cl_input_size[7],
          pFile[7]);
  }

  for (int i = 0; i < input_num; i++) {
    fclose(pFile[i]);
    pFile[i] = NULL;
  }

  OCL_CHECK(err, buffer_in0[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[0],
                     input_buf_ptr0.data() + cl_input_offset[0], &err));
  OCL_CHECK(err, buffer_in1[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[1],
                     input_buf_ptr1.data() + cl_input_offset[1], &err));
  OCL_CHECK(err, buffer_in2[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[2],
                     input_buf_ptr2.data() + cl_input_offset[2], &err));
  OCL_CHECK(err, buffer_in3[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[3],
                     input_buf_ptr3.data() + cl_input_offset[3], &err));
  OCL_CHECK(err, buffer_in4[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[4],
                     input_buf_ptr4.data() + cl_input_offset[4], &err));
  OCL_CHECK(err, buffer_in5[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[5],
                     input_buf_ptr5.data() + cl_input_offset[5], &err));
  OCL_CHECK(err, buffer_in6[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[6],
                     input_buf_ptr6.data() + cl_input_offset[6], &err));
  OCL_CHECK(err, buffer_in7[flag] = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[7],
                     input_buf_ptr7.data() + cl_input_offset[7], &err));
  OCL_CHECK(
      err, buffer_out[flag] = cl::Buffer(
               context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, cl_output_size,
               output_buf_ptr.data() + cl_output_offset, &err));
  for (int i = 0; i < NumInput; i++) {
    cl_input_offset[i] += (cl_input_size[i] / 4);
  }
  cl_output_offset += (cl_output_size / 4);

  int narg = 0;
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in0[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in1[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in2[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in3[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in4[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in5[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in6[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in7[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_out[flag]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[0]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[1]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[2]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[3]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[4]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[5]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[6]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[7]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, smallest_snapshot));

  vector<cl::Event> write_event(1);
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects(
                     {buffer_in0[flag], buffer_in1[flag], buffer_in2[flag],
                      buffer_in3[flag], buffer_in4[flag], buffer_in5[flag],
                      buffer_in6[flag], buffer_in7[flag]},
                     0 /*0 means from host*/, nullptr, &write_event[0]));
  set_callback(write_event[0], "ooo_queue");

  std::vector<cl::Event> waitList;
  waitList.push_back(write_event[0]);
  OCL_CHECK(err, err = q.enqueueNDRangeKernel(krnl_compact, 0, 1, 1, &waitList,
                                              &kernel_events[flag]));
  set_callback(kernel_events[flag], "ooo_queue");

  std::vector<cl::Event> eventList;
  eventList.push_back(kernel_events[flag]);
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects(
                     {buffer_out[flag]}, CL_MIGRATE_MEM_OBJECT_HOST, &eventList,
                     &read_events[flag]));
  set_callback(read_events[flag], "ooo_queue");
}

void HW::compaction_post(int idx) {
  uint32_t output_block_num = output_buf_ptr[tmp_output_offset[idx] + 15];
  printf("\noutput block num: %u\n", output_block_num);
  tmp_output_offset[idx + 1] =
      tmp_output_offset[idx] + (uint64_t)BLOCK_SIZE * output_block_num / 4;
}

void HW::compaction_post_single(std::string input_file_name, int idx) {
  printf("\n");
  FILE *pFile = fopen(input_file_name.c_str(), "rb");
  if (pFile == NULL) {
    printf("open input file %d failed\n", idx * NumInput);
  }
  fseek(pFile, 0, SEEK_END);
  uint32_t file_size = ftell(pFile);
  uint32_t input_block_num = file_size / BLOCK_SIZE;
  printf("input %d block num: %u\n", idx * NumInput, input_block_num);
  fseek(pFile, 0, SEEK_SET);
  uint64_t cl_input_size = input_block_num * BLOCK_SIZE;

  fread(output_buf_ptr.data() + tmp_output_offset[idx], 1, cl_input_size,
        pFile);
  tmp_output_offset[idx + 1] = tmp_output_offset[idx] + cl_input_size / 4;

  fclose(pFile);
  pFile = NULL;
}

void HW::compaction_last(int input_num, uint64_t smallest_snapshot) {
  uint32_t input_block_num[NumInput] = {0};
  uint64_t cl_input_size[NumInput] = {0};
  uint64_t cl_output_size = 0;

  printf("\n");
  for (int i = 0; i < NumInput; i++) {
    if (i < input_num) {
      cl_input_size[i] = (tmp_output_offset[i + 1] - tmp_output_offset[i]) * 4;
      input_block_num[i] = cl_input_size[i] / BLOCK_SIZE;
      printf("last input %d block num: %u\n", i, input_block_num[i]);
      cl_output_size += cl_input_size[i];
    } else  // empty path
    {
      input_block_num[i] = 0;
      cl_input_size[i] = 4096;
      tmp_output_offset[i] = tmp_output_offset[i - 1] + 1024;
    }
  }
  cl_output_offset = cl_output_size;

  cl::Buffer buffer_in0_last;
  cl::Buffer buffer_in1_last;
  cl::Buffer buffer_in2_last;
  cl::Buffer buffer_in3_last;
  cl::Buffer buffer_in4_last;
  cl::Buffer buffer_in5_last;
  cl::Buffer buffer_in6_last;
  cl::Buffer buffer_in7_last;
  cl::Buffer buffer_out_last;

  OCL_CHECK(err, buffer_in0_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[0],
                     output_buf_ptr.data() + tmp_output_offset[0], &err));
  OCL_CHECK(err, buffer_in1_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[1],
                     output_buf_ptr.data() + tmp_output_offset[1], &err));
  OCL_CHECK(err, buffer_in2_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[2],
                     output_buf_ptr.data() + tmp_output_offset[2], &err));
  OCL_CHECK(err, buffer_in3_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[3],
                     output_buf_ptr.data() + tmp_output_offset[3], &err));
  OCL_CHECK(err, buffer_in4_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[4],
                     output_buf_ptr.data() + tmp_output_offset[4], &err));
  OCL_CHECK(err, buffer_in5_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[5],
                     output_buf_ptr.data() + tmp_output_offset[5], &err));
  OCL_CHECK(err, buffer_in6_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[6],
                     output_buf_ptr.data() + tmp_output_offset[6], &err));
  OCL_CHECK(err, buffer_in7_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,
                     cl_input_size[7],
                     output_buf_ptr.data() + tmp_output_offset[7], &err));
  OCL_CHECK(err, buffer_out_last = cl::Buffer(
                     context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY,
                     cl_output_size, output_buf_ptr_last.data(), &err));

  int narg = 0;
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in0_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in1_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in2_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in3_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in4_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in5_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in6_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_in7_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, buffer_out_last));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[0]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[1]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[2]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[3]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[4]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[5]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[6]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, input_block_num[7]));
  OCL_CHECK(err, err = krnl_compact.setArg(narg++, smallest_snapshot));

  OCL_CHECK(err, err = q.enqueueMigrateMemObjects(
                     {buffer_in0_last, buffer_in1_last, buffer_in2_last,
                      buffer_in3_last, buffer_in4_last, buffer_in5_last,
                      buffer_in6_last, buffer_in7_last},
                     0 /*0 means from host*/));
  OCL_CHECK(err, err = q.enqueueTask(krnl_compact));

  OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_out_last},
                                                  CL_MIGRATE_MEM_OBJECT_HOST));
  OCL_CHECK(err, err = q.finish());
}

void HW::read_events_wait(int flag) {
  OCL_CHECK(err, err = read_events[flag].wait());
}

void HW::first_stage_wait() {
  printf("Waiting...\n");
  OCL_CHECK(err, err = q.flush());
  OCL_CHECK(err, err = q.finish());
}
#endif

void HW::free_resource() {
  cl_output_offset = 0;
#ifdef EMU
  for (int i = 0; i < NumInput; i++) {
    if (output_buf_ptr[i] != NULL) {
      free(output_buf_ptr[i]);
      output_buf_ptr[i] = NULL;
    }
    tmp_output_size[i] = 0;
  }
  if (output_buf_ptr_last != NULL) {
    free(output_buf_ptr_last);
    output_buf_ptr_last = NULL;
  }
#else
  for (int i = 0; i < NumInput; i++) {
    cl_input_offset[i] = 0;
    tmp_output_offset[i] = 0;
  }
  tmp_output_offset[NumInput] = 0;

#endif
  float throughput_MBs = (float)cl_output_offset / end_micros;
  printf("time                %.2f ms\n", end_micros / 1000.0);
  printf("throughput          %.2f MB/s\n", throughput_MBs);
  printf("end\n");
}

}  // namespace ROCKSDB_NAMESPACE
