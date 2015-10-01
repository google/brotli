#ifndef BROTLI_ENC_UTF8_UTIL_H_
#define BROTLI_ENC_UTF8_UTIL_H_

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
// Heuristics for deciding about the UTF8-ness of strings.

#include "./types.h"

namespace brotli {

static const double kMinUTF8Ratio = 0.75;

// Returns true if at least min_fraction of the bytes between pos and
// pos + length in the (data, mask) ringbuffer is UTF8-encoded.
bool IsMostlyUTF8(const uint8_t* data, const size_t pos, const size_t mask,
                  const size_t length, const double min_fraction);

}  // namespace brotli

#endif  // BROTLI_ENC_UTF8_UTIL_H_
