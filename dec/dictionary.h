/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Collection of static dictionary words. */

#ifndef BROTLI_DEC_DICTIONARY_H_
#define BROTLI_DEC_DICTIONARY_H_

#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

extern const uint8_t kBrotliDictionary[122784];

static const uint32_t kBrotliDictionaryOffsetsByLength[] = {
  0, 0, 0, 0, 0, 4096, 9216, 21504, 35840, 44032, 53248, 63488, 74752, 87040,
  93696, 100864, 104704, 106752, 108928, 113536, 115968, 118528, 119872, 121280,
  122016
};

static const uint8_t kBrotliDictionarySizeBitsByLength[] = {
  0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10,
  9, 9, 8, 7, 7,  8,  7,  7,  6,  6,  5,  5,
};

static const int kBrotliMinDictionaryWordLength = 4;
static const int kBrotliMaxDictionaryWordLength = 24;

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_DEC_DICTIONARY_H_ */
