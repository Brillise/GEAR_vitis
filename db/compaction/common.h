#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <ap_int.h>
#include <cstddef>
#include "hls_stream.h"

#define NumInput (2)
#define BLOCK_SIZE (8192)
#define BLOCK_SIZE_512 (BLOCK_SIZE / 64)
#define SST_BLOCK_NUM (800)
#define SST_SIZE (BLOCK_SIZE * SST_BLOCK_NUM)

#define UserKeySize (8)
#define KeySize (16)
#define Unit (64 / KeySize)
#define ValueSize (10)
#define ValueWidth (ValueSize * 8)
#define uniValueWidth (ValueWidth * Unit)
#define EntryNum (312)
#define BURST_KEY_SIZE_512 ((EntryNum * KeySize - 1) / 64 + 1)
#define BURST_VALUE_SIZE_512 ((EntryNum * ValueSize - 1) / 64 + 1)

#define Align(a, b) ((a - 1) / b + 1)

typedef ap_uint<3> vint3_t;
typedef ap_uint<7> bitmap_t;
typedef ap_uint<128> uint128_t;
typedef ap_uint<512> uint512_t;
typedef ap_uint<uniValueWidth> univalue_t;
