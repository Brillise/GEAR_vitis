#pragma once

#ifndef ROCKSDB_LITE

#ifdef _WIN64
#define _CRT_SECURE_NO_WARNINGS
#else
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "db/compaction/common.h"
#include "db/compaction/device.h"
#include "rocksdb/env.h"
#include "rocksdb/rocksdb_namespace.h"

#define HARDWARE
#define TransTime
#define HWdebug
//#define EMU

#ifdef EMU
#include "db/compaction/kernel_emu.h"
#endif

namespace ROCKSDB_NAMESPACE {

class HW {
 public:
  HW();   // constructor
  ~HW();  // destructor

 public:
  void InitInputFileSimple(std::vector<std::string> dbname, int input_num,
                           uint64_t smallest_snapshot);
  void run_compaction_post();
  void free_resource();

 private:
  uint32_t input_block_num[NumInput];
  uint64_t cl_input_size[NumInput];
  uint64_t cl_output_size = 0;  // first 64 bytes for output results meta

#ifdef EMU
  uint512_t *input_buf_ptr[NumInput] = {NULL};
#else
  std::vector<int, aligned_allocator<int> > input_buf_ptr0;
  std::vector<int, aligned_allocator<int> > input_buf_ptr1;
  std::vector<int, aligned_allocator<int> > input_buf_ptr2;
  std::vector<int, aligned_allocator<int> > input_buf_ptr3;
  std::vector<int, aligned_allocator<int> > input_buf_ptr4;
  std::vector<int, aligned_allocator<int> > input_buf_ptr5;
  std::vector<int, aligned_allocator<int> > input_buf_ptr6;
  std::vector<int, aligned_allocator<int> > input_buf_ptr7;
#endif

  cl_int err;
  cl::Context context;
  cl::Kernel krnl_compact;
  cl::CommandQueue q;

#ifdef TransTime
  Env *env_hw;
  uint64_t start_micros;
#endif

 public:
#ifdef EMU
  uint512_t *output_buf_ptr = NULL;
#else
  std::vector<int, aligned_allocator<int> > output_buf_ptr;
#endif
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // ROCKSDB_LITE