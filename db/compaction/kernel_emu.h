#pragma once

#include "db/compaction/common.h"
#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

void compaction(uint512_t *in0, uint512_t *in1, uint512_t *in2, uint512_t *in3,
                uint512_t *in4, uint512_t *in5, uint512_t *in6, uint512_t *in7,
                uint512_t *out, uint32_t block_num0, uint32_t block_num1,
                uint32_t block_num2, uint32_t block_num3, uint32_t block_num4,
                uint32_t block_num5, uint32_t block_num6, uint32_t block_num7,
                uint64_t smallest_snapshot_c);

}  // namespace ROCKSDB_NAMESPACE
