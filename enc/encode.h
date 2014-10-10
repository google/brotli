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

struct BrotliParams {
  enum Mode {
    MODE_TEXT = 0,
    MODE_FONT = 1,
  };
  Mode mode;

  BrotliParams() : mode(MODE_TEXT) {}
};

class BrotliCompressor {
 public:
  explicit BrotliCompressor(BrotliParams params);
  ~BrotliCompressor();

  // Writes the stream header into the internal output buffer.
  void WriteStreamHeader();

  // Encodes the data in input_buffer as a meta-block and writes it to
  // encoded_buffer and sets *encoded_size to the number of bytes that was
  // written.
  void WriteMetaBlock(const size_t input_size,
                      const uint8_t* input_buffer,
                      const bool is_last,
                      size_t* encoded_size,
                      uint8_t* encoded_buffer);

  // Writes a zero-length meta-block with end-of-input bit set to the
  // internal output buffer and copies the output buffer to encoded_buffer and
  // sets *encoded_size to the number of bytes written.
  void FinishStream(size_t* encoded_size, uint8_t* encoded_buffer);


 private:
  // Initializes the hasher with the hashes of dictionary words.
  void StoreDictionaryWordHashes();

  BrotliParams params_;
  int window_bits_;
  std::unique_ptr<Hashers> hashers_;
  Hashers::Type hash_type_;
  int dist_ringbuffer_[4];
  size_t dist_ringbuffer_idx_;
  size_t input_pos_;
  RingBuffer ringbuffer_;
  std::vector<float> literal_cost_;
  int storage_ix_;
  uint8_t* storage_;
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
