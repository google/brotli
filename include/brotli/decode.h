/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* API for Brotli decompression */

#ifndef BROTLI_DEC_DECODE_H_
#define BROTLI_DEC_DECODE_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct BrotliDecoderStateStruct BrotliDecoderState;

typedef enum {
  /* Decoding error, e.g. corrupt input or memory allocation problem */
  BROTLI_DECODER_RESULT_ERROR = 0,
  /* Decoding successfully completed */
  BROTLI_DECODER_RESULT_SUCCESS = 1,
  /* Partially done; should be called again with more input */
  BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT = 2,
  /* Partially done; should be called again with more output */
  BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT = 3
} BrotliDecoderResult;

#define BROTLI_DECODER_ERROR_CODES_LIST(BROTLI_ERROR_CODE, SEPARATOR)      \
  BROTLI_ERROR_CODE(_, NO_ERROR, 0) SEPARATOR                              \
  /* Same as BrotliDecoderResult values */                                 \
  BROTLI_ERROR_CODE(_, SUCCESS, 1) SEPARATOR                               \
  BROTLI_ERROR_CODE(_, NEEDS_MORE_INPUT, 2) SEPARATOR                      \
  BROTLI_ERROR_CODE(_, NEEDS_MORE_OUTPUT, 3) SEPARATOR                     \
                                                                           \
  /* Errors caused by invalid input */                                     \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, EXUBERANT_NIBBLE, -1) SEPARATOR        \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, RESERVED, -2) SEPARATOR                \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, EXUBERANT_META_NIBBLE, -3) SEPARATOR   \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, SIMPLE_HUFFMAN_ALPHABET, -4) SEPARATOR \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, SIMPLE_HUFFMAN_SAME, -5) SEPARATOR     \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, CL_SPACE, -6) SEPARATOR                \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, HUFFMAN_SPACE, -7) SEPARATOR           \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, CONTEXT_MAP_REPEAT, -8) SEPARATOR      \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, BLOCK_LENGTH_1, -9) SEPARATOR          \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, BLOCK_LENGTH_2, -10) SEPARATOR         \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, TRANSFORM, -11) SEPARATOR              \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, DICTIONARY, -12) SEPARATOR             \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, WINDOW_BITS, -13) SEPARATOR            \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, PADDING_1, -14) SEPARATOR              \
  BROTLI_ERROR_CODE(_ERROR_FORMAT_, PADDING_2, -15) SEPARATOR              \
                                                                           \
  /* -16..-19 codes are reserved */                                        \
                                                                           \
  BROTLI_ERROR_CODE(_ERROR_, INVALID_ARGUMENTS, -20) SEPARATOR             \
                                                                           \
  /* Memory allocation problems */                                         \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, CONTEXT_MODES, -21) SEPARATOR           \
  /* Literal, insert and distance trees together */                        \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, TREE_GROUPS, -22) SEPARATOR             \
  /* -23..-24 codes are reserved for distinct tree groups */               \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, CONTEXT_MAP, -25) SEPARATOR             \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, RING_BUFFER_1, -26) SEPARATOR           \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, RING_BUFFER_2, -27) SEPARATOR           \
  /* -28..-29 codes are reserved for dynamic ringbuffer allocation */      \
  BROTLI_ERROR_CODE(_ERROR_ALLOC_, BLOCK_TYPE_TREES, -30) SEPARATOR        \
                                                                           \
  /* "Impossible" states */                                                \
  BROTLI_ERROR_CODE(_ERROR_, UNREACHABLE, -31)

typedef enum {
#define BROTLI_COMMA_ ,
#define BROTLI_ERROR_CODE_ENUM_ITEM_(PREFIX, NAME, CODE) \
    BROTLI_DECODER ## PREFIX ## NAME = CODE
  BROTLI_DECODER_ERROR_CODES_LIST(BROTLI_ERROR_CODE_ENUM_ITEM_, BROTLI_COMMA_)
#undef BROTLI_ERROR_CODE_ENUM_ITEM_
#undef BROTLI_COMMA_
} BrotliDecoderErrorCode;

#define BROTLI_LAST_ERROR_CODE BROTLI_DECODER_ERROR_UNREACHABLE

/* Creates the instance of BrotliDecoderState and initializes it. |alloc_func|
   and |free_func| MUST be both zero or both non-zero. In the case they are both
   zero, default memory allocators are used. |opaque| is passed to |alloc_func|
   and |free_func| when they are called. */
BROTLI_DEC_API BrotliDecoderState* BrotliDecoderCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

/* Deinitializes and frees BrotliDecoderState instance. */
BROTLI_DEC_API void BrotliDecoderDestroyInstance(BrotliDecoderState* state);

/* Decompresses the data in |encoded_buffer| into |decoded_buffer|, and sets
   |*decoded_size| to the decompressed length. */
BROTLI_DEC_API BrotliDecoderResult BrotliDecoderDecompress(
    size_t encoded_size,
    const uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(encoded_size)],
    size_t* decoded_size,
    uint8_t decoded_buffer[BROTLI_ARRAY_PARAM(*decoded_size)]);

/* Decompresses the data. Supports partial input and output.

   Must be called with an allocated input buffer in |*next_in| and an allocated
   output buffer in |*next_out|. The values |*available_in| and |*available_out|
   must specify the allocated size in |*next_in| and |*next_out| respectively;
   when |*available_out| is 0, |next_out| is allowed to be NULL.

   After each call, |*available_in| will be decremented by the amount of input
   bytes consumed, and the |*next_in| pointer will be incremented by that
   amount. Similarly, |*available_out| will be decremented by the amount of
   output bytes written, and the |*next_out| pointer will be incremented by that
   amount. |total_out|, if it is not a null-pointer, will be set to the number
   of bytes decompressed since the last state initialization.

   Input is never overconsumed, so |next_in| and |available_in| could be passed
   to the next consumer after decoding is complete. */
BROTLI_DEC_API BrotliDecoderResult BrotliDecoderDecompressStream(
  BrotliDecoderState* s, size_t* available_in, const uint8_t** next_in,
  size_t* available_out, uint8_t** next_out, size_t* total_out);

/* Fills the new state with a dictionary for LZ77, warming up the ringbuffer,
   e.g. for custom static dictionaries for data formats.
   Not to be confused with the built-in transformable dictionary of Brotli.
   |size| should be less or equal to 2^24 (16MiB), otherwise the dictionary will
   be ignored. The dictionary must exist in memory until decoding is done and
   is owned by the caller. To use:
    1) Allocate and initialize state with BrotliCreateInstance
    2) Use BrotliDecoderSetCustomDictionary
    3) Use BrotliDecoderDecompressStream
    4) Clean up and free state with BrotliDestroyState
*/
BROTLI_DEC_API void BrotliDecoderSetCustomDictionary(
    BrotliDecoderState* s, size_t size,
    const uint8_t dict[BROTLI_ARRAY_PARAM(size)]);

/* Returns BROTLI_TRUE, if decoder has some unconsumed output.
   Otherwise returns BROTLI_FALSE. */
BROTLI_DEC_API BROTLI_BOOL BrotliDecoderHasMoreOutput(
    const BrotliDecoderState* s);

/* Returns pointer to internal output buffer.
   Set |size| to zero, to request all the continous output produced by decoder
   so far. Otherwise no more than |size| bytes output will be provided.
   After return |size| contains the size of output buffer region available for
   reading. |size| bytes of output are considered consumed for all consecutive
   calls to the instance methods; returned pointer becomes invalidated as well.

   This method is created to make language bindings easier and more efficient:
     1. push data to DecompressStream, until "needs more output" is reported
     2. use TakeOutput to peek bytes and copy to language-specific entity
   Also this could be useful if there is an output stream that is able to
   consume all the provided data (e.g. when data is saved to file system).
 */
BROTLI_DEC_API const uint8_t* BrotliDecoderTakeOutput(
    BrotliDecoderState* s, size_t* size);

/* Returns BROTLI_TRUE, if decoder has already received some input bytes.
   Otherwise returns BROTLI_FALSE. */
BROTLI_DEC_API BROTLI_BOOL BrotliDecoderIsUsed(const BrotliDecoderState* s);

/* Returns BROTLI_TRUE, if decoder is in a state where we reached the end of the
   input and produced all of the output; returns BROTLI_FALSE otherwise. */
BROTLI_BOOL BrotliDecoderIsFinished(const BrotliDecoderState* s);

/* Returns detailed error code after BrotliDecoderDecompressStream returns
   BROTLI_DECODER_RESULT_ERROR. */
BrotliDecoderErrorCode BrotliDecoderGetErrorCode(const BrotliDecoderState* s);

BROTLI_DEC_API const char* BrotliDecoderErrorString(BrotliDecoderErrorCode c);

/* Decoder version. Look at BROTLI_VERSION for more information. */
BROTLI_DEC_API uint32_t BrotliDecoderVersion(void);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif

#endif  /* BROTLI_DEC_DECODE_H_ */
