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
// Write bits into a byte array.

#ifndef BROTLI_ENC_WRITE_BITS_H_
#define BROTLI_ENC_WRITE_BITS_H_

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "./port.h"

namespace brotli {

//#define BIT_WRITER_DEBUG

// This function writes bits into bytes in increasing addresses, and within
// a byte least-significant-bit first.
//
// The function can write up to 56 bits in one go with WriteBits
// Example: let's assume that 3 bits (Rs below) have been written already:
//
// BYTE-0     BYTE+1       BYTE+2
//
// 0000 0RRR    0000 0000    0000 0000
//
// Now, we could write 5 or less bits in MSB by just sifting by 3
// and OR'ing to BYTE-0.
//
// For n bits, we take the last 5 bits, OR that with high bits in BYTE-0,
// and locate the rest in BYTE+1, BYTE+2, etc.
inline void WriteBits(int n_bits,
                      uint64_t bits,
                      int * __restrict pos,
                      uint8_t * __restrict array) {
#ifdef BIT_WRITER_DEBUG
  printf("WriteBits  %2d  0x%016llx  %10d\n", n_bits, bits, *pos);
#endif
  assert(bits < 1ULL << n_bits);
#ifdef IS_LITTLE_ENDIAN
  // This branch of the code can write up to 56 bits at a time,
  // 7 bits are lost by being perhaps already in *p and at least
  // 1 bit is needed to initialize the bit-stream ahead (i.e. if 7
  // bits are in *p and we write 57 bits, then the next write will
  // access a byte that was never initialized).
  uint8_t *p = &array[*pos >> 3];
  uint64_t v = *p;
  v |= bits << (*pos & 7);
  BROTLI_UNALIGNED_STORE64(p, v);  // Set some bits.
  *pos += n_bits;
#else
  // implicit & 0xff is assumed for uint8_t arithmetics
  uint8_t *array_pos = &array[*pos >> 3];
  const int bits_reserved_in_first_byte = (*pos & 7);
  bits <<= bits_reserved_in_first_byte;
  *array_pos++ |= bits;
  for (int bits_left_to_write = n_bits - 8 + bits_reserved_in_first_byte;
       bits_left_to_write >= 1;
       bits_left_to_write -= 8) {
    bits >>= 8;
    *array_pos++ = bits;
  }
  *array_pos = 0;
  *pos += n_bits;
#endif
}

inline void WriteBitsPrepareStorage(int pos, uint8_t *array) {
#ifdef BIT_WRITER_DEBUG
  printf("WriteBitsPrepareStorage            %10d\n", pos);
#endif
  assert((pos & 7) == 0);
  array[pos >> 3] = 0;
}

}  // namespace brotli

#endif  // BROTLI_ENC_WRITE_BITS_H_
