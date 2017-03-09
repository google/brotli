/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Collection of static dictionary words. */

#ifndef BROTLI_COMMON_DICTIONARY_H_
#define BROTLI_COMMON_DICTIONARY_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct BrotliDictionary {
  /**
   * Number of bits to encode index of dictionary word in a bucket.
   *
   * Specification: Appendix A. Static Dictionary Data
   *
   * Words in a dictionary are bucketed by length.
   * @c 0 means that there are no words of a given length.
   * Dictionary consists of words with length of [4..24] bytes.
   * Values at [0..3] and [25..31] indices should not be addressed.
   */
  uint8_t size_bits_by_length[32];

  /* assert(offset[i + 1] == offset[i] + (bits[i] ? (i << bits[i]) : 0)) */
  uint32_t offsets_by_length[32];

  /* Data array is not bound, and should obey to size_bits_by_length values.
     Specified size matches default (RFC 7932) dictionary. */
  /* assert(sizeof(data) == offsets_by_length[31]) */
  uint8_t data[122784];
} BrotliDictionary;

BROTLI_COMMON_API extern const BrotliDictionary* BrotliGetDictionary(void);

#define BROTLI_MIN_DICTIONARY_WORD_LENGTH 4
#define BROTLI_MAX_DICTIONARY_WORD_LENGTH 24

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_COMMON_DICTIONARY_H_ */
