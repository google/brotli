/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* API for Brotli compression. */

#ifndef BROTLI_ENC_ENCODE_H_
#define BROTLI_ENC_ENCODE_H_

#include <brotli/port.h>
#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static const int kBrotliMinWindowBits = 10;
static const int kBrotliMaxWindowBits = 24;  /* == BROTLI_MAX_DISTANCE_BITS */
static const int kBrotliMinInputBlockBits = 16;
static const int kBrotliMaxInputBlockBits = 24;

#define BROTLI_MIN_QUALITY 0
#define BROTLI_MAX_QUALITY 11

typedef enum BrotliEncoderMode {
  /* Default compression mode. The compressor does not know anything in
     advance about the properties of the input. */
  BROTLI_MODE_GENERIC = 0,
  /* Compression mode for UTF-8 format text input. */
  BROTLI_MODE_TEXT = 1,
  /* Compression mode used in WOFF 2.0. */
  BROTLI_MODE_FONT = 2
} BrotliEncoderMode;

#define BROTLI_DEFAULT_QUALITY 11
#define BROTLI_DEFAULT_WINDOW 22
#define BROTLI_DEFAULT_MODE BROTLI_MODE_GENERIC

typedef enum BrotliEncoderOperation {
  BROTLI_OPERATION_PROCESS = 0,
  /* Request output stream to flush. Performed when input stream is depleted
     and there is enough space in output stream. */
  BROTLI_OPERATION_FLUSH = 1,
  /* Request output stream to finish. Performed when input stream is depleted
     and there is enough space in output stream. */
  BROTLI_OPERATION_FINISH = 2,
  /* Emits metadata block to stream. Stream is soft-flushed before metadata
     block is emitted. CAUTION: when operation is started, length of the input
     buffer is interpreted as length of a metadata block; changing operation,
     expanding or truncating input before metadata block is completely emitted
     will cause an error; metadata block must not be greater than 16MiB. */
  BROTLI_OPERATION_EMIT_METADATA = 3
} BrotliEncoderOperation;

typedef enum BrotliEncoderParameter {
  BROTLI_PARAM_MODE = 0,
  /* Controls the compression-speed vs compression-density tradeoffs. The higher
     the quality, the slower the compression. Range is 0 to 11. */
  BROTLI_PARAM_QUALITY = 1,
  /* Base 2 logarithm of the sliding window size. Range is 10 to 24. */
  BROTLI_PARAM_LGWIN = 2,
  /* Base 2 logarithm of the maximum input block size. Range is 16 to 24.
     If set to 0, the value will be set based on the quality. */
  BROTLI_PARAM_LGBLOCK = 3
} BrotliEncoderParameter;

/* A state can not be reused for multiple brotli streams. */
typedef struct BrotliEncoderStateStruct BrotliEncoderState;

/* Sets the specified parameter to the given encoder instance.
   Returns BROTLI_FALSE if parameter is unrecognized, or value is invalid.
   Returns BROTLI_FALSE if value of parameter can not be changed at current
   encoder state (e.g. when encoding is started, window size might be already
   encoded and therefore it is impossible to change it).
   Returns BROTLI_TRUE if value is accepted. CAUTION: invalid values might be
   accepted in case they would not break encoding process. */
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderSetParameter(
    BrotliEncoderState* state, BrotliEncoderParameter p, uint32_t value);

/* Creates the instance of BrotliEncoderState and initializes it.
   |alloc_func| and |free_func| MUST be both zero or both non-zero. In the case
   they are both zero, default memory allocators are used. |opaque| is passed to
   |alloc_func| and |free_func| when they are called. */
BROTLI_ENC_API BrotliEncoderState* BrotliEncoderCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

/* Deinitializes and frees BrotliEncoderState instance. */
BROTLI_ENC_API void BrotliEncoderDestroyInstance(BrotliEncoderState* state);

/* The maximum input size that can be processed at once. */
BROTLI_DEPRECATED BROTLI_ENC_API size_t BrotliEncoderInputBlockSize(
    BrotliEncoderState* state);

/* Copies the given input data to the internal ring buffer of the compressor.
   No processing of the data occurs at this time and this function can be
   called multiple times before calling WriteBrotliData() to process the
   accumulated input. At most input_block_size() bytes of input data can be
   copied to the ring buffer, otherwise the next WriteBrotliData() will fail.
 */
BROTLI_DEPRECATED BROTLI_ENC_API void BrotliEncoderCopyInputToRingBuffer(
    BrotliEncoderState* state, const size_t input_size,
    const uint8_t* input_buffer);

/* Processes the accumulated input data and sets |*out_size| to the length of
   the new output meta-block, or to zero if no new output meta-block has been
   created (in this case the processed input data is buffered internally).
   If |*out_size| is positive, |*output| points to the start of the output
   data. If |is_last| or |force_flush| is BROTLI_TRUE, an output meta-block is
   always created. However, until |is_last| is BROTLI_TRUE encoder may retain up
   to 7 bits of the last byte of output. To force encoder to dump the remaining
   bits use WriteMetadata() to append an empty meta-data block.
   Returns BROTLI_FALSE if the size of the input data is larger than
   input_block_size(). */
BROTLI_DEPRECATED BROTLI_ENC_API BROTLI_BOOL BrotliEncoderWriteData(
    BrotliEncoderState* state, const BROTLI_BOOL is_last,
    const BROTLI_BOOL force_flush, size_t* out_size, uint8_t** output);

/* Fills the new state with a dictionary for LZ77, warming up the ringbuffer,
   e.g. for custom static dictionaries for data formats.
   Not to be confused with the built-in transformable dictionary of Brotli.
   To decode, use BrotliDecoderSetCustomDictionary() of the decoder with the
   same dictionary. */
BROTLI_ENC_API void BrotliEncoderSetCustomDictionary(
    BrotliEncoderState* state, size_t size,
    const uint8_t dict[BROTLI_ARRAY_PARAM(size)]);

/* Returns buffer size that is large enough to contain BrotliEncoderCompress
   output for any input.
   CAUTION: result is not applicable to BrotliEncoderCompressStream output,
   because every "flush" adds extra overhead bytes, and some encoder settings
   (e.g. quality 0 and 1) might imply a "soft flush" after every chunk of input.
   Returns 0 if result does not fit size_t. */
BROTLI_ENC_API size_t BrotliEncoderMaxCompressedSize(size_t input_size);

/* Compresses the data in |input_buffer| into |encoded_buffer|, and sets
   |*encoded_size| to the compressed length.
   BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW and BROTLI_DEFAULT_MODE should
   be used as |quality|, |lgwin| and |mode| if there are no specific
   requirements to encoder speed and compression ratio.
   If compression fails, |*encoded_size| is set to 0.
   If BrotliEncoderMaxCompressedSize(|input_size|) is not zero, then
   |*encoded_size| is never set to the bigger value.
   Returns BROTLI_FALSE if there was an error and BROTLI_TRUE otherwise. */
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderCompress(
    int quality, int lgwin, BrotliEncoderMode mode, size_t input_size,
    const uint8_t input_buffer[BROTLI_ARRAY_PARAM(input_size)],
    size_t* encoded_size,
    uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(*encoded_size)]);

/* Progressively compress input stream and push produced bytes to output stream.
   Internally workflow consists of 3 tasks:
    * (optional) copy input data to internal buffer
    * actually compress data and (optionally) store it to internal buffer
    * (optional) copy compressed bytes from internal buffer to output stream
   Whenever all 3 tasks can't move forward anymore, or error occurs, this
   method returns.

   |available_in| and |next_in| represent input stream; when X bytes of input
   are consumed, X is subtracted from |available_in| and added to |next_in|.
   |available_out| and |next_out| represent output stream; when Y bytes are
   pushed to output, Y is subtracted from |available_out| and added to
   |next_out|. |total_out|, if it is not a null-pointer, is assigned to the
   total amount of bytes pushed by the instance of encoder to output.

   |op| is used to perform flush or finish the stream.

   Flushing the stream means forcing encoding of all input passed to encoder and
   completing the current output block, so it could be fully decoded by stream
   decoder. To perform flush |op| must be set to BROTLI_OPERATION_FLUSH. Under
   some circumstances (e.g. lack of output stream capacity) this operation would
   require several calls to BrotliEncoderCompressStream. The method must be
   called again until both input stream is depleted and encoder has no more
   output (see BrotliEncoderHasMoreOutput) after the method is called.

   Finishing the stream means encoding of all input passed to encoder and
   adding specific "final" marks, so stream decoder could determine that stream
   is complete. To perform finish |op| must be set to BROTLI_OPERATION_FINISH.
   Under some circumstances (e.g. lack of output stream capacity) this operation
   would require several calls to BrotliEncoderCompressStream. The method must
   be called again until both input stream is depleted and encoder has no more
   output (see BrotliEncoderHasMoreOutput) after the method is called.

   WARNING: when flushing and finishing, |op| should not change until operation
   is complete; input stream should not be refilled as well.

   Returns BROTLI_FALSE if there was an error and BROTLI_TRUE otherwise.
*/
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderCompressStream(
    BrotliEncoderState* s, BrotliEncoderOperation op, size_t* available_in,
    const uint8_t** next_in, size_t* available_out, uint8_t** next_out,
    size_t* total_out);

/* Check if encoder is in "finished" state, i.e. no more input is acceptable and
   no more output will be produced.
   Works only with BrotliEncoderCompressStream workflow.
   Returns BROTLI_TRUE if stream is finished and BROTLI_FALSE otherwise. */
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderIsFinished(BrotliEncoderState* s);

/* Check if encoder has more output bytes in internal buffer.
   Works only with BrotliEncoderCompressStream workflow.
   Returns BROTLI_TRUE if has more output (in internal buffer) and BROTLI_FALSE
   otherwise. */
BROTLI_ENC_API BROTLI_BOOL BrotliEncoderHasMoreOutput(BrotliEncoderState* s);

/* Returns pointer to internal output buffer.
   Set |size| to zero, to request all the continous output produced by encoder
   so far. Otherwise no more than |size| bytes output will be provided.
   After return |size| contains the size of output buffer region available for
   reading. |size| bytes of output are considered consumed for all consecutive
   calls to the instance methods; returned pointer becomes invalidated as well.

   This method is created to make language bindings easier and more efficient:
     1. push input data to CompressStream, until HasMoreOutput becomes true
     2. use TakeOutput to peek bytes and copy to language-specific entity
   Also this could be useful if there is an output stream that is able to
   consume all the provided data (e.g. when data is saved to file system).
 */
BROTLI_ENC_API const uint8_t* BrotliEncoderTakeOutput(
    BrotliEncoderState* s, size_t* size);


/* Encoder version. Look at BROTLI_VERSION for more information. */
BROTLI_ENC_API uint32_t BrotliEncoderVersion(void);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_ENCODE_H_ */
