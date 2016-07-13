/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* C++ API for Brotli compression. */

#ifndef BROTLI_COMPRESSOR_H_
#define BROTLI_COMPRESSOR_H_

#include <brotli/encode.h>
#include <brotli/streams.h>

namespace brotli {

static const int kMinWindowBits = kBrotliMinWindowBits;
static const int kMaxWindowBits = kBrotliMaxWindowBits;
static const int kMinInputBlockBits = kBrotliMinInputBlockBits;
static const int kMaxInputBlockBits = kBrotliMaxInputBlockBits;

struct BrotliParams {
  BrotliParams(void)
      : mode(MODE_GENERIC),
        quality(11),
        lgwin(22),
        lgblock(0),
        enable_dictionary(true),
        enable_transforms(false),
        greedy_block_split(false),
        enable_context_modeling(true) {}

  enum Mode {
    /* Default compression mode. The compressor does not know anything in
       advance about the properties of the input. */
    MODE_GENERIC = 0,
    /* Compression mode for UTF-8 format text input. */
    MODE_TEXT = 1,
    /* Compression mode used in WOFF 2.0. */
    MODE_FONT = 2
  };
  Mode mode;

  /* Controls the compression-speed vs compression-density tradeoffs. The higher
     the |quality|, the slower the compression. Range is 0 to 11. */
  int quality;
  /* Base 2 logarithm of the sliding window size. Range is 10 to 24. */
  int lgwin;
  /* Base 2 logarithm of the maximum input block size. Range is 16 to 24.
     If set to 0, the value will be set based on the quality. */
  int lgblock;

  /* These settings are deprecated and will be ignored.
     All speed vs. size compromises are controlled by the |quality| param. */
  bool enable_dictionary;
  bool enable_transforms;
  bool greedy_block_split;
  bool enable_context_modeling;
};

/* An instance can not be reused for multiple brotli streams. */
class BrotliCompressor {
 public:
  explicit BrotliCompressor(BrotliParams params);
  ~BrotliCompressor(void);

  /* The maximum input size that can be processed at once. */
  size_t input_block_size(void) const {
    return BrotliEncoderInputBlockSize(state_);
  }

  /* Encodes the data in |input_buffer| as a meta-block and writes it to
     |encoded_buffer| (|*encoded_size should| be set to the size of
     |encoded_buffer|) and sets |*encoded_size| to the number of bytes that
     was written. The |input_size| must not be greater than input_block_size().
     Returns false if there was an error and true otherwise. */
  bool WriteMetaBlock(const size_t input_size,
                      const uint8_t* input_buffer,
                      const bool is_last,
                      size_t* encoded_size,
                      uint8_t* encoded_buffer);

  /* Writes a metadata meta-block containing the given input to encoded_buffer.
     |*encoded_size| should be set to the size of the encoded_buffer.
     Sets |*encoded_size| to the number of bytes that was written.
     Note that the given input data will not be part of the sliding window and
     thus no backward references can be made to this data from subsequent
     metablocks. |input_size| must not be greater than 2^24 and provided
     |*encoded_size| must not be less than |input_size| + 6.
     Returns false if there was an error and true otherwise. */
  bool WriteMetadata(const size_t input_size,
                     const uint8_t* input_buffer,
                     const bool is_last,
                     size_t* encoded_size,
                     uint8_t* encoded_buffer);

  /* Writes a zero-length meta-block with end-of-input bit set to the
     internal output buffer and copies the output buffer to |encoded_buffer|
     (|*encoded_size| should be set to the size of |encoded_buffer|) and sets
     |*encoded_size| to the number of bytes written.
     Returns false if there was an error and true otherwise. */
  bool FinishStream(size_t* encoded_size, uint8_t* encoded_buffer);

  /* Copies the given input data to the internal ring buffer of the compressor.
     No processing of the data occurs at this time and this function can be
     called multiple times before calling WriteBrotliData() to process the
     accumulated input. At most input_block_size() bytes of input data can be
     copied to the ring buffer, otherwise the next WriteBrotliData() will fail.
   */
  void CopyInputToRingBuffer(const size_t input_size,
                             const uint8_t* input_buffer);

  /* Processes the accumulated input data and sets |*out_size| to the length of
     the new output meta-block, or to zero if no new output meta-block has been
     created (in this case the processed input data is buffered internally).
     If |*out_size| is positive, |*output| points to the start of the output
     data. If |is_last| or |force_flush| is true, an output meta-block is always
     created. However, until |is_last| is true encoder may retain up to 7 bits
     of the last byte of output. To force encoder to dump the remaining bits
     use WriteMetadata() to append an empty meta-data block.
     Returns false if the size of the input data is larger than
     input_block_size(). */
  bool WriteBrotliData(const bool is_last, const bool force_flush,
                       size_t* out_size, uint8_t** output);

  /* Fills the new state with a dictionary for LZ77, warming up the ringbuffer,
     e.g. for custom static dictionaries for data formats.
     Not to be confused with the built-in transformable dictionary of Brotli.
     To decode, use BrotliSetCustomDictionary() of the decoder with the same
     dictionary. */
  void BrotliSetCustomDictionary(size_t size, const uint8_t* dict);

  /* No-op, but we keep it here for API backward-compatibility. */
  void WriteStreamHeader(void) {}

 private:
  BrotliEncoderState* state_;
};

/* Compresses the data in |input_buffer| into |encoded_buffer|, and sets
   |*encoded_size| to the compressed length.
   Returns 0 if there was an error and 1 otherwise. */
int BrotliCompressBuffer(BrotliParams params,
                         size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer);

/* Same as above, but uses the specified input and output classes instead
   of reading from and writing to pre-allocated memory buffers. */
int BrotliCompress(BrotliParams params, BrotliIn* in, BrotliOut* out);

/* Before compressing the data, sets a custom LZ77 dictionary with
   BrotliCompressor::BrotliSetCustomDictionary. */
int BrotliCompressWithCustomDictionary(size_t dictsize, const uint8_t* dict,
                                       BrotliParams params,
                                       BrotliIn* in, BrotliOut* out);

}  /* namespace brotli */

#endif  /* BROTLI_COMPRESSOR_H_ */
