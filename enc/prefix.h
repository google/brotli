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

namespace brotli {

static const int kNumInsertLenPrefixes = 24;
static const int kNumCopyLenPrefixes = 24;
static const int kNumCommandPrefixes = 704;
static const int kNumBlockLenPrefixes = 26;
static const int kNumDistanceShortCodes = 16;
static const int kNumDistancePrefixes = 520;

int CommandPrefix(int insert_length, int copy_length);
int InsertLengthExtraBits(int prefix);
int InsertLengthOffset(int prefix);
int CopyLengthExtraBits(int prefix);
int CopyLengthOffset(int prefix);

void PrefixEncodeCopyDistance(int distance_code,
                              int num_direct_codes,
                              int shift_bits,
                              uint16_t* prefix,
                              int* nbits,
                              uint32_t* extra_bits);

int BlockLengthPrefix(int length);
int BlockLengthExtraBits(int prefix);
int BlockLengthOffset(int prefix);

}  // namespace brotli

#endif  // BROTLI_ENC_PREFIX_H_
