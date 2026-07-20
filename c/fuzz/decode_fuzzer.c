// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <brotli/decode.h>

#define kBufferSize 1024

// Entry point for LibFuzzer.
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 3) {
    return 0;
  }
  size_t addend = data[0] & 7;
  uint32_t decode_large = (data[0] & 8) ? 1 : 0;
  BrotliSharedDictionaryType dict_type = (data[0] & 0x10) ? BROTLI_SHARED_DICTIONARY_SERIALIZED : BROTLI_SHARED_DICTIONARY_RAW;
  size--;
  data++;
  uint16_t dict_size = (uint16_t)((((uint16_t)data[0]) << 8)) | ((uint16_t) data[1]);
  const uint8_t* dict = data;
  size-=2;
  data+=2;
  if (size < dict_size) {
    dict_size = 0;
  }
  size-=dict_size;
  data+=dict_size;

  if (addend == 0)
    addend = size;

  const uint8_t* next_in = data;

  uint8_t buffer[kBufferSize];
  /* The biggest "magic number" in brotli is 16MiB - 16, so no need to check
     the cases with much longer output. */
  const size_t total_out_limit = (addend == 0) ? (1 << 26) : (1 << 24);
  size_t total_out = 0;

  BrotliDecoderState* state = BrotliDecoderCreateInstance(0, 0, 0);
  if (!state) {
    // OOM is out-of-scope here.
    return 0;
  }

  BrotliDecoderSetParameter(state, BROTLI_DECODER_PARAM_LARGE_WINDOW, decode_large);
  if (dict_size > 0) {
    BrotliDecoderAttachDictionary(state, BROTLI_SHARED_DICTIONARY_RAW,
                                      dict_size, dict);
  }
  /* Test both fast (addend == size) and slow (addend <= 7) decoding paths. */
  for (size_t i = 0; i < size;) {
    size_t next_i = i + addend;
    if (next_i > size)
      next_i = size;
    size_t avail_in = next_i - i;
    i = next_i;
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
      size_t avail_out = kBufferSize;
      uint8_t* next_out = buffer;
      result = BrotliDecoderDecompressStream(
          state, &avail_in, &next_in, &avail_out, &next_out, &total_out);
      if (total_out > total_out_limit)
        break;
    }
    if (total_out > total_out_limit)
      break;
    if (result != BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
      break;
  }

  BrotliDecoderDestroyInstance(state);
  return 0;
}
