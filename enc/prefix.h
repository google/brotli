/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Functions for encoding of integers into prefix codes the amount of extra
// bits, and the actual values of the extra bits.

#ifndef BROTLI_ENC_PREFIX_H_
#define BROTLI_ENC_PREFIX_H_

#include "./fast_log.h"
#include "./types.h"

namespace brotli {

static const uint32_t kNumInsertLenPrefixes = 24;
static const uint32_t kNumCopyLenPrefixes = 24;
static const uint32_t kNumCommandPrefixes = 704;
static const uint32_t kNumBlockLenPrefixes = 26;
static const uint32_t kNumDistanceShortCodes = 16;
static const uint32_t kNumDistancePrefixes = 520;

// Represents the range of values belonging to a prefix code:
// [offset, offset + 2^nbits)
struct PrefixCodeRange {
  uint32_t offset;
  uint32_t nbits;
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

inline void GetBlockLengthPrefixCode(uint32_t len, uint32_t* code,
                                     uint32_t* n_extra, uint32_t* extra) {
  *code = 0;
  while (*code < 25 && len >= kBlockLengthPrefixCode[*code + 1].offset) {
    ++(*code);
  }
  *n_extra = kBlockLengthPrefixCode[*code].nbits;
  *extra = len - kBlockLengthPrefixCode[*code].offset;
}

inline void PrefixEncodeCopyDistance(size_t distance_code,
                                     size_t num_direct_codes,
                                     size_t postfix_bits,
                                     uint16_t* code,
                                     uint32_t* extra_bits) {
  if (distance_code < kNumDistanceShortCodes + num_direct_codes) {
    *code = static_cast<uint16_t>(distance_code);
    *extra_bits = 0;
    return;
  }
  distance_code -= kNumDistanceShortCodes + num_direct_codes;  /* >= 0 */
  distance_code += (1 << (postfix_bits + 2));  /* > 0 */
  size_t bucket = Log2FloorNonZero(distance_code) - 1;
  size_t postfix_mask = (1 << postfix_bits) - 1;
  size_t postfix = distance_code & postfix_mask;
  size_t prefix = (distance_code >> bucket) & 1;
  size_t offset = (2 + prefix) << bucket;
  size_t nbits = bucket - postfix_bits;
  *code = static_cast<uint16_t>(
      (kNumDistanceShortCodes + num_direct_codes +
       ((2 * (nbits - 1) + prefix) << postfix_bits) + postfix));
  *extra_bits = static_cast<uint32_t>(
      (nbits << 24) | ((distance_code - offset) >> postfix_bits));
}

}  // namespace brotli

#endif  // BROTLI_ENC_PREFIX_H_
