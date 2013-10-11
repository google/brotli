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

#include <assert.h>

#include "./bit_reader.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define MAX_NUM_BIT_READ 25

#define LBITS 64      // Number of bits prefetched.
#define WBITS 32      // Minimum number of bytes needed after
                      // BrotliFillBitWindow.
#define LOG8_WBITS 4  // Number of bytes needed to store WBITS bits.

static const uint32_t kBitMask[MAX_NUM_BIT_READ] = {
  0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767,
  65535, 131071, 262143, 524287, 1048575, 2097151, 4194303, 8388607, 16777215
};

void BrotliInitBitReader(BrotliBitReader* const br,
                         const uint8_t* const start,
                         size_t length) {
  size_t i;
  assert(br != NULL);
  assert(start != NULL);
  assert(length < 0xfffffff8u);   // can't happen with a RIFF chunk.

  br->buf_ = start;
  br->len_ = length;
  br->val_ = 0;
  br->pos_ = 0;
  br->bit_pos_ = 0;
  br->eos_ = 0;
  br->error_ = 0;
  for (i = 0; i < sizeof(br->val_) && i < br->len_; ++i) {
    br->val_ |= ((uint64_t)br->buf_[br->pos_]) << (8 * i);
    ++br->pos_;
  }
}

void BrotliBitReaderSetBuffer(BrotliBitReader* const br,
                              const uint8_t* const buf, size_t len) {
  assert(br != NULL);
  assert(buf != NULL);
  assert(len < 0xfffffff8u);   // can't happen with a RIFF chunk.
  br->eos_ = (br->pos_ >= len);
  br->buf_ = buf;
  br->len_ = len;
}

// If not at EOS, reload up to LBITS byte-by-byte
static void ShiftBytes(BrotliBitReader* const br) {
  while (br->bit_pos_ >= 8 && br->pos_ < br->len_) {
    br->val_ >>= 8;
    br->val_ |= ((uint64_t)br->buf_[br->pos_]) << (LBITS - 8);
    ++br->pos_;
    br->bit_pos_ -= 8;
  }
}

void BrotliFillBitWindow(BrotliBitReader* const br) {
  if (br->bit_pos_ >= WBITS) {
#if (defined(__x86_64__) || defined(_M_X64))
    if (br->pos_ + sizeof(br->val_) < br->len_) {
      br->val_ >>= WBITS;
      br->bit_pos_ -= WBITS;
      // The expression below needs a little-endian arch to work correctly.
      // This gives a large speedup for decoding speed.
      br->val_ |= *(const uint64_t*)(br->buf_ + br->pos_) << (LBITS - WBITS);
      br->pos_ += LOG8_WBITS;
      return;
    }
#endif
    ShiftBytes(br);       // Slow path.
    if (br->pos_ == br->len_ && br->bit_pos_ == LBITS) {
      br->eos_ = 1;
    }
  }
}

uint32_t BrotliReadBits(BrotliBitReader* const br, int n_bits) {
  assert(n_bits >= 0);
  // Flag an error if end_of_stream or n_bits is more than allowed limit.
  if (n_bits == 0 || (!br->eos_ && n_bits < MAX_NUM_BIT_READ)) {
    const uint32_t val =
        (uint32_t)(br->val_ >> br->bit_pos_) & kBitMask[n_bits];
    const int new_bits = br->bit_pos_ + n_bits;
    br->bit_pos_ = new_bits;
    // If this read is going to cross the read buffer, set the eos flag.
    if (br->pos_ == br->len_) {
      if (new_bits >= LBITS) {
        br->eos_ = 1;
      }
    }
    ShiftBytes(br);
    return val;
  } else {
    br->error_ = 1;
    return 0;
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
