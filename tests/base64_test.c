/* Copyright 2026 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t BuildInput(uint8_t** output) {
  static const char kTrigger[] = ";base64,";
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t num_chunks = 1500;
  const size_t chunk_size = 700;
  const size_t base64_run = 512;
  const size_t trigger_len = sizeof(kTrigger) - 1;
  const size_t total = trigger_len + base64_run + num_chunks * chunk_size;
  uint8_t* buffer = (uint8_t*)malloc(total);
  size_t position = 0;
  uint32_t random = 12345;
  size_t i;
  size_t chunk;

  if (buffer == NULL) return 0;
  memcpy(buffer + position, kTrigger, trigger_len);
  position += trigger_len;
  for (i = 0; i < base64_run; ++i) {
    random = random * 1103515245u + 12345u;
    buffer[position++] = (uint8_t)kAlphabet[(random >> 16) % 64];
  }

  for (chunk = 0; chunk < num_chunks; ++chunk) {
    int bias = (int)(chunk % 8) * 8;
    for (i = 0; i < chunk_size; ++i) {
      int low6;
      random = random * 1103515245u + 12345u;
      low6 = bias + (int)((random >> 16) % 8);
      if ((i & 1) == 0) {
        buffer[position++] = (uint8_t)(0xC0 | (low6 & 0x3F));
      } else {
        buffer[position++] = (uint8_t)(0x80 | (low6 & 0x3F));
      }
    }
  }

  *output = buffer;
  return position;
}

static BROTLI_BOOL Compress(const uint8_t* input, size_t input_size,
                            uint8_t** output, size_t* output_size) {
  size_t output_capacity = input_size + input_size / 2 + 1024;
  BrotliEncoderState* state =
      BrotliEncoderCreateInstance(NULL, NULL, NULL);
  const uint8_t* next_input = input;
  size_t available_input = input_size;
  uint8_t* next_output;
  size_t available_output;
  BROTLI_BOOL result;

  *output = (uint8_t*)malloc(output_capacity);
  if (state == NULL || *output == NULL) {
    BrotliEncoderDestroyInstance(state);
    return BROTLI_FALSE;
  }
  next_output = *output;
  available_output = output_capacity;
  if (!BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, 9) ||
      !BrotliEncoderSetParameter(state, BROTLI_PARAM_BASE64_MODE, 1)) {
    BrotliEncoderDestroyInstance(state);
    return BROTLI_FALSE;
  }

  result = BrotliEncoderCompressStream(
      state, BROTLI_OPERATION_FINISH, &available_input, &next_input,
      &available_output, &next_output, NULL);
  result = TO_BROTLI_BOOL(result && available_input == 0 &&
                          BrotliEncoderIsFinished(state));
  *output_size = output_capacity - available_output;
  BrotliEncoderDestroyInstance(state);
  return result;
}

int main(void) {
  uint8_t* input = NULL;
  uint8_t* compressed = NULL;
  uint8_t* decoded = NULL;
  size_t input_size = BuildInput(&input);
  size_t compressed_size = 0;
  size_t decoded_size = input_size;
  BROTLI_BOOL ok = BROTLI_FALSE;

  if (input_size == 0 ||
      !Compress(input, input_size, &compressed, &compressed_size)) {
    goto cleanup;
  }
  decoded = (uint8_t*)malloc(input_size);
  if (decoded == NULL) goto cleanup;
  if (BrotliDecoderDecompress(compressed_size, compressed, &decoded_size,
                              decoded) != BROTLI_DECODER_RESULT_SUCCESS) {
    goto cleanup;
  }
  ok = TO_BROTLI_BOOL(decoded_size == input_size &&
                      memcmp(input, decoded, input_size) == 0);

cleanup:
  free(input);
  free(compressed);
  free(decoded);
  return ok ? 0 : 1;
}
