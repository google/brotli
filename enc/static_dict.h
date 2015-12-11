/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Class to model the static dictionary.

#ifndef BROTLI_ENC_STATIC_DICT_H_
#define BROTLI_ENC_STATIC_DICT_H_

#include "./types.h"

namespace brotli {

static const int kMaxDictionaryMatchLen = 37;
static const int kInvalidMatch = 0xfffffff;

// Matches data against static dictionary words, and for each length l,
// for which a match is found, updates matches[l] to be the minimum possible
//   (distance << 5) + len_code.
// Prerequisites:
//   matches array is at least kMaxDictionaryMatchLen + 1 long
//   all elements are initialized to kInvalidMatch
bool FindAllStaticDictionaryMatches(const uint8_t* data,
                                    int min_length,
                                    int max_length,
                                    int* matches);

}  // namespace brotli

#endif  // BROTLI_ENC_STATIC_DICT_H_
