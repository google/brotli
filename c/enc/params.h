/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Parameters for the Brotli encoder with chosen quality levels. */

#ifndef BROTLI_ENC_PARAMS_H_
#define BROTLI_ENC_PARAMS_H_

#include <brotli/encode.h>

typedef struct BrotliHasherParams {
  int type;
  int bucket_bits;
  int block_bits;
  int hash_len;
  int num_last_distances_to_check;
} BrotliHasherParams;

/* Encoding parameters */
typedef struct BrotliEncoderParams {
  BrotliEncoderMode mode;
  int quality;
  int lgwin;
  int lgblock;
  size_t size_hint;
  BROTLI_BOOL disable_literal_context_modeling;
  BrotliHasherParams hasher;
} BrotliEncoderParams;

#endif  /* BROTLI_ENC_PARAMS_H_ */
