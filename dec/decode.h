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

#define BROTLI_ERROR_CODES_LIST(BROTLI_ERROR_CODE, SEPARATOR)                  \
  BROTLI_ERROR_CODE(BROTLI_NO_ERROR, 0) SEPARATOR                              \
  /* Same as BrotliResult values */                                            \
  BROTLI_ERROR_CODE(BROTLI_SUCCESS, 1) SEPARATOR                               \
  BROTLI_ERROR_CODE(BROTLI_NEEDS_MORE_INPUT, 2) SEPARATOR                      \
  BROTLI_ERROR_CODE(BROTLI_NEEDS_MORE_OUTPUT, 3) SEPARATOR                     \
                                                                               \
  /* Errors caused by invalid input */                                         \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_EXUBERANT_NIBBLE, -1) SEPARATOR        \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_RESERVED, -2) SEPARATOR                \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_EXUBERANT_META_NIBBLE, -3) SEPARATOR   \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_SIMPLE_HUFFMAN_ALPHABET, -4) SEPARATOR \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_SIMPLE_HUFFMAN_SAME, -5) SEPARATOR     \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_CL_SPACE, -6) SEPARATOR                \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_HUFFMAN_SPACE, -7) SEPARATOR           \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_CONTEXT_MAP_REPEAT, -8) SEPARATOR      \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_BLOCK_LENGTH_1, -9) SEPARATOR          \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_BLOCK_LENGTH_2, -10) SEPARATOR         \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_TRANSFORM, -11) SEPARATOR              \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_DICTIONARY, -12) SEPARATOR             \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_WINDOW_BITS, -13) SEPARATOR            \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_PADDING_1, -14) SEPARATOR              \
  BROTLI_ERROR_CODE(BROTLI_ERROR_FORMAT_PADDING_2, -15) SEPARATOR              \
                                                                               \
  /* -16..-20 codes are reserved */                                            \
                                                                               \
  /* Memory allocation problems */                                             \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_CONTEXT_MODES, -21) SEPARATOR           \
  /* Literal, insert and distance trees together */                            \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_TREE_GROUPS, -22) SEPARATOR             \
  /* -23..-24 codes are reserved for distinct tree groups */                   \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_CONTEXT_MAP, -25) SEPARATOR             \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_RING_BUFFER_1, -26) SEPARATOR           \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_RING_BUFFER_2, -27) SEPARATOR           \
  /* -28..-29 codes are reserved for dynamic ringbuffer allocation */          \
  BROTLI_ERROR_CODE(BROTLI_ERROR_ALLOC_BLOCK_TYPE_TREES, -30) SEPARATOR        \
                                                                               \
  /* "Impossible" states */                                                    \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_1, -31) SEPARATOR                 \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_2, -32) SEPARATOR                 \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_3, -33) SEPARATOR                 \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_4, -34) SEPARATOR                 \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_5, -35) SEPARATOR                 \
  BROTLI_ERROR_CODE(BROTLI_ERROR_UNREACHABLE_6, -36)

typedef enum {
#define _BROTLI_COMMA /**/,
#define _BROTLI_ERROR_CODE_ENUM_ITEM(NAME, CODE) NAME = CODE
  BROTLI_ERROR_CODES_LIST(_BROTLI_ERROR_CODE_ENUM_ITEM, _BROTLI_COMMA)
#undef _BROTLI_ERROR_CODE_ENUM_ITEM
#undef _BROTLI_COMMA
} BrotliErrorCode;

#define BROTLI_LAST_ERROR_CODE BROTLI_ERROR_UNREACHABLE_6

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
   amount. |total_out|, if it is not a null-pointer, will be set to the number
   of bytes decompressed since the last state initialization.

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
   |size| should be less or equal to 2^24 (16MiB), otherwise the dictionary will
   be ignored. The dictionary must exist in memory until decoding is done and
   is owned by the caller. To use:
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

/* Returns detailed error code after BrotliDecompressStream returns
   BROTLI_RESULT_ERROR. */
BrotliErrorCode BrotliGetErrorCode(const BrotliState* s);

static const char* BrotliErrorString(BrotliErrorCode c) {
  switch (c) {
#define _BROTLI_ERROR_CODE_CASE(NAME, CODE) case NAME: return #NAME;
#define _BROTLI_NOTHING
    BROTLI_ERROR_CODES_LIST(_BROTLI_ERROR_CODE_CASE, _BROTLI_NOTHING)
#undef _BROTLI_ERROR_CODE_CASE
#undef _BROTLI_NOTHING
    default: return "INVALID BrotliErrorCode";
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif

#endif  /* BROTLI_DEC_DECODE_H_ */
