/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* API for Brotli decompression */

#ifndef BROTLI_DEC_DECODE_H_
#define BROTLI_DEC_DECODE_H_

#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct BrotliStateStruct BrotliState;

typedef enum {
  /* Decoding error, e.g. corrupt input or memory allocation problem */
  BROTLI_RESULT_ERROR = 0,
  /* Decoding successfully completed */
  BROTLI_RESULT_SUCCESS = 1,
  /* Partially done; should be called again with more input */
  BROTLI_RESULT_NEEDS_MORE_INPUT = 2,
  /* Partially done; should be called again with more output */
  BROTLI_RESULT_NEEDS_MORE_OUTPUT = 3
} BrotliResult;

/* Creates the instance of BrotliState and initializes it. |alloc_func| and
   |free_func| MUST be both zero or both non-zero. In the case they are both
   zero, default memory allocators are used. |opaque| is passed to |alloc_func|
   and |free_func| when they are called. */
BrotliState* BrotliCreateState(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

/* Deinitializes and frees BrotliState instance. */
void BrotliDestroyState(BrotliState* state);

/* Sets |*decoded_size| to the decompressed size of the given encoded stream.
   This function only works if the encoded buffer has a single meta block,
   or if it has two meta-blocks, where the first is uncompressed and the
   second is empty.
   Returns 1 on success, 0 on failure. */
int BrotliDecompressedSize(size_t encoded_size,
                           const uint8_t* encoded_buffer,
                           size_t* decoded_size);

/* Decompresses the data in |encoded_buffer| into |decoded_buffer|, and sets
   |*decoded_size| to the decompressed length. */
BrotliResult BrotliDecompressBuffer(size_t encoded_size,
                                    const uint8_t* encoded_buffer,
                                    size_t* decoded_size,
                                    uint8_t* decoded_buffer);

/* Decompresses the data. Supports partial input and output.

   Must be called with an allocated input buffer in |*next_in| and an allocated
   output buffer in |*next_out|. The values |*available_in| and |*available_out|
   must specify the allocated size in |*next_in| and |*next_out| respectively.

   After each call, |*available_in| will be decremented by the amount of input
   bytes consumed, and the |*next_in| pointer will be incremented by that
   amount. Similarly, |*available_out| will be decremented by the amount of
   output bytes written, and the |*next_out| pointer will be incremented by that
   amount. |total_out| will be set to the number of bytes decompressed since
   last state initialization.

   Input is never overconsumed, so |next_in| and |available_in| could be passed
   to the next consumer after decoding is complete. */
BrotliResult BrotliDecompressStream(size_t* available_in,
                                    const uint8_t** next_in,
                                    size_t* available_out,
                                    uint8_t** next_out,
                                    size_t* total_out,
                                    BrotliState* s);

/* Fills the new state with a dictionary for LZ77, warming up the ringbuffer,
   e.g. for custom static dictionaries for data formats.
   Not to be confused with the built-in transformable dictionary of Brotli.
   The dictionary must exist in memory until decoding is done and is owned by
   the caller. To use:
    1) Allocate and initialize state with BrotliCreateState
    2) Use BrotliSetCustomDictionary
    3) Use BrotliDecompressStream
    4) Clean up and free state with BrotliDestroyState
*/
void BrotliSetCustomDictionary(
    size_t size, const uint8_t* dict, BrotliState* s);

/* Returns 1, if s is in a state where we have not read any input bytes yet,
   and 0 otherwise */
int BrotliStateIsStreamStart(const BrotliState* s);

/* Returns 1, if s is in a state where we reached the end of the input and
   produced all of the output, and 0 otherwise. */
int BrotliStateIsStreamEnd(const BrotliState* s);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif

#endif  /* BROTLI_DEC_DECODE_H_ */
