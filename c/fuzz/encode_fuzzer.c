// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <brotli/encode.h>

#define kBufferSize 1024

// Entry point for LibFuzzer.
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 6) {
    return 0;
  }
  size_t addend = data[0] & 7;
  BrotliSharedDictionaryType dict_type = (data[0] & 8) ? BROTLI_SHARED_DICTIONARY_SERIALIZED : BROTLI_SHARED_DICTIONARY_RAW;
  int dict_quality = data[0] >> 4;
  data++;
  size--;
  uint32_t enc_mode = data[0] & 3;
  uint32_t enc_quality = (data[0] >> 2) & 0xF;
  uint32_t enc_disable = (data[0] & 0x40) ? 1 : 0;
  uint32_t enc_large = (data[0] & 0x80) ? 1 : 0;
  data++;
  size--;
  uint32_t enc_lgwin = BROTLI_MIN_WINDOW_BITS + (data[0] & 0xF);
  uint32_t enc_lgblock = BROTLI_MIN_INPUT_BLOCK_BITS + (data[0] >> 4);
  data++;
  size--;
  uint32_t enc_npostfix = (data[0] & 0x7);
  uint32_t enc_ndirect = (data[0] >> 4) << enc_npostfix;
  data++;
  size--;
  uint16_t dict_size = (uint16_t)((((uint16_t)data[0]) << 8)) | ((uint16_t) data[1]);
  const uint8_t* dict_data = data;
  size-=2;
  data+=2;
  if (size < dict_size) {
    dict_size = 0;
  }
  size-=dict_size;
  data+=dict_size;

  const uint8_t* next_in = data;
  if (addend == 0)
    addend = size;

  uint8_t buffer[kBufferSize];
  /* The biggest "magic number" in brotli is 16MiB - 16, so no need to check
     the cases with much longer output. */
  const size_t total_out_limit = (addend == 0) ? (1 << 26) : (1 << 24);
  size_t total_out = 0;
  BrotliEncoderPreparedDictionary *dict = NULL;
  if (dict_size > 0) {
    dict = BrotliEncoderPrepareDictionary(dict_type, dict_size, dict_data, dict_quality, NULL, NULL, NULL);
  }
  BrotliEncoderState* state = BrotliEncoderCreateInstance(0, 0, 0);
  if (dict) {
    BrotliEncoderAttachPreparedDictionary(state, dict);
  }
  BrotliEncoderSetParameter(state, BROTLI_PARAM_MODE, enc_mode);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, enc_quality);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, enc_lgwin);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_LGBLOCK, enc_lgblock);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING, enc_disable);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_LARGE_WINDOW, enc_large);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_NPOSTFIX, enc_npostfix);
  BrotliEncoderSetParameter(state, BROTLI_PARAM_NDIRECT, enc_ndirect);

  /* Test both fast (addend == size) and slow (addend <= 7) decoding paths. */
  for (size_t i = 0; i < size;) {
    size_t next_i = i + addend;
    if (next_i > size)
      next_i = size;
    size_t avail_in = next_i - i;
    i = next_i;
    size_t avail_out = kBufferSize;
    uint8_t* next_out = buffer;
    while (avail_out > 0 && avail_in > 0) {
        if (!BrotliEncoderCompressStream(state, (next_i == size) ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS,
                                     &avail_in, &next_in, &avail_out, &next_out, &total_out)) {
            break;
        }
        if (avail_out == 0) {
            avail_out = kBufferSize;
            next_out = buffer;
        }
    }
  }
  // TODO check round-trip compression

  if (dict) {
    BrotliEncoderDestroyPreparedDictionary(dict);
  }
  BrotliEncoderDestroyInstance(state);
  return 0;
}
