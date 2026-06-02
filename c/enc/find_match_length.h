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

#if defined(BROTLI_RVV_1)
#include <riscv_vector.h>

static BROTLI_INLINE size_t FindMatchLengthWithLimit(const uint8_t* s1,
                                                     const uint8_t* s2,
                                                     size_t limit) {
  const uint8_t* s1_orig = s1;
  while (limit > 0) {
    size_t vl = __riscv_vsetvl_e8m1(limit);
    vuint8m1_t v1 = __riscv_vle8_v_u8m1(s1, vl);
    vuint8m1_t v2 = __riscv_vle8_v_u8m1(s2, vl);
    vbool8_t mask = __riscv_vmsne_vv_u8m1_b8(v1, v2, vl);
    long first_diff = __riscv_vfirst_m_b8(mask, vl);
    if (first_diff >= 0) {
      return (size_t)(s1 - s1_orig) + (size_t)first_diff;
    }
    s1 += vl;
    s2 += vl;
    limit -= vl;
  }
  return (size_t)(s1 - s1_orig);
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
