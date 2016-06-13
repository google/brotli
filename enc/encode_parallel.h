/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* API for parallel Brotli compression
   Note that this is only a proof of concept currently and not part of the
   final API yet. */

#ifndef BROTLI_ENC_ENCODE_PARALLEL_H_
#define BROTLI_ENC_ENCODE_PARALLEL_H_

#include "../common/types.h"
#include "./compressor.h"

namespace brotli {

int BrotliCompressBufferParallel(BrotliParams params,
                                 size_t input_size,
                                 const uint8_t* input_buffer,
                                 size_t* encoded_size,
                                 uint8_t* encoded_buffer);

}  /* namespace brotli */

#endif  /* BROTLI_ENC_ENCODE_PARALLEL_H_ */
