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

template<int kSize>
double Entropy(const std::vector<Histogram<kSize> >& histograms) {
  double retval = 0;
  for (int i = 0; i < histograms.size(); ++i) {
    retval += histograms[i].EntropyBitCost();
  }
  return retval;
}

template<int kSize>
double TotalBitCost(const std::vector<Histogram<kSize> >& histograms) {
  double retval = 0;
  for (int i = 0; i < histograms.size(); ++i) {
    retval += PopulationCost(histograms[i]);
  }
  return retval;
}

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

void EncodeMetaBlockLength(size_t meta_block_size,
                           bool is_last,
                           bool is_uncompressed,
                           int* storage_ix, uint8_t* storage) {
  WriteBits(1, is_last, storage_ix, storage);
  if (is_last) {
    if (meta_block_size == 0) {
      WriteBits(1, 1, storage_ix, storage);
      return;
    }
    WriteBits(1, 0, storage_ix, storage);
  }
  --meta_block_size;
  int num_bits = Log2Floor(meta_block_size) + 1;
  if (num_bits < 16) {
    num_bits = 16;
  }
  WriteBits(2, (num_bits - 13) >> 2, storage_ix, storage);
  while (num_bits > 0) {
    WriteBits(4, meta_block_size & 0xf, storage_ix, storage);
    meta_block_size >>= 4;
    num_bits -= 4;
  }
  if (!is_last) {
    WriteBits(1, is_uncompressed, storage_ix, storage);
  }
}

template<int kSize>
void BuildAndStoreEntropyCode(const Histogram<kSize>& histogram,
                              const int tree_limit,
                              const int alphabet_size,
                              EntropyCode<kSize>* code,
                              int* storage_ix, uint8_t* storage) {
  memset(code->depth_, 0, sizeof(code->depth_));
  memset(code->bits_, 0, sizeof(code->bits_));
  BuildAndStoreHuffmanTree(histogram.data_, alphabet_size, 9,
                           code->depth_, code->bits_, storage_ix, storage);
}

template<int kSize>
void BuildAndStoreEntropyCodes(
    const std::vector<Histogram<kSize> >& histograms,
    int alphabet_size,
    std::vector<EntropyCode<kSize> >* entropy_codes,
    int* storage_ix, uint8_t* storage) {
  entropy_codes->resize(histograms.size());
  for (int i = 0; i < histograms.size(); ++i) {
    BuildAndStoreEntropyCode(histograms[i], 15, alphabet_size,
                             &(*entropy_codes)[i],
                             storage_ix, storage);
  }
}

void EncodeCommand(const Command& cmd,
                   const EntropyCodeCommand& entropy,
                   int* storage_ix, uint8_t* storage) {
  int code = cmd.cmd_prefix_;
  WriteBits(entropy.depth_[code], entropy.bits_[code], storage_ix, storage);
  int nextra = cmd.cmd_extra_ >> 48;
  uint64_t extra = cmd.cmd_extra_ & 0xffffffffffffULL;
  if (nextra > 0) {
    WriteBits(nextra, extra, storage_ix, storage);
  }
}

void EncodeCopyDistance(const Command& cmd, const EntropyCodeDistance& entropy,
                        int* storage_ix, uint8_t* storage) {
  int code = cmd.dist_prefix_;
  int extra_bits = cmd.dist_extra_ >> 24;
  uint64_t extra_bits_val = cmd.dist_extra_ & 0xffffff;
  WriteBits(entropy.depth_[code], entropy.bits_[code], storage_ix, storage);
  if (extra_bits > 0) {
    WriteBits(extra_bits, extra_bits_val, storage_ix, storage);
  }
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

void MoveAndEncode(const BlockSplitCode& code,
                   BlockSplitIterator* it,
                   int* storage_ix, uint8_t* storage) {
  if (it->length_ == 0) {
    ++it->idx_;
    it->type_ = it->split_.types_[it->idx_];
    it->length_ = it->split_.lengths_[it->idx_];
    StoreBlockSwitch(code, it->idx_, storage_ix, storage);
  }
  --it->length_;
}

struct EncodingParams {
  int num_direct_distance_codes;
  int distance_postfix_bits;
  int literal_context_mode;
};

struct MetaBlock {
  std::vector<Command> cmds;
  EncodingParams params;
  BlockSplit literal_split;
  BlockSplit command_split;
  BlockSplit distance_split;
  std::vector<int> literal_context_modes;
  std::vector<int> literal_context_map;
  std::vector<int> distance_context_map;
  std::vector<HistogramLiteral> literal_histograms;
  std::vector<HistogramCommand> command_histograms;
  std::vector<HistogramDistance> distance_histograms;
};

void BuildMetaBlock(const EncodingParams& params,
                    const std::vector<Command>& cmds,
                    const uint8_t* ringbuffer,
                    const size_t pos,
                    const size_t mask,
                    MetaBlock* mb) {
  mb->cmds = cmds;
  mb->params = params;
  if (cmds.empty()) {
    return;
  }
  RecomputeDistancePrefixes(&mb->cmds,
                            mb->params.num_direct_distance_codes,
                            mb->params.distance_postfix_bits);
  SplitBlock(mb->cmds,
             &ringbuffer[pos & mask],
             &mb->literal_split,
             &mb->command_split,
             &mb->distance_split);

  mb->literal_context_modes.resize(mb->literal_split.num_types_,
                                   mb->params.literal_context_mode);


  int num_literal_contexts =
      mb->literal_split.num_types_ << kLiteralContextBits;
  int num_distance_contexts =
      mb->distance_split.num_types_ << kDistanceContextBits;
  std::vector<HistogramLiteral> literal_histograms(num_literal_contexts);
  mb->command_histograms.resize(mb->command_split.num_types_);
  std::vector<HistogramDistance> distance_histograms(num_distance_contexts);
  BuildHistograms(mb->cmds,
                  mb->literal_split,
                  mb->command_split,
                  mb->distance_split,
                  ringbuffer,
                  pos,
                  mask,
                  mb->literal_context_modes,
                  &literal_histograms,
                  &mb->command_histograms,
                  &distance_histograms);

  // Histogram ids need to fit in one byte.
  static const int kMaxNumberOfHistograms = 256;

  mb->literal_histograms = literal_histograms;
  ClusterHistograms(literal_histograms,
                    1 << kLiteralContextBits,
                    mb->literal_split.num_types_,
                    kMaxNumberOfHistograms,
                    &mb->literal_histograms,
                    &mb->literal_context_map);

  mb->distance_histograms = distance_histograms;
  ClusterHistograms(distance_histograms,
                    1 << kDistanceContextBits,
                    mb->distance_split.num_types_,
                    kMaxNumberOfHistograms,
                    &mb->distance_histograms,
                    &mb->distance_context_map);
}

size_t MetaBlockLength(const std::vector<Command>& cmds) {
  size_t length = 0;
  for (int i = 0; i < cmds.size(); ++i) {
    const Command& cmd = cmds[i];
    length += cmd.insert_len_ + cmd.copy_len_;
  }
  return length;
}

void StoreMetaBlock(const MetaBlock& mb,
                    const bool is_last,
                    const uint8_t* ringbuffer,
                    const size_t mask,
                    size_t* pos,
                    int* storage_ix, uint8_t* storage) {
  size_t length = MetaBlockLength(mb.cmds);
  const size_t end_pos = *pos + length;
  EncodeMetaBlockLength(length, is_last, false, storage_ix, storage);

  if (length == 0) {
    return;
  }
  BlockSplitCode literal_split_code;
  BlockSplitCode command_split_code;
  BlockSplitCode distance_split_code;
  BuildAndStoreBlockSplitCode(mb.literal_split.types_,
                              mb.literal_split.lengths_,
                              mb.literal_split.num_types_,
                              9,  // quality
                              &literal_split_code,
                              storage_ix, storage);
  BuildAndStoreBlockSplitCode(mb.command_split.types_,
                              mb.command_split.lengths_,
                              mb.command_split.num_types_,
                              9,  // quality
                              &command_split_code,
                              storage_ix, storage);
  BuildAndStoreBlockSplitCode(mb.distance_split.types_,
                              mb.distance_split.lengths_,
                              mb.distance_split.num_types_,
                              9,  // quality
                              &distance_split_code,
                              storage_ix, storage);
  WriteBits(2, mb.params.distance_postfix_bits, storage_ix, storage);
  WriteBits(4,
            mb.params.num_direct_distance_codes >>
            mb.params.distance_postfix_bits,
            storage_ix, storage);
  int num_distance_codes =
      kNumDistanceShortCodes + mb.params.num_direct_distance_codes +
      (48 << mb.params.distance_postfix_bits);
  for (int i = 0; i < mb.literal_split.num_types_; ++i) {
    WriteBits(2, mb.literal_context_modes[i], storage_ix, storage);
  }
  EncodeContextMap(mb.literal_context_map, mb.literal_histograms.size(),
                   storage_ix, storage);
  EncodeContextMap(mb.distance_context_map, mb.distance_histograms.size(),
                   storage_ix, storage);
  std::vector<EntropyCodeLiteral> literal_codes;
  std::vector<EntropyCodeCommand> command_codes;
  std::vector<EntropyCodeDistance> distance_codes;
  BuildAndStoreEntropyCodes(mb.literal_histograms, 256, &literal_codes,
                            storage_ix, storage);
  BuildAndStoreEntropyCodes(mb.command_histograms, kNumCommandPrefixes,
                            &command_codes, storage_ix, storage);
  BuildAndStoreEntropyCodes(mb.distance_histograms, num_distance_codes,
                            &distance_codes, storage_ix, storage);
  BlockSplitIterator literal_it(mb.literal_split);
  BlockSplitIterator command_it(mb.command_split);
  BlockSplitIterator distance_it(mb.distance_split);
  for (int i = 0; i < mb.cmds.size(); ++i) {
    const Command& cmd = mb.cmds[i];
    MoveAndEncode(command_split_code, &command_it, storage_ix, storage);
    EncodeCommand(cmd, command_codes[command_it.type_], storage_ix, storage);
    for (int j = 0; j < cmd.insert_len_; ++j) {
      MoveAndEncode(literal_split_code, &literal_it, storage_ix, storage);
      int histogram_idx = literal_it.type_;
      uint8_t prev_byte = *pos > 0 ? ringbuffer[(*pos - 1) & mask] : 0;
      uint8_t prev_byte2 = *pos > 1 ? ringbuffer[(*pos - 2) & mask] : 0;
      int context = ((literal_it.type_ << kLiteralContextBits) +
                     Context(prev_byte, prev_byte2,
                             mb.literal_context_modes[literal_it.type_]));
      histogram_idx = mb.literal_context_map[context];
      int literal = ringbuffer[*pos & mask];
      WriteBits(literal_codes[histogram_idx].depth_[literal],
                literal_codes[histogram_idx].bits_[literal],
                storage_ix, storage);
      ++(*pos);
    }
    if (*pos < end_pos && cmd.cmd_prefix_ >= 128) {
      MoveAndEncode(distance_split_code, &distance_it, storage_ix, storage);
      int context = (distance_it.type_ << 2) + cmd.DistanceContext();
      int histogram_index = mb.distance_context_map[context];
      EncodeCopyDistance(cmd, distance_codes[histogram_index],
                         storage_ix, storage);
    }
    *pos += cmd.copy_len_;
  }
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
    StoreDictionaryWordHashes();
  }
}

BrotliCompressor::~BrotliCompressor() {
  delete[] storage_;
}

StaticDictionary *BrotliCompressor::static_dictionary_ = NULL;

void BrotliCompressor::StoreDictionaryWordHashes() {
  const int num_transforms = kNumTransforms;
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

void BrotliCompressor::WriteMetaBlock(const size_t input_size,
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
  EncodingParams params;
  params.num_direct_distance_codes =
      params_.mode == BrotliParams::MODE_FONT ? 12 : 0;
  params.distance_postfix_bits =
      params_.mode == BrotliParams::MODE_FONT ? 1 : 0;
  params.literal_context_mode = CONTEXT_SIGNED;
  const int storage_ix0 = storage_ix_;
  MetaBlock mb;
  BuildMetaBlock(params, commands, ringbuffer_.start(), input_pos_,
                 kRingBufferMask, &mb);
  StoreMetaBlock(mb, is_last, ringbuffer_.start(), kRingBufferMask,
                 &input_pos_, &storage_ix_, storage_);
  size_t output_size = is_last ? ((storage_ix_ + 7) >> 3) : (storage_ix_ >> 3);
  output_size -= (storage_ix0 >> 3);
  if (input_size + 4 < output_size) {
    storage_ix_ = storage_ix0;
    storage_[storage_ix_ >> 3] &= (1 << (storage_ix_ & 7)) - 1;
    EncodeMetaBlockLength(input_size, false, true, &storage_ix_, storage_);
    size_t hdr_size = (storage_ix_ + 7) >> 3;
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
}

void BrotliCompressor::FinishStream(
    size_t* encoded_size, uint8_t* encoded_buffer) {
  WriteMetaBlock(0, NULL, true, encoded_size, encoded_buffer);
}

int BrotliCompressBuffer(BrotliParams params,
                         size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer) {
  if (input_size == 0) {
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
    compressor.WriteMetaBlock(block_size, input_buffer, is_last,
                              &output_size, &encoded_buffer[*encoded_size]);
    input_buffer += block_size;
    *encoded_size += output_size;
    max_output_size -= output_size;
  }
  return 1;
}

}  // namespace brotli
