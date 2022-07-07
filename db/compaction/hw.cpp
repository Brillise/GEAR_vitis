#include "db/compaction/hw.h"

#include "rocksdb/env.h"

namespace ROCKSDB_NAMESPACE {

HW::HW() {
#ifdef HARDWARE
  std::string binaryPath = "/home/centos/Documents/vitis_compaction_meta/";
  std::string binaryFileName =
      binaryPath + "compaction/Hardware/compaction_meta.awsxclbin";
#ifndef EMU
  init_device(context, q, krnl_compact, binaryFileName, err);
#endif

#ifdef TransTime
  env_hw = Env::Default();
  total_micros = 0;
#endif
#endif
  printf("--- HW engine successfully initialized -------------------------\n");
}

HW::~HW() { printf("--- HW shutdown-------------------------\n"); }

void HW::InitInputFileSimple(std::vector<std::string> dbname,
                             uint64_t smallest_snapshot) {
#ifdef EMU
  FILE *pFile[NumInput] = {NULL};
  for (int i = 0; i < NumInput; i++) {
    pFile[i] = fopen(dbname[i].c_str(), "rb");
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
    input_buf_ptr[i] = (uint512_t *)malloc(cl_input_size[i]);
    if (input_buf_ptr[i] == NULL) {
      printf("malloc file %d input buffer failed\n", i);
    }
    fread(input_buf_ptr[i], 1, cl_input_size[i], pFile[i]);
    fclose(pFile[i]);
    pFile[i] = NULL;
  }

  output_buf_ptr = (uint512_t *)malloc(cl_output_size);
  if (output_buf_ptr == NULL) {
    printf("malloc file output buffer failed\n");
  }
  uint64_t block_num_info = input_block_num[1];
  block_num_info = block_num_info << 32 | input_block_num[0];
  compaction_emu(input_buf_ptr[0], input_buf_ptr[1], output_buf_ptr,
                 block_num_info, smallest_snapshot);
#else
  FILE *pFile[NumInput] = {NULL};
  for (int i = 0; i < NumInput; i++) {
    pFile[i] = fopen(dbname[i].c_str(), "rb");
    if (pFile[i] == NULL) {
      printf("open input file %d failed\n", i);
    }
    fseek(pFile[i], 0, SEEK_END);
    uint32_t file_size = ftell(pFile[i]);
    input_block_num[i] = file_size / BLOCK_SIZE;
#ifdef HWdebug
    printf("input %d block num: %u\n", i, input_block_num[i]);
#endif
    fseek(pFile[i], 0, SEEK_SET);

    cl_input_size[i] = input_block_num[i] * BLOCK_SIZE;
    cl_output_size += cl_input_size[i];
  }

  input_buf_ptr0.resize(cl_input_size[0] / 4);
  input_buf_ptr1.resize(cl_input_size[1] / 4);
  output_buf_ptr.resize(cl_output_size / 4);
  fread(input_buf_ptr0.data(), 1, cl_input_size[0], pFile[0]);
  fread(input_buf_ptr1.data(), 1, cl_input_size[1], pFile[1]);
  fclose(pFile[0]);
  fclose(pFile[1]);
  pFile[0] = NULL;
  pFile[1] = NULL;

  uint64_t block_num_info = input_block_num[1];
  block_num_info = block_num_info << 32 | input_block_num[0];

#ifdef HWdebug
  printf("\n");
  printf("run compaction\n");
#endif

  start_micros = env_hw->NowMicros();
  cl_mem_ext_ptr_t in_data_Ext0, in_data_Ext1;
  cl_mem_ext_ptr_t out_data_Ext;
  in_data_Ext0.flags = XCL_MEM_DDR_BANK0;
  in_data_Ext0.obj = input_buf_ptr0.data();
  in_data_Ext0.param = 0;
  in_data_Ext1.flags = XCL_MEM_DDR_BANK0;
  in_data_Ext1.obj = input_buf_ptr1.data();
  in_data_Ext1.param = 0;
  out_data_Ext.flags = XCL_MEM_DDR_BANK1;
  out_data_Ext.obj = output_buf_ptr.data();
  out_data_Ext.param = 0;
  OCL_CHECK(err, cl::Buffer buffer_in0(context,
                                       CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR |
                                           CL_MEM_EXT_PTR_XILINX,
                                       cl_input_size[0], &in_data_Ext0, &err));
  OCL_CHECK(err, cl::Buffer buffer_in1(context,
                                       CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR |
                                           CL_MEM_EXT_PTR_XILINX,
                                       cl_input_size[1], &in_data_Ext1, &err));
  OCL_CHECK(err, cl::Buffer buffer_out(context,
                                       CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR |
                                           CL_MEM_EXT_PTR_XILINX,
                                       cl_output_size, &out_data_Ext, &err));

  OCL_CHECK(err, err = krnl_compact.setArg(0, buffer_in0));
  OCL_CHECK(err, err = krnl_compact.setArg(1, buffer_in1));
  OCL_CHECK(err, err = krnl_compact.setArg(2, buffer_out));
  OCL_CHECK(err, err = krnl_compact.setArg(3, block_num_info));
  OCL_CHECK(err, err = krnl_compact.setArg(4, smallest_snapshot));
  // Copy input data to device global memory
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_in0, buffer_in1},
                                                  0 /* 0 means from host*/));

  OCL_CHECK(err, err = q.enqueueTask(krnl_compact));

  // Copy Result from Device Global Memory to Host Local Memory
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_out},
                                                  CL_MIGRATE_MEM_OBJECT_HOST));
#ifdef HWdebug
  printf("start\n");
#endif
#endif
}

void HW::run_compaction_post() {
#ifndef EMU
  q.finish();
#endif

#ifdef TransTime
  uint64_t end_micros = env_hw->NowMicros() - start_micros;
  float throughput_MBs = (float)cl_output_size / end_micros;
  total_micros += end_micros;
  printf("time                %.2f ms\n", end_micros / 1000.0);
  printf("throughput          %.2f MB/s\n", throughput_MBs);
  printf("total_time          %.2f ms\n", total_micros / 1000.0);
#endif

#ifdef HWdebug
  printf("end\n");
#endif
}

}  // namespace ROCKSDB_NAMESPACE
