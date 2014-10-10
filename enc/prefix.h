// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Functions for encoding of integers into prefix codes the amount of extra
// bits, and the actual values of the extra bits.

#ifndef BROTLI_ENC_PREFIX_H_
#define BROTLI_ENC_PREFIX_H_

#include <stdint.h>
#include "./fast_log.h"

namespace brotli {

static const int kNumInsertLenPrefixes = 24;
static const int kNumCopyLenPrefixes = 24;
static const int kNumCommandPrefixes = 704;
static const int kNumBlockLenPrefixes = 26;
static const int kNumDistanceShortCodes = 16;
static const int kNumDistancePrefixes = 520;

// Represents the range of values belonging to a prefix code:
// [offset, offset + 2^nbits)
struct PrefixCodeRange {
  int offset;
  int nbits;
};

static const PrefixCodeRange kBlockLengthPrefixCode[kNumBlockLenPrefixes] = {
  {   1,  2}, {    5,  2}, {  9,   2}, {  13,  2},
  {  17,  3}, {   25,  3}, {  33,  3}, {  41,  3},
  {  49,  4}, {   65,  4}, {  81,  4}, {  97,  4},
  { 113,  5}, {  145,  5}, { 177,  5}, { 209,  5},
  { 241,  6}, {  305,  6}, { 369,  7}, { 497,  8},
  { 753,  9}, { 1265, 10}, {2289, 11}, {4337, 12},
  {8433, 13}, {16625, 24}
};

inline void GetBlockLengthPrefixCode(int len,
                                     int* code, int* n_extra, int* extra) {
  *code = 0;
  while (*code < 25 && len >= kBlockLengthPrefixCode[*code + 1].offset) {
    ++(*code);
  }
  *n_extra = kBlockLengthPrefixCode[*code].nbits;
  *extra = len - kBlockLengthPrefixCode[*code].offset;
}

inline void PrefixEncodeCopyDistance(int distance_code,
                                     int num_direct_codes,
                                     int postfix_bits,
                                     uint16_t* code,
                                     uint32_t* extra_bits) {
  distance_code -= 1;
  if (distance_code < kNumDistanceShortCodes + num_direct_codes) {
    *code = distance_code;
    *extra_bits = 0;
    return;
  }
  distance_code -= kNumDistanceShortCodes + num_direct_codes;
  distance_code += (1 << (postfix_bits + 2));
  int bucket = Log2Floor(distance_code) - 1;
  int postfix_mask = (1 << postfix_bits) - 1;
  int postfix = distance_code & postfix_mask;
  int prefix = (distance_code >> bucket) & 1;
  int offset = (2 + prefix) << bucket;
  int nbits = bucket - postfix_bits;
  *code = kNumDistanceShortCodes + num_direct_codes +
      ((2 * (nbits - 1) + prefix) << postfix_bits) + postfix;
  *extra_bits = (nbits << 24) | ((distance_code - offset) >> postfix_bits);
}

}  // namespace brotli

#endif  // BROTLI_ENC_PREFIX_H_
