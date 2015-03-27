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
// Implementation of Brotli compressor.

#include "./encode.h"

#include <algorithm>
#include <limits>

#include "./backward_references.h"
#include "./bit_cost.h"
#include "./block_splitter.h"
#include "./brotli_bit_stream.h"
#include "./cluster.h"
#include "./context.h"
#include "./metablock.h"
#include "./transform.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./histogram.h"
#include "./literal_cost.h"
#include "./prefix.h"
#include "./write_bits.h"

namespace brotli {

static const int kWindowBits = 22;
// To make decoding faster, we allow the decoder to write 16 bytes ahead in
// its ringbuffer, therefore the encoder has to decrease max distance by this
// amount.
static const int kDecoderRingBufferWriteAheadSlack = 16;
static const int kMaxBackwardDistance =
    (1 << kWindowBits) - kDecoderRingBufferWriteAheadSlack;

static const int kMetaBlockSizeBits = 21;
static const int kRingBufferBits = 23;
static const int kRingBufferMask = (1 << kRingBufferBits) - 1;

int ParseAsUTF8(int* symbol, const uint8_t* input, int size) {
  // ASCII
  if ((input[0] & 0x80) == 0) {
    *symbol = input[0];
    if (*symbol > 0) {
      return 1;
    }
  }
  // 2-byte UTF8
  if (size > 1 &&
      (input[0] & 0xe0) == 0xc0 &&
      (input[1] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x1f) << 6) |
               (input[1] & 0x3f));
    if (*symbol > 0x7f) {
      return 2;
    }
  }
  // 3-byte UFT8
  if (size > 2 &&
      (input[0] & 0xf0) == 0xe0 &&
      (input[1] & 0xc0) == 0x80 &&
      (input[2] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x0f) << 12) |
               ((input[1] & 0x3f) << 6) |
               (input[2] & 0x3f));
    if (*symbol > 0x7ff) {
      return 3;
    }
  }
  // 4-byte UFT8
  if (size > 3 &&
      (input[0] & 0xf8) == 0xf0 &&
      (input[1] & 0xc0) == 0x80 &&
      (input[2] & 0xc0) == 0x80 &&
      (input[3] & 0xc0) == 0x80) {
    *symbol = (((input[0] & 0x07) << 18) |
               ((input[1] & 0x3f) << 12) |
               ((input[2] & 0x3f) << 6) |
               (input[3] & 0x3f));
    if (*symbol > 0xffff && *symbol <= 0x10ffff) {
      return 4;
    }
  }
  // Not UTF8, emit a special symbol above the UTF8-code space
  *symbol = 0x110000 | input[0];
  return 1;
}

// Returns true if at least min_fraction of the data is UTF8-encoded.
bool IsMostlyUTF8(const uint8_t* data, size_t length, double min_fraction) {
  size_t size_utf8 = 0;
  size_t pos = 0;
  while (pos < length) {
    int symbol;
    int bytes_read = ParseAsUTF8(&symbol, data + pos, length - pos);
    pos += bytes_read;
    if (symbol < 0x110000) size_utf8 += bytes_read;
  }
  return size_utf8 > min_fraction * length;
}

void RecomputeDistancePrefixes(std::vector<Command>* cmds,
                               int num_direct_distance_codes,
                               int distance_postfix_bits) {
  if (num_direct_distance_codes == 0 &&
      distance_postfix_bits == 0) {
    return;
  }
  for (int i = 0; i < cmds->size(); ++i) {
    Command* cmd = &(*cmds)[i];
    if (cmd->copy_len_ > 0 && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(cmd->DistanceCode(),
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

size_t MetaBlockLength(const std::vector<Command>& cmds) {
  size_t length = 0;
  for (int i = 0; i < cmds.size(); ++i) {
    const Command& cmd = cmds[i];
    length += cmd.insert_len_ + cmd.copy_len_;
  }
  return length;
}

BrotliCompressor::BrotliCompressor(BrotliParams params)
    : params_(params),
      window_bits_(kWindowBits),
      hashers_(new Hashers()),
      input_pos_(0),
      ringbuffer_(kRingBufferBits, kMetaBlockSizeBits),
      literal_cost_(1 << kRingBufferBits),
      storage_ix_(0),
      storage_(new uint8_t[2 << kMetaBlockSizeBits]) {
  dist_cache_[0] = 4;
  dist_cache_[1] = 11;
  dist_cache_[2] = 15;
  dist_cache_[3] = 16;
  storage_[0] = 0;
  switch (params.mode) {
    case BrotliParams::MODE_TEXT: hash_type_ = 8; break;
    case BrotliParams::MODE_FONT: hash_type_ = 9; break;
    default: break;
  }
  hashers_->Init(hash_type_);
  if (params.mode == BrotliParams::MODE_TEXT) {
    StoreDictionaryWordHashes(params.enable_transforms);
  }
}

BrotliCompressor::~BrotliCompressor() {
  delete[] storage_;
}

StaticDictionary *BrotliCompressor::static_dictionary_ = NULL;

void BrotliCompressor::StoreDictionaryWordHashes(bool enable_transforms) {
  const int num_transforms = enable_transforms ? kNumTransforms : 1;
  if (static_dictionary_ == NULL) {
    static_dictionary_ = new StaticDictionary;
    for (int t = num_transforms - 1; t >= 0; --t) {
      for (int i = kMaxDictionaryWordLength;
           i >= kMinDictionaryWordLength; --i) {
        const int num_words = 1 << kBrotliDictionarySizeBitsByLength[i];
        for (int j = num_words - 1; j >= 0; --j) {
          int word_id = t * num_words + j;
          std::string word = GetTransformedDictionaryWord(i, word_id);
          if (word.size() >= 4) {
            static_dictionary_->Insert(word, i, word_id);
          }
        }
      }
    }
  }
  hashers_->SetStaticDictionary(static_dictionary_);
}

void BrotliCompressor::WriteStreamHeader() {
  // Encode window size.
  if (window_bits_ == 16) {
    WriteBits(1, 0, &storage_ix_, storage_);
  } else {
    WriteBits(1, 1, &storage_ix_, storage_);
    WriteBits(3, window_bits_ - 17, &storage_ix_, storage_);
  }
}

bool BrotliCompressor::WriteMetaBlock(const size_t input_size,
                                      const uint8_t* input_buffer,
                                      const bool is_last,
                                      size_t* encoded_size,
                                      uint8_t* encoded_buffer) {
  static const double kMinUTF8Ratio = 0.75;
  bool utf8_mode = false;
  std::vector<Command> commands((input_size + 1) >> 1);
  if (input_size > 0) {
    ringbuffer_.Write(input_buffer, input_size);
    utf8_mode = IsMostlyUTF8(
      &ringbuffer_.start()[input_pos_ & kRingBufferMask],
      input_size, kMinUTF8Ratio);
    if (utf8_mode) {
      EstimateBitCostsForLiteralsUTF8(input_pos_, input_size,
                                      kRingBufferMask, kRingBufferMask,
                                      ringbuffer_.start(), &literal_cost_[0]);
    } else {
      EstimateBitCostsForLiterals(input_pos_, input_size,
                                  kRingBufferMask, kRingBufferMask,
                                  ringbuffer_.start(), &literal_cost_[0]);
    }
    int last_insert_len = 0;
    int num_commands = 0;
    double base_min_score = 8.115;
    CreateBackwardReferences(
        input_size, input_pos_,
        ringbuffer_.start(), kRingBufferMask,
        &literal_cost_[0], kRingBufferMask,
        kMaxBackwardDistance,
        base_min_score,
        9,  // quality
        hashers_.get(),
        hash_type_,
        dist_cache_,
        &last_insert_len,
        &commands[0],
        &num_commands);
    commands.resize(num_commands);
    if (last_insert_len > 0) {
      commands.push_back(Command(last_insert_len));
    }
  }
  int num_direct_distance_codes =
      params_.mode == BrotliParams::MODE_FONT ? 12 : 0;
  int distance_postfix_bits = params_.mode == BrotliParams::MODE_FONT ? 1 : 0;
  int literal_context_mode = CONTEXT_SIGNED;
  const int storage_ix0 = storage_ix_;
  MetaBlockSplit mb;
  size_t len = MetaBlockLength(commands);
  if (!commands.empty()) {
    if (params_.greedy_block_split) {
      BuildMetaBlockGreedy(ringbuffer_.start(), input_pos_, kRingBufferMask,
                           commands.data(), commands.size(), 9, &mb);
    } else {
      RecomputeDistancePrefixes(&commands,
                                num_direct_distance_codes,
                                distance_postfix_bits);
      BuildMetaBlock(ringbuffer_.start(), input_pos_, kRingBufferMask,
                     commands,
                     num_direct_distance_codes,
                     distance_postfix_bits,
                     literal_context_mode,
                     &mb);
    }
  }
  if (!StoreMetaBlock(ringbuffer_.start(), input_pos_, len, kRingBufferMask,
                      is_last, 9,
                      num_direct_distance_codes,
                      distance_postfix_bits,
                      literal_context_mode,
                      commands.data(), commands.size(),
                      mb,
                      &storage_ix_, storage_)) {
    return false;
  }
  input_pos_ += len;
  size_t output_size = is_last ? ((storage_ix_ + 7) >> 3) : (storage_ix_ >> 3);
  output_size -= (storage_ix0 >> 3);
  if (input_size + 4 < output_size) {
    storage_ix_ = storage_ix0;
    storage_[storage_ix_ >> 3] &= (1 << (storage_ix_ & 7)) - 1;
    if (!StoreUncompressedMetaBlockHeader(input_size, &storage_ix_, storage_)) {
      return false;
    }
    size_t hdr_size = (storage_ix_ + 7) >> 3;
    if ((hdr_size + input_size + (is_last ? 1 : 0)) > *encoded_size) {
      return false;
    }
    memcpy(encoded_buffer, storage_, hdr_size);
    memcpy(encoded_buffer + hdr_size, input_buffer, input_size);
    *encoded_size = hdr_size + input_size;
    if (is_last) {
      encoded_buffer[*encoded_size] = 0x3;  // ISLAST, ISEMPTY
      ++(*encoded_size);
    }
    storage_ix_ = 0;
    storage_[0] = 0;
  } else {
    if (output_size > *encoded_size) {
      return false;
    }
    memcpy(encoded_buffer, storage_, output_size);
    *encoded_size = output_size;
    if (is_last) {
      storage_ix_ = 0;
      storage_[0] = 0;
    } else {
      storage_ix_ -= output_size << 3;
      storage_[storage_ix_ >> 3] = storage_[output_size];
    }
  }
  return true;
}

bool BrotliCompressor::FinishStream(
    size_t* encoded_size, uint8_t* encoded_buffer) {
  return WriteMetaBlock(0, NULL, true, encoded_size, encoded_buffer);
}

int BrotliCompressBuffer(BrotliParams params,
                         size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer) {
  if (*encoded_size == 0) {
    // Output buffer needs at least one byte.
    return 0;
  } else  if (input_size == 0) {
    encoded_buffer[0] = 6;
    *encoded_size = 1;
    return 1;
  }

  BrotliCompressor compressor(params);
  compressor.WriteStreamHeader();

  const int max_block_size = 1 << kMetaBlockSizeBits;
  size_t max_output_size = *encoded_size;
  const uint8_t* input_end = input_buffer + input_size;
  *encoded_size = 0;

  while (input_buffer < input_end) {
    int block_size = max_block_size;
    bool is_last = false;
    if (block_size >= input_end - input_buffer) {
      block_size = input_end - input_buffer;
      is_last = true;
    }
    size_t output_size = max_output_size;
    if (!compressor.WriteMetaBlock(block_size, input_buffer,
                                   is_last, &output_size,
                                   &encoded_buffer[*encoded_size])) {
      return 0;
    }
    input_buffer += block_size;
    *encoded_size += output_size;
    max_output_size -= output_size;
  }
  return 1;
}

}  // namespace brotli
