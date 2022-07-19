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

void event_cb(cl_event event1, cl_int cmd_status, void *data);

void set_callback(cl::Event event, const char *queue_name);

}  // namespace ROCKSDB_NAMESPACE
#endif
