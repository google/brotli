// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// API for Brotli compression

#ifndef BROTLI_ENC_ENCODE_H_
#define BROTLI_ENC_ENCODE_H_

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "./hash.h"
#include "./ringbuffer.h"
#include "./static_dict.h"

namespace brotli {

static const int kMaxWindowBits = 24;
static const int kMinWindowBits = 16;
static const int kMinInputBlockBits = 16;
static const int kMaxInputBlockBits = 24;

struct BrotliParams {
  BrotliParams()
      : mode(MODE_TEXT),
        quality(11),
        lgwin(22),
        lgblock(0),
        enable_transforms(false),
        greedy_block_split(false) {}

  enum Mode {
    MODE_TEXT = 0,
    MODE_FONT = 1,
  };
  Mode mode;

  // Controls the compression-speed vs compression-density tradeoffs. The higher
  // the quality, the slower the compression. Range is 0 to 11.
  int quality;
  // Base 2 logarithm of the sliding window size. Range is 16 to 24.
  int lgwin;
  // Base 2 logarithm of the maximum input block size. Range is 16 to 24.
  // If set to 0, the value will be set based on the quality.
  int lgblock;

  bool enable_transforms;
  bool greedy_block_split;
};

class BrotliCompressor {
 public:
  explicit BrotliCompressor(BrotliParams params);
  ~BrotliCompressor();

  // The maximum input size that can be processed at once.
  size_t input_block_size() const { return 1 << params_.lgblock; }

  // Encodes the data in input_buffer as a meta-block and writes it to
  // encoded_buffer (*encoded_size should be set to the size of
  // encoded_buffer) and sets *encoded_size to the number of bytes that
  // was written. Returns 0 if there was an error and 1 otherwise.
  bool WriteMetaBlock(const size_t input_size,
                      const uint8_t* input_buffer,
                      const bool is_last,
                      size_t* encoded_size,
                      uint8_t* encoded_buffer);

  // Writes a zero-length meta-block with end-of-input bit set to the
  // internal output buffer and copies the output buffer to encoded_buffer
  // (*encoded_size should be set to the size of encoded_buffer) and sets
  // *encoded_size to the number of bytes written. Returns false if there was
  // an error and true otherwise.
  bool FinishStream(size_t* encoded_size, uint8_t* encoded_buffer);

  // No-op, but we keep it here for API backward-compatibility.
  void WriteStreamHeader() {}

 private:
  // Initializes the hasher with the hashes of dictionary words.
  void StoreDictionaryWordHashes(bool enable_transforms);

  uint8_t* GetBrotliStorage(size_t size);

  BrotliParams params_;
  int max_backward_distance_;
  std::unique_ptr<Hashers> hashers_;
  int hash_type_;
  size_t input_pos_;
  std::unique_ptr<RingBuffer> ringbuffer_;
  std::vector<float> literal_cost_;
  int dist_cache_[4];
  uint8_t last_byte_;
  uint8_t last_byte_bits_;
  int storage_size_;
  std::unique_ptr<uint8_t[]> storage_;
  static StaticDictionary *static_dictionary_;
};

// Compresses the data in input_buffer into encoded_buffer, and sets
// *encoded_size to the compressed length.
// Returns 0 if there was an error and 1 otherwise.
int BrotliCompressBuffer(BrotliParams params,
                         size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer);

}  // namespace brotli

#endif  // BROTLI_ENC_ENCODE_H_
