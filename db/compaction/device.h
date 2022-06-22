#ifndef DEVICE_H_
#define DEVICE_H_
#include <ap_int.h>
#include <ap_utils.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "db/compaction/common.h"
#include "db/compaction/xcl2.hpp"
#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {
// Device Configuration Process
void init_device(cl::Context &context, cl::CommandQueue &q,
                 cl::Kernel &krnl_compact, const std::string &binaryFileName,
                 cl_int &err);

// Assigns Device Buffers with DDR
cl_mem_ext_ptr_t get_buffer_extension(int ddr_no);
}  // namespace ROCKSDB_NAMESPACE
#endif
