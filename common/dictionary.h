/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Collection of static dictionary words. */

#ifndef BROTLI_COMMON_DICTIONARY_H_
#define BROTLI_COMMON_DICTIONARY_H_

#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

extern const uint8_t kBrotliDictionary[122784];
extern const uint32_t kBrotliDictionaryOffsetsByLength[25];
extern const uint8_t kBrotliDictionarySizeBitsByLength[25];

#define kBrotliMinDictionaryWordLength 4
#define kBrotliMaxDictionaryWordLength 24

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_COMMON_DICTIONARY_H_ */
