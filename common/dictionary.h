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

BROTLI_COMMON_API extern const uint8_t kBrotliDictionary[122784];
BROTLI_COMMON_API extern const uint32_t kBrotliDictionaryOffsetsByLength[25];
BROTLI_COMMON_API extern const uint8_t kBrotliDictionarySizeBitsByLength[25];

#define BROTLI_MIN_DICTIONARY_WORD_LENGTH 4
#define BROTLI_MAX_DICTIONARY_WORD_LENGTH 24

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_COMMON_DICTIONARY_H_ */
