/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Literal cost model to allow backward reference replacement to be efficient.

#ifndef BROTLI_ENC_LITERAL_COST_H_
#define BROTLI_ENC_LITERAL_COST_H_

#include "./types.h"

namespace brotli {

// Estimates how many bits the literals in the interval [pos, pos + len) in the
// ringbuffer (data, mask) will take entropy coded and writes these estimates
// to the cost[0..len) array.
void EstimateBitCostsForLiterals(size_t pos, size_t len, size_t mask,
                                 const uint8_t *data, float *cost);

}  // namespace brotli

#endif  // BROTLI_ENC_LITERAL_COST_H_
