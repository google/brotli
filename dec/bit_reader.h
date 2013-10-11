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
// Bit reading helpers

#ifndef BROTLI_DEC_BIT_READER_H_
#define BROTLI_DEC_BIT_READER_H_

#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct {
  uint64_t       val_;        // pre-fetched bits
  const uint8_t* buf_;        // input byte buffer
  size_t         len_;        // buffer length
  size_t         pos_;        // byte position in buf_
  int            bit_pos_;    // current bit-reading position in val_
  int            eos_;        // bitstream is finished
  int            error_;      // an error occurred (buffer overflow attempt...)
} BrotliBitReader;

void BrotliInitBitReader(BrotliBitReader* const br,
                         const uint8_t* const start,
                         size_t length);

//  Sets a new data buffer.
void BrotliBitReaderSetBuffer(BrotliBitReader* const br,
                              const uint8_t* const buffer, size_t length);

// Reads the specified number of bits from Read Buffer.
// Flags an error in case end_of_stream or n_bits is more than allowed limit.
// Flags eos if this read attempt is going to cross the read buffer.
uint32_t BrotliReadBits(BrotliBitReader* const br, int n_bits);

// Return the prefetched bits, so they can be looked up.
static BROTLI_INLINE uint32_t BrotliPrefetchBits(BrotliBitReader* const br) {
  return (uint32_t)(br->val_ >> br->bit_pos_);
}

// For jumping over a number of bits in the bit stream when accessed with
// BrotliPrefetchBits and BrotliFillBitWindow.
static BROTLI_INLINE void BrotliSetBitPos(BrotliBitReader* const br, int val) {
  br->bit_pos_ = val;
}

// Advances the Read buffer by 4 bytes to make room for reading next 32 bits.
void BrotliFillBitWindow(BrotliBitReader* const br);

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif

#endif  // BROTLI_DEC_BIT_READER_H_
