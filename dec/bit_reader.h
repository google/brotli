/* Copyright 2013 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

/* Bit reading helpers */

#ifndef BROTLI_DEC_BIT_READER_H_
#define BROTLI_DEC_BIT_READER_H_

#include <string.h>
#include "./port.h"
#include "./streams.h"
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_MAX_NUM_BIT_READ   25
#define BROTLI_READ_SIZE          1024
#define BROTLI_IMPLICIT_ZEROES    128
#define BROTLI_IBUF_SIZE          (BROTLI_READ_SIZE + BROTLI_IMPLICIT_ZEROES)
#define BROTLI_IBUF_MASK          (BROTLI_READ_SIZE - 1)

/* Masking with this expression turns to a single "Unsigned Bit Field Extract"
   UBFX instruction on ARM. */
static BROTLI_INLINE uint32_t BitMask(int n) { return ~((0xffffffff) << n); }

typedef struct {
#if (BROTLI_64_BITS_LITTLE_ENDIAN)
  uint64_t    val_;          /* pre-fetched bits */
#else
  uint32_t    val_;          /* pre-fetched bits */
#endif
  uint32_t    bit_pos_;      /* current bit-reading position in val_ */
  uint8_t*    next_in;       /* the byte we're reading from */
  uint32_t    avail_in;
  int         eos_;          /* input stream is finished */
  BrotliInput input_;        /* input callback */

  /* Input byte buffer, consist of a ringbuffer and a "slack" region where */
  /* bytes from the start of the ringbuffer are copied. */
  uint8_t buf_[BROTLI_IBUF_SIZE];
} BrotliBitReader;

/* Initializes the bitreader fields. After this, BrotliReadInput then
   BrotliWarmupBitReader must be used. */
void BrotliInitBitReader(BrotliBitReader* const br, BrotliInput input);

/* Initializes bit reading and bit position with the first input data available.
   Requires that there is enough input available (BrotliCheckInputAmount). */
void BrotliWarmupBitReader(BrotliBitReader* const br);

/* Pulls data from the input to the the read buffer.

   Returns 0 if one of:
    - the input callback returned an error, or
    - there is no more input and the position is past the end of the stream.
    - finish is false and less than BROTLI_READ_SIZE are available - a next call
      when more data is available makes it continue including the partially read
      data

   If finish is true and the end of the stream is reached,
   BROTLI_IMPLICIT_ZEROES additional zero bytes are copied to the ringbuffer.
*/
static BROTLI_INLINE int BrotliReadInput(
    BrotliBitReader* const br, int finish) {
  if (PREDICT_FALSE(br->eos_)) {
    return 0;
  } else {
    size_t i;
    int bytes_read;
    if (br->next_in != br->buf_) {
      for (i = 0; i < br->avail_in; i++) {
        br->buf_[i] = br->next_in[i];
      }
      br->next_in = br->buf_;
    }
    bytes_read = BrotliRead(br->input_, br->next_in + br->avail_in,
        (size_t)(BROTLI_READ_SIZE - br->avail_in));
    if (bytes_read < 0) {
      return 0;
    }
    br->avail_in += (uint32_t)bytes_read;
    if (br->avail_in < BROTLI_READ_SIZE) {
      if (!finish) {
        return 0;
      }
      br->eos_ = 1;
      /* Store BROTLI_IMPLICIT_ZEROES bytes of zero after the stream end. */
      memset(br->next_in + br->avail_in, 0, BROTLI_IMPLICIT_ZEROES);
      br->avail_in += BROTLI_IMPLICIT_ZEROES;
    }
    return 1;
  }
}

/* Returns amount of unread bytes the bit reader still has buffered from the
   BrotliInput, including whole bytes in br->val_. */
static BROTLI_INLINE size_t BrotliGetRemainingBytes(BrotliBitReader* br) {
  return br->avail_in + sizeof(br->val_) - (br->bit_pos_ >> 3);
}

/* Checks if there is at least num bytes left in the input ringbuffer (excluding
   the bits remaining in br->val_). The maximum value for num is
   BROTLI_IMPLICIT_ZEROES bytes. */
static BROTLI_INLINE int BrotliCheckInputAmount(
    BrotliBitReader* const br, size_t num) {
  return br->avail_in >= num;
}

/* Guarantees that there are at least n_bits in the buffer.
   n_bits should be in the range [1..24] */
static BROTLI_INLINE void BrotliFillBitWindow(
    BrotliBitReader* const br, int n_bits) {
#if (BROTLI_64_BITS_LITTLE_ENDIAN)
  if (IS_CONSTANT(n_bits) && n_bits <= 8) {
    if (br->bit_pos_ >= 56) {
      br->val_ >>= 56;
      br->bit_pos_ ^= 56;  /* here same as -= 56 because of the if condition */
      br->val_ |= (*(const uint64_t*)(br->next_in)) << 8;
      br->avail_in -= 7;
      br->next_in += 7;
    }
  } else if (IS_CONSTANT(n_bits) && n_bits <= 16) {
    if (br->bit_pos_ >= 48) {
      br->val_ >>= 48;
      br->bit_pos_ ^= 48;  /* here same as -= 48 because of the if condition */
      br->val_ |= (*(const uint64_t*)(br->next_in)) << 16;
      br->avail_in -= 6;
      br->next_in += 6;
    }
  } else {
    if (br->bit_pos_ >= 32) {
      br->val_ >>= 32;
      br->bit_pos_ ^= 32;  /* here same as -= 32 because of the if condition */
      br->val_ |= ((uint64_t)(*(const uint32_t*)(br->next_in))) << 32;
      br->avail_in -= 4;
      br->next_in += 4;
    }
  }
#elif (BROTLI_LITTLE_ENDIAN)
  if (IS_CONSTANT(n_bits) && n_bits <= 8) {
    if (br->bit_pos_ >= 24) {
      br->val_ >>= 24;
      br->bit_pos_ ^= 24;  /* here same as -= 24 because of the if condition */
      br->val_ |= (*(const uint32_t*)(br->next_in)) << 8;
      br->avail_in -= 3;
      br->next_in += 3;
    }
  } else {
    if (br->bit_pos_ >= 16) {
      br->val_ >>= 16;
      br->bit_pos_ ^= 16;  /* here same as -= 16 because of the if condition */
      br->val_ |= ((uint32_t)(*(const uint16_t*)(br->next_in))) << 16;
      br->avail_in -= 2;
      br->next_in += 2;
    }
    if (!IS_CONSTANT(n_bits) || (n_bits > 16)) {
      if (br->bit_pos_ >= 8) {
        br->val_ >>= 8;
        br->bit_pos_ ^= 8;  /* here same as -= 8 because of the if condition */
        br->val_ |= ((uint32_t)*br->next_in) << 24;
        --br->avail_in;
        ++br->next_in;
      }
    }
  }
#else
  while (br->bit_pos_ >= 8) {
    br->val_ >>= 8;
    br->val_ |= ((uint32_t)*br->next_in) << 24;
    br->bit_pos_ -= 8;
    --br->avail_in;
    ++br->next_in;
  }
#endif
}

/* Like BrotliGetBits, but does not mask the result, it is only guaranteed
that it has minimum n_bits. */
static BROTLI_INLINE uint32_t BrotliGetBitsUnmasked(
    BrotliBitReader* const br, int n_bits) {
  BrotliFillBitWindow(br, n_bits);
  return (uint32_t)(br->val_ >> br->bit_pos_);
}

/* Returns the specified number of bits from br without advancing bit pos. */
static BROTLI_INLINE uint32_t BrotliGetBits(
    BrotliBitReader* const br, int n_bits) {
  BrotliFillBitWindow(br, n_bits);
  return (uint32_t)(br->val_ >> br->bit_pos_) & BitMask(n_bits);
}

/* Advances the bit pos by n_bits. */
static BROTLI_INLINE void BrotliDropBits(
    BrotliBitReader* const br, int n_bits) {
  br->bit_pos_ += (uint32_t)n_bits;
}

/* Reads the specified number of bits from br and advances the bit pos. */
static BROTLI_INLINE uint32_t BrotliReadBits(
    BrotliBitReader* const br, int n_bits) {
  uint32_t val;
  BrotliFillBitWindow(br, n_bits);
  val = (uint32_t)(br->val_ >> br->bit_pos_) & BitMask(n_bits);
#ifdef BROTLI_DECODE_DEBUG
  printf("[BrotliReadBits]  %d %d %d val: %6x\n",
         (int)br->avail_in, (int)br->bit_pos_, n_bits, val);
#endif
  br->bit_pos_ += (uint32_t)n_bits;
  return val;
}

/* Advances the bit reader position to the next byte boundary and verifies
   that any skipped bits are set to zero. */
static BROTLI_INLINE int BrotliJumpToByteBoundary(BrotliBitReader* br) {
  uint32_t new_bit_pos = (br->bit_pos_ + 7) & (uint32_t)(~7UL);
  uint32_t pad_bits = BrotliReadBits(br, (int)(new_bit_pos - br->bit_pos_));
  return pad_bits == 0;
}

/* Copies remaining input bytes stored in the bit reader to the output. Value
   num may not be larger than BrotliGetRemainingBytes. The bit reader must be
   warmed up again after this. */
static BROTLI_INLINE void BrotliCopyBytes(uint8_t* dest,
                                          BrotliBitReader* br, size_t num) {
  while (br->bit_pos_ + 8 <= (BROTLI_64_BITS_LITTLE_ENDIAN ? 64 : 32)
      && num > 0) {
    *dest = (uint8_t)(br->val_ >> br->bit_pos_);
    br->bit_pos_ += 8;
    ++dest;
    --num;
  }
  memcpy(dest, br->next_in, num);
  br->avail_in -= (uint32_t)num;
  br->next_in += num;
  br->bit_pos_ = 0;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif

#endif  /* BROTLI_DEC_BIT_READER_H_ */
