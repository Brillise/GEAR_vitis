#pragma once

#include "db/compaction/common.h"
#include "rocksdb/rocksdb_namespace.h"

namespace ROCKSDB_NAMESPACE {

void compaction_emu(uint512_t *in0, uint512_t *in1, uint512_t *out,
                    uint64_t block_num_info, uint64_t smallest_snapshot_c);

}  // namespace ROCKSDB_NAMESPACE
