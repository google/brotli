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
// Class to model the static dictionary.

#ifndef BROTLI_ENC_STATIC_DICT_H_
#define BROTLI_ENC_STATIC_DICT_H_

#include <stdint.h>

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
