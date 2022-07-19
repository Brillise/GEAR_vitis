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
//#define EMU

#ifdef EMU
#include "db/compaction/kernel_emu.h"
#endif
using std::vector;
namespace ROCKSDB_NAMESPACE {

class HW {
 public:
  HW();   // constructor
  ~HW();  // destructor

 public:
#ifdef EMU
  void compaction_emu(int input_num, std::string *input_file_name,
                      uint64_t smallest_snapshot, int idx);
  void compaction_last_emu(int input_num, uint64_t smallest_snapshot);
#else
  void compaction_pre(int input_num, std::string *input_file_name,
                      uint64_t smallest_snapshot, int flag);
  void compaction_post(int idx);
  void compaction_post_single(std::string input_file_name, int idx);
  void compaction_last(int input_num, uint64_t smallest_snapshot);
  void read_events_wait(int flag);
  void first_stage_wait();
#endif
  void free_resource();

 private:
  uint64_t cl_output_offset = 0;
#ifdef EMU
  uint512_t *input_buf_ptr[NumInput] = {NULL};
  uint64_t tmp_output_size[NumInput] = {0};
#else
  cl_int err;
  cl::Context context;
  cl::Kernel krnl_compact;
  cl::CommandQueue q;
  vector<cl::Event> kernel_events;
  vector<cl::Event> read_events;
  cl::Buffer buffer_in0[2];
  cl::Buffer buffer_in1[2];
  cl::Buffer buffer_in2[2];
  cl::Buffer buffer_in3[2];
  cl::Buffer buffer_in4[2];
  cl::Buffer buffer_in5[2];
  cl::Buffer buffer_in6[2];
  cl::Buffer buffer_in7[2];
  cl::Buffer buffer_out[2];
  std::vector<int, aligned_allocator<int> > input_buf_ptr0;
  std::vector<int, aligned_allocator<int> > input_buf_ptr1;
  std::vector<int, aligned_allocator<int> > input_buf_ptr2;
  std::vector<int, aligned_allocator<int> > input_buf_ptr3;
  std::vector<int, aligned_allocator<int> > input_buf_ptr4;
  std::vector<int, aligned_allocator<int> > input_buf_ptr5;
  std::vector<int, aligned_allocator<int> > input_buf_ptr6;
  std::vector<int, aligned_allocator<int> > input_buf_ptr7;
  uint32_t cl_input_offset[NumInput] = {0};
  uint64_t tmp_output_offset[NumInput + 1] = {0};
#endif

 public:
  uint64_t end_micros = 0;
#ifdef EMU
  uint512_t *output_buf_ptr[NumInput] = {NULL};
  uint512_t *output_buf_ptr_last = NULL;
#else
  std::vector<int, aligned_allocator<int> > output_buf_ptr;
  std::vector<int, aligned_allocator<int> > output_buf_ptr_last;
#endif
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // ROCKSDB_LITE