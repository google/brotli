/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function to find maximal matching prefixes of strings. */

#ifndef BROTLI_ENC_FIND_MATCH_LENGTH_H_
#define BROTLI_ENC_FIND_MATCH_LENGTH_H_

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Separate implementation for little-endian 64-bit targets, for speed. */
#if defined(BROTLI_TARGET_RISCV_RVV) && BROTLI_64_BITS && BROTLI_LITTLE_ENDIAN
/* RISC-V RVV optimized version using vector instructions */
static BROTLI_INLINE size_t FindMatchLengthWithLimit(const uint8_t* s1,
                                                     const uint8_t* s2,
                                                     size_t limit) {
  const uint8_t *s1_orig = s1;
  size_t matched = 0;
  
  /* Process 16 bytes at a time using RVV vector instructions */
  while (limit >= 16) {
    size_t first_mismatch;
    asm volatile (
      "vsetvli zero, %1, e8, m1, ta, ma\n\t"
      "vle8.v v0, (%2)\n\t"
      "vle8.v v1, (%3)\n\t"
      "vmseq.vv v2, v0, v1\n\t"
      "vfirst.m %0, v2"
      : "=r" (first_mismatch)
      : "r" (16), "r" (s1), "r" (s2)
      : "memory"
    );
    if (first_mismatch < 16) {
      return matched + first_mismatch;
    }
    matched += 16;
    s1 += 16;
    s2 += 16;
    limit -= 16;
  }
  
  /* Handle remaining bytes using 8-byte blocks */
  for (; limit >= 8; limit -= 8) {
    uint64_t x = BROTLI_UNALIGNED_LOAD64LE(s2) ^
                 BROTLI_UNALIGNED_LOAD64LE(s1);
    s2 += 8;
    if (x != 0) {
      size_t matching_bits = (size_t)BROTLI_TZCNT64(x);
      return matched + (matching_bits >> 3);
    }
    s1 += 8;
    matched += 8;
  }
  
  /* Handle remaining bytes */
  while (limit && *s1 == *s2) {
    limit--;
    ++s2;
    ++s1;
    matched++;
  }
  return matched;
}
#elif defined(BROTLI_TZCNT64) && BROTLI_64_BITS && BROTLI_LITTLE_ENDIAN
static BROTLI_INLINE size_t FindMatchLengthWithLimit(const uint8_t* s1,
                                                     const uint8_t* s2,
                                                     size_t limit) {
  const uint8_t *s1_orig = s1;
  for (; limit >= 8; limit -= 8) {
    uint64_t x = BROTLI_UNALIGNED_LOAD64LE(s2) ^
                 BROTLI_UNALIGNED_LOAD64LE(s1);
    s2 += 8;
    if (x != 0) {
      size_t matching_bits = (size_t)BROTLI_TZCNT64(x);
      return (size_t)(s1 - s1_orig) + (matching_bits >> 3);
    }
    s1 += 8;
  }
  while (limit && *s1 == *s2) {
    limit--;
    ++s2;
    ++s1;
  }
  return (size_t)(s1 - s1_orig);
}
#else
static BROTLI_INLINE size_t FindMatchLengthWithLimit(const uint8_t* s1,
                                                     const uint8_t* s2,
                                                     size_t limit) {
  size_t matched = 0;
  const uint8_t* s2_limit = s2 + limit;
  const uint8_t* s2_ptr = s2;
  /* Find out how long the match is. We loop over the data 32 bits at a
     time until we find a 32-bit block that doesn't match; then we find
     the first non-matching bit and use that to calculate the total
     length of the match. */
  while (s2_ptr <= s2_limit - 4 &&
         BrotliUnalignedRead32(s2_ptr) ==
         BrotliUnalignedRead32(s1 + matched)) {
    s2_ptr += 4;
    matched += 4;
  }
  while ((s2_ptr < s2_limit) && (s1[matched] == *s2_ptr)) {
    ++s2_ptr;
    ++matched;
  }
  return matched;
}
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_FIND_MATCH_LENGTH_H_ */
