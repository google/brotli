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

#include "./prefix.h"

#include "./fast_log.h"

namespace brotli {

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

static const PrefixCodeRange kInsertLengthPrefixCode[kNumInsertLenPrefixes] = {
  {   0,  0}, {   1,  0}, {  2,   0}, {    3,  0},
  {   4,  0}, {   5,  0}, {  6,   1}, {    8,  1},
  {  10,  2}, {  14,  2}, { 18,   3}, {   26,  3},
  {  34,  4}, {  50,  4}, { 66,   5}, {   98,  5},
  { 130,  6}, { 194,  7}, { 322,  8}, {  578,  9},
  {1090, 10}, {2114, 12}, {6210, 14}, {22594, 24},
};

static const PrefixCodeRange kCopyLengthPrefixCode[kNumCopyLenPrefixes] = {
  {  2, 0}, {   3,  0}, {   4,  0}, {   5,  0},
  {  6, 0}, {   7,  0}, {   8,  0}, {   9,  0},
  { 10, 1}, {  12,  1}, {  14,  2}, {  18,  2},
  { 22, 3}, {  30,  3}, {  38,  4}, {  54,  4},
  { 70, 5}, { 102,  5}, { 134,  6}, { 198,  7},
  {326, 8}, { 582,  9}, {1094, 10}, {2118, 24},
};

static const int kInsertAndCopyRangeLut[9] = {
  0, 1, 4, 2, 3, 6, 5, 7, 8,
};

static const int kInsertRangeLut[9] = {
  0, 0, 1, 1, 0, 2, 1, 2, 2,
};

static const int kCopyRangeLut[9] = {
  0, 1, 0, 1, 2, 0, 2, 1, 2,
};

int InsertLengthPrefix(int length) {
  for (int i = 0; i < kNumInsertLenPrefixes; ++i) {
    const PrefixCodeRange& range = kInsertLengthPrefixCode[i];
    if (length >= range.offset && length < range.offset + (1 << range.nbits)) {
      return i;
    }
  }
  return -1;
}

int CopyLengthPrefix(int length) {
  for (int i = 0; i < kNumCopyLenPrefixes; ++i) {
    const PrefixCodeRange& range = kCopyLengthPrefixCode[i];
    if (length >= range.offset && length < range.offset + (1 << range.nbits)) {
      return i;
    }
  }
  return -1;
}

int CommandPrefix(int insert_length, int copy_length) {
  if (copy_length == 0) {
    copy_length = 4;
  }
  int insert_prefix = InsertLengthPrefix(insert_length);
  int copy_prefix = CopyLengthPrefix(copy_length);
  int range_idx = 3 * (insert_prefix >> 3) + (copy_prefix >> 3);
  return ((kInsertAndCopyRangeLut[range_idx] << 6) +
          ((insert_prefix & 7) << 3) + (copy_prefix & 7));
}

int InsertLengthExtraBits(int code) {
  int insert_code = (kInsertRangeLut[code >> 6] << 3) + ((code >> 3) & 7);
  return kInsertLengthPrefixCode[insert_code].nbits;
}

int InsertLengthOffset(int code) {
  int insert_code = (kInsertRangeLut[code >> 6] << 3) + ((code >> 3) & 7);
  return kInsertLengthPrefixCode[insert_code].offset;
}

int CopyLengthExtraBits(int code) {
  int copy_code = (kCopyRangeLut[code >> 6] << 3) + (code & 7);
  return kCopyLengthPrefixCode[copy_code].nbits;
}

int CopyLengthOffset(int code) {
  int copy_code = (kCopyRangeLut[code >> 6] << 3) + (code & 7);
  return kCopyLengthPrefixCode[copy_code].offset;
}

void PrefixEncodeCopyDistance(int distance_code,
                              int num_direct_codes,
                              int postfix_bits,
                              uint16_t* code,
                              int* nbits,
                              uint32_t* extra_bits) {
  distance_code -= 1;
  if (distance_code < kNumDistanceShortCodes + num_direct_codes) {
    *code = distance_code;
    *nbits = 0;
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
  *nbits = bucket - postfix_bits;
  *code = kNumDistanceShortCodes + num_direct_codes +
      ((2 * (*nbits - 1) + prefix) << postfix_bits) + postfix;
  *extra_bits = (distance_code - offset) >> postfix_bits;
}

int BlockLengthPrefix(int length) {
  for (int i = 0; i < kNumBlockLenPrefixes; ++i) {
    const PrefixCodeRange& range = kBlockLengthPrefixCode[i];
    if (length >= range.offset && length < range.offset + (1 << range.nbits)) {
      return i;
    }
  }
  return -1;
}

int BlockLengthExtraBits(int length_code) {
  return kBlockLengthPrefixCode[length_code].nbits;
}

int BlockLengthOffset(int length_code) {
  return kBlockLengthPrefixCode[length_code].offset;
}

}  // namespace brotli
