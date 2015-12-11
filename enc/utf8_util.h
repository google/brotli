/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Heuristics for deciding about the UTF8-ness of strings.

#ifndef BROTLI_ENC_UTF8_UTIL_H_
#define BROTLI_ENC_UTF8_UTIL_H_

#include "./types.h"

namespace brotli {

static const double kMinUTF8Ratio = 0.75;

// Returns true if at least min_fraction of the bytes between pos and
// pos + length in the (data, mask) ringbuffer is UTF8-encoded.
bool IsMostlyUTF8(const uint8_t* data, const size_t pos, const size_t mask,
                  const size_t length, const double min_fraction);

}  // namespace brotli

#endif  // BROTLI_ENC_UTF8_UTIL_H_
