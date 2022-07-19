#pragma once

#include <ap_int.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cstddef>
#include <string>

#include "hls_stream.h"

#define NumInput (8)
#define BLOCK_SIZE (8192)
#define BLOCK_SIZE_512 (BLOCK_SIZE / 64)

#define KeySize (16)
#define Unit (64 / KeySize)
#define ValueSize (10)
#define KeyWidth (KeySize * 8)
#define ValueWidth (ValueSize * 8)
#define EntryNum (312)
#define BURST_KEY_SIZE_512 ((EntryNum * KeySize - 1) / 64 + 1)
#define BURST_VALUE_SIZE_512 ((EntryNum * ValueSize - 1) / 64 + 1)

#define Align(a, b) ((a - 1) / b + 1)

typedef ap_uint<2> vint2_t;
typedef ap_uint<3> vint3_t;
typedef ap_uint<ValueWidth> value_t;
typedef ap_uint<KeyWidth> my_key_t;
typedef ap_uint<512> uint512_t;

#define SST_SIZE (50 * BLOCK_SIZE)
#define SST_BLOCK_NUM (SST_SIZE / BLOCK_SIZE)