// Copyright 2010 Google Inc. All Rights Reserved.
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
// Function to find maximal matching prefixes of strings.

#ifndef BROTLI_ENC_FIND_MATCH_LENGTH_H_
#define BROTLI_ENC_FIND_MATCH_LENGTH_H_

#include <stdint.h>

#include "./port.h"

namespace brotli {

// Separate implementation for x86_64, for speed.
#if defined(__GNUC__) && defined(ARCH_K8)

static inline int FindMatchLengthWithLimit(const uint8_t* s1,
                                           const uint8_t* s2,
                                           size_t limit) {
  int matched = 0;
  size_t limit2 = (limit >> 3) + 1;  // + 1 is for pre-decrement in while
  while (PREDICT_TRUE(--limit2)) {
    if (PREDICT_FALSE(BROTLI_UNALIGNED_LOAD64(s2) ==
                      BROTLI_UNALIGNED_LOAD64(s1 + matched))) {
      s2 += 8;
      matched += 8;
    } else {
      uint64_t x =
          BROTLI_UNALIGNED_LOAD64(s2) ^ BROTLI_UNALIGNED_LOAD64(s1 + matched);
      int matching_bits =  __builtin_ctzll(x);
      matched += matching_bits >> 3;
      return matched;
    }
  }
  limit = (limit & 7) + 1;  // + 1 is for pre-decrement in while
  while (--limit) {
    if (PREDICT_TRUE(s1[matched] == *s2)) {
      ++s2;
      ++matched;
    } else {
      return matched;
    }
  }
  return matched;
}
#else
static inline int FindMatchLengthWithLimit(const uint8_t* s1,
                                           const uint8_t* s2,
                                           size_t limit) {
  int matched = 0;
  const uint8_t* s2_limit = s2 + limit;
  const uint8_t* s2_ptr = s2;
  // Find out how long the match is. We loop over the data 32 bits at a
  // time until we find a 32-bit block that doesn't match; then we find
  // the first non-matching bit and use that to calculate the total
  // length of the match.
  while (s2_ptr <= s2_limit - 4 &&
         BROTLI_UNALIGNED_LOAD32(s2_ptr) ==
         BROTLI_UNALIGNED_LOAD32(s1 + matched)) {
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

}  // namespace brotli

#endif  // BROTLI_ENC_FIND_MATCH_LENGTH_H_
