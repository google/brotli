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
#include "./cluster.h"
#include "./context.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./histogram.h"
#include "./prefix.h"
#include "./write_bits.h"

namespace brotli {

template<int kSize>
double Entropy(const std::vector<Histogram<kSize> >& histograms) {
  double retval = 0;
  for (int i = 0; i < histograms.size(); ++i) {
    retval += histograms[i].EntropyBitCost();
  }
  return retval;
}

void EncodeSize(size_t len, int* storage_ix, uint8_t* storage) {
  std::vector<uint8_t> len_bytes;
  while (len > 0) {
    len_bytes.push_back(len & 0xff);
    len >>= 8;
  };
  WriteBits(3, len_bytes.size(), storage_ix, storage);
  for (int i = 0; i < len_bytes.size(); ++i) {
    WriteBits(8, len_bytes[i], storage_ix, storage);
  }
}

void EncodeMetaBlockLength(int input_size_bits,
                           size_t meta_block_size,
                           bool is_last_meta_block,
                           int* storage_ix, uint8_t* storage) {
  WriteBits(1, is_last_meta_block, storage_ix, storage);
  if (is_last_meta_block) return;
  while (input_size_bits > 0) {
    WriteBits(8, meta_block_size & 0xff, storage_ix, storage);
    meta_block_size >>= 8;
    input_size_bits -= 8;
  }
  if (input_size_bits > 0) {
    WriteBits(input_size_bits, meta_block_size, storage_ix, storage);
  }
}

template<int kSize>
void EntropyEncode(int val, const EntropyCode<kSize>& code,
                   int* storage_ix, uint8_t* storage) {
  if (code.count_ <= 1) {
    return;
  };
  WriteBits(code.depth_[val], code.bits_[val], storage_ix, storage);
}

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const uint8_t* code_length_bitdepth,
    int* storage_ix, uint8_t* storage) {
  static const uint8_t kStorageOrder[kCodeLengthCodes] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  };
  // Throw away trailing zeros:
  int codes_to_store = kCodeLengthCodes;
  for (; codes_to_store > 4; --codes_to_store) {
    if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
      break;
    }
  }
  WriteBits(4, codes_to_store - 4, storage_ix, storage);
  for (int i = 0; i < codes_to_store; ++i) {
    WriteBits(3, code_length_bitdepth[kStorageOrder[i]], storage_ix, storage);
  }
}

void StoreHuffmanTreeToBitMask(
    const uint8_t* huffman_tree,
    const uint8_t* huffman_tree_extra_bits,
    const int huffman_tree_size,
    const EntropyCode<kCodeLengthCodes>& entropy,
    int* storage_ix, uint8_t* storage) {
  for (int i = 0; i < huffman_tree_size; ++i) {
    const int ix = huffman_tree[i];
    const int extra_bits = huffman_tree_extra_bits[i];
    EntropyEncode(ix, entropy, storage_ix, storage);
    switch (ix) {
      case 16:
        WriteBits(2, extra_bits, storage_ix, storage);
        break;
      case 17:
        WriteBits(3, extra_bits, storage_ix, storage);
        break;
      case 18:
        WriteBits(7, extra_bits, storage_ix, storage);
        break;
    }
  }
}

template<int kSize>
void StoreHuffmanCode(const EntropyCode<kSize>& code, int alphabet_size,
                      int* storage_ix, uint8_t* storage) {
  const int kMaxBits = 8;
  const int kMaxSymbol = 1 << kMaxBits;

  if (code.count_ == 0) {   // emit minimal tree for empty cases
    // bits: small tree marker: 1, count-1: 0, large 8-bit code: 0, code: 0
    WriteBits(4, 0x01, storage_ix, storage);
    return;
  }
  if (code.count_ <= 2 &&
      code.symbols_[0] < kMaxSymbol &&
      code.symbols_[1] < kMaxSymbol) {
    // Small tree marker to encode 1 or 2 symbols.
    WriteBits(1, 1, storage_ix, storage);
    WriteBits(1, code.count_ - 1, storage_ix, storage);
    if (code.symbols_[0] <= 1) {
      // Code bit for small (1 bit) symbol value.
      WriteBits(1, 0, storage_ix, storage);
      WriteBits(1, code.symbols_[0], storage_ix, storage);
    } else {
      WriteBits(1, 1, storage_ix, storage);
      WriteBits(8, code.symbols_[0], storage_ix, storage);
    }
    if (code.count_ == 2) {
      WriteBits(8, code.symbols_[1], storage_ix, storage);
    }
    return;
  }
  WriteBits(1, 0, storage_ix, storage);

  uint8_t huffman_tree[kSize];
  uint8_t huffman_tree_extra_bits[kSize];
  int huffman_tree_size = 0;
  WriteHuffmanTree(&code.depth_[0],
                   alphabet_size,
                   &huffman_tree[0],
                   &huffman_tree_extra_bits[0],
                   &huffman_tree_size);
  Histogram<kCodeLengthCodes> huffman_tree_histogram;
  memset(huffman_tree_histogram.data_, 0, sizeof(huffman_tree_histogram.data_));
  for (int i = 0; i < huffman_tree_size; ++i) {
    huffman_tree_histogram.Add(huffman_tree[i]);
  }
  EntropyCode<kCodeLengthCodes> huffman_tree_entropy;
  BuildEntropyCode(huffman_tree_histogram, 7, kCodeLengthCodes,
                   &huffman_tree_entropy);
  Histogram<kCodeLengthCodes> trimmed_histogram = huffman_tree_histogram;
  uint8_t* last_code = &huffman_tree[huffman_tree_size - 1];
  while (*last_code == 0 || *last_code >= 17) {
    trimmed_histogram.Remove(*last_code--);
  }
  int trimmed_size = trimmed_histogram.total_count_;
  bool write_length = false;
  if (trimmed_size > 1 && trimmed_size < huffman_tree_size) {
    EntropyCode<kCodeLengthCodes> trimmed_entropy;
    BuildEntropyCode(trimmed_histogram, 7, kCodeLengthCodes, &trimmed_entropy);
    int huffman_bit_cost = HuffmanTreeBitCost(huffman_tree_histogram,
                                              huffman_tree_entropy);
    int trimmed_bit_cost = HuffmanTreeBitCost(trimmed_histogram,
                                              trimmed_entropy);;
    const int nbits = Log2Ceiling(trimmed_size - 1);
    const int nbitpairs = (nbits == 0) ? 1 : (nbits + 1) / 2;
    if (trimmed_bit_cost + 3 + 2 * nbitpairs < huffman_bit_cost) {
      write_length = true;
      huffman_tree_size = trimmed_size;
      huffman_tree_entropy = trimmed_entropy;
    }
  }

  StoreHuffmanTreeOfHuffmanTreeToBitMask(
      &huffman_tree_entropy.depth_[0], storage_ix, storage);
  WriteBits(1, write_length, storage_ix, storage);
  if (write_length) {
    const int nbits = Log2Ceiling(huffman_tree_size - 1);
    const int nbitpairs = (nbits == 0) ? 1 : (nbits + 1) / 2;
    WriteBits(3, nbitpairs - 1, storage_ix, storage);
    WriteBits(nbitpairs * 2, huffman_tree_size - 2, storage_ix, storage);
  }
  StoreHuffmanTreeToBitMask(&huffman_tree[0], &huffman_tree_extra_bits[0],
                            huffman_tree_size, huffman_tree_entropy,
                            storage_ix, storage);
}

template<int kSize>
void StoreHuffmanCodes(const std::vector<EntropyCode<kSize> >& codes,
                       int alphabet_size,
                       int* storage_ix, uint8_t* storage) {
  for (int i = 0; i < codes.size(); ++i) {
    StoreHuffmanCode(codes[i], alphabet_size, storage_ix, storage);
  }
}

void EncodeCommand(const Command& cmd,
                   const EntropyCodeCommand& entropy,
                   int* storage_ix, uint8_t* storage) {
  int code = cmd.command_prefix_;
  EntropyEncode(code, entropy, storage_ix, storage);
  if (code >= 128) {
    code -= 128;
  }
  int insert_extra_bits = InsertLengthExtraBits(code);
  uint64_t insert_extra_bits_val =
      cmd.insert_length_ - InsertLengthOffset(code);
  int copy_extra_bits = CopyLengthExtraBits(code);
  uint64_t copy_extra_bits_val = cmd.copy_length_ - CopyLengthOffset(code);
  if (insert_extra_bits > 0) {
    WriteBits(insert_extra_bits, insert_extra_bits_val, storage_ix, storage);
  }
  if (copy_extra_bits > 0) {
    WriteBits(copy_extra_bits, copy_extra_bits_val, storage_ix, storage);
  }
}

void EncodeCopyDistance(const Command& cmd, const EntropyCodeDistance& entropy,
                        int* storage_ix, uint8_t* storage) {
  int code = cmd.distance_prefix_;
  int extra_bits = cmd.distance_extra_bits_;
  uint64_t extra_bits_val = cmd.distance_extra_bits_value_;
  EntropyEncode(code, entropy, storage_ix, storage);
  if (extra_bits > 0) {
    WriteBits(extra_bits, extra_bits_val, storage_ix, storage);
  }
}


void ComputeDistanceShortCodes(std::vector<Command>* cmds) {
  static const int kIndexOffset[16] = {
    3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
  };
  static const int kValueOffset[16] = {
    0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
  };
  int dist_ringbuffer[4] = { 4, 11, 15, 16 };
  int ringbuffer_idx = 0;
  for (int i = 0; i < cmds->size(); ++i) {
    int cur_dist = (*cmds)[i].copy_distance_;
    if (cur_dist == 0) break;
    int dist_code = cur_dist + 16;
    for (int k = 0; k < 16; ++k) {
      // Only accept more popular choices.
      if (cur_dist < 11 && ((k >= 2 && k < 4) || k >= 6)) {
        // Typically unpopular ranges, don't replace a short distance
        // with them.
        continue;
      }
      int comp = (dist_ringbuffer[(ringbuffer_idx + kIndexOffset[k]) & 3] +
                  kValueOffset[k]);
      if (cur_dist == comp) {
        dist_code = k + 1;
        break;
      }
    }
    if (dist_code > 1) {
      dist_ringbuffer[ringbuffer_idx & 3] = cur_dist;
      ++ringbuffer_idx;
    }
    (*cmds)[i].distance_code_ = dist_code;
  }
}

void ComputeCommandPrefixes(std::vector<Command>* cmds,
                            int num_direct_distance_codes,
                            int distance_postfix_bits) {
  for (int i = 0; i < cmds->size(); ++i) {
    Command* cmd = &(*cmds)[i];
    cmd->command_prefix_ = CommandPrefix(cmd->insert_length_,
                                         cmd->copy_length_);
    if (cmd->copy_length_ > 0) {
      PrefixEncodeCopyDistance(cmd->distance_code_,
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->distance_prefix_,
                               &cmd->distance_extra_bits_,
                               &cmd->distance_extra_bits_value_);
    }
    if (cmd->command_prefix_ < 128 && cmd->distance_prefix_ == 0) {
      cmd->distance_prefix_ = 0xffff;
    } else {
      cmd->command_prefix_ += 128;
    }
  }
}

int IndexOf(const std::vector<int>& v, int value) {
  for (int i = 0; i < v.size(); ++i) {
    if (v[i] == value) return i;
  }
  return -1;
}

void MoveToFront(std::vector<int>* v, int index) {
  int value = (*v)[index];
  for (int i = index; i > 0; --i) {
    (*v)[i] = (*v)[i - 1];
  }
  (*v)[0] = value;
}

std::vector<int> MoveToFrontTransform(const std::vector<int>& v) {
  if (v.empty()) return v;
  std::vector<int> mtf(*max_element(v.begin(), v.end()) + 1);
  for (int i = 0; i < mtf.size(); ++i) mtf[i] = i;
  std::vector<int> result(v.size());
  for (int i = 0; i < v.size(); ++i) {
    int index = IndexOf(mtf, v[i]);
    result[i] = index;
    MoveToFront(&mtf, index);
  }
  return result;
}

// Finds runs of zeros in v_in and replaces them with a prefix code of the run
// length plus extra bits in *v_out and *extra_bits. Non-zero values in v_in are
// shifted by *max_length_prefix. Will not create prefix codes bigger than the
// initial value of *max_run_length_prefix. The prefix code of run length L is
// simply Log2Floor(L) and the number of extra bits is the same as the prefix
// code.
void RunLengthCodeZeros(const std::vector<int>& v_in,
                        int* max_run_length_prefix,
                        std::vector<int>* v_out,
                        std::vector<int>* extra_bits) {
  int max_reps = 0;
  for (int i = 0; i < v_in.size();) {
    for (; i < v_in.size() && v_in[i] != 0; ++i) ;
    int reps = 0;
    for (; i < v_in.size() && v_in[i] == 0; ++i) {
      ++reps;
    }
    max_reps = std::max(reps, max_reps);
  }
  int max_prefix = max_reps > 0 ? Log2Floor(max_reps) : 0;
  *max_run_length_prefix = std::min(max_prefix, *max_run_length_prefix);
  for (int i = 0; i < v_in.size();) {
    if (v_in[i] != 0) {
      v_out->push_back(v_in[i] + *max_run_length_prefix);
      extra_bits->push_back(0);
      ++i;
    } else {
      int reps = 1;
      for (uint32_t k = i + 1; k < v_in.size() && v_in[k] == 0; ++k) {
        ++reps;
      }
      i += reps;
      while (reps) {
        if (reps < (2 << *max_run_length_prefix)) {
          int run_length_prefix = Log2Floor(reps);
          v_out->push_back(run_length_prefix);
          extra_bits->push_back(reps - (1 << run_length_prefix));
          break;
        } else {
          v_out->push_back(*max_run_length_prefix);
          extra_bits->push_back((1 << *max_run_length_prefix) - 1);
          reps -= (2 << *max_run_length_prefix) - 1;
        }
      }
    }
  }
}

// Returns a maximum zero-run-length-prefix value such that run-length coding
// zeros in v with this maximum prefix value and then encoding the resulting
// histogram and entropy-coding v produces the least amount of bits.
int BestMaxZeroRunLengthPrefix(const std::vector<int>& v) {
  int min_cost = std::numeric_limits<int>::max();
  int best_max_prefix = 0;
  for (int max_prefix = 0; max_prefix <= 16; ++max_prefix) {
    std::vector<int> rle_symbols;
    std::vector<int> extra_bits;
    int max_run_length_prefix = max_prefix;
    RunLengthCodeZeros(v, &max_run_length_prefix, &rle_symbols, &extra_bits);
    if (max_run_length_prefix < max_prefix) break;
    HistogramLiteral histogram;
    for (int i = 0; i < rle_symbols.size(); ++i) {
      histogram.Add(rle_symbols[i]);
    }
    int bit_cost = PopulationCost(histogram);
    if (max_prefix > 0) {
      bit_cost += 4;
    }
    for (int i = 1; i <= max_prefix; ++i) {
      bit_cost += histogram.data_[i] * i;  // extra bits
    }
    if (bit_cost < min_cost) {
      min_cost = bit_cost;
      best_max_prefix = max_prefix;
    }
  }
  return best_max_prefix;
}

void EncodeContextMap(const std::vector<int>& context_map,
                      int context_mode,
                      int context_mode_bits,
                      int num_clusters,
                      int* storage_ix, uint8_t* storage) {
  if (context_mode == 0) {
    WriteBits(1, 0, storage_ix, storage);  // no context
    return;
  }

  WriteBits(1, 1, storage_ix, storage);  // have context
  if (context_mode_bits > 0) {
    WriteBits(context_mode_bits, context_mode - 1, storage_ix, storage);
  }
  WriteBits(8, num_clusters - 1, storage_ix, storage);

  if (num_clusters == 1 || num_clusters == context_map.size()) {
    return;
  }

  std::vector<int> transformed_symbols = MoveToFrontTransform(context_map);
  std::vector<int> rle_symbols;
  std::vector<int> extra_bits;
  int max_run_length_prefix = BestMaxZeroRunLengthPrefix(transformed_symbols);
  RunLengthCodeZeros(transformed_symbols, &max_run_length_prefix,
                     &rle_symbols, &extra_bits);
  HistogramLiteral symbol_histogram;
  for (int i = 0; i < rle_symbols.size(); ++i) {
    symbol_histogram.Add(rle_symbols[i]);
  }
  EntropyCodeLiteral symbol_code;
  BuildEntropyCode(symbol_histogram, 15, num_clusters + max_run_length_prefix,
                   &symbol_code);
  bool use_rle = max_run_length_prefix > 0;
  WriteBits(1, use_rle, storage_ix, storage);
  if (use_rle) {
    WriteBits(4, max_run_length_prefix - 1, storage_ix, storage);
  }
  StoreHuffmanCode(symbol_code, num_clusters + max_run_length_prefix,
                   storage_ix, storage);
  for (int i = 0; i < rle_symbols.size(); ++i) {
    EntropyEncode(rle_symbols[i], symbol_code, storage_ix, storage);
    if (rle_symbols[i] > 0 && rle_symbols[i] <= max_run_length_prefix) {
      WriteBits(rle_symbols[i], extra_bits[i], storage_ix, storage);
    }
  }
  WriteBits(1, 1, storage_ix, storage);  // use move-to-front
}

template<int kSize>
void BuildEntropyCodes(const std::vector<Histogram<kSize> >& histograms,
                       int alphabet_size,
                       std::vector<EntropyCode<kSize> >* entropy_codes) {
  entropy_codes->resize(histograms.size());
  for (int i = 0; i < histograms.size(); ++i) {
    BuildEntropyCode(histograms[i], 15, alphabet_size, &(*entropy_codes)[i]);
  }
}

struct BlockSplitCode {
  EntropyCodeLiteral block_type_code;
  EntropyCodeBlockLength block_len_code;
};

void EncodeBlockLength(const EntropyCodeBlockLength& entropy,
                       int length,
                       int* storage_ix, uint8_t* storage) {
  int len_code = BlockLengthPrefix(length);
  int extra_bits = BlockLengthExtraBits(len_code);
  int extra_bits_value = length - BlockLengthOffset(len_code);
  EntropyEncode(len_code, entropy, storage_ix, storage);

  if (extra_bits > 0) {
    WriteBits(extra_bits, extra_bits_value, storage_ix, storage);
  }
}

void ComputeBlockTypeShortCodes(BlockSplit* split) {
  if (split->num_types_ <= 1) {
    split->num_types_ = 1;
    return;
  }
  int ringbuffer[2] = { 0, 1 };
  size_t index = 0;
  for (int i = 0; i < split->types_.size(); ++i) {
    int type = split->types_[i];
    int type_code;
    if (type == ringbuffer[index & 1]) {
      type_code = 0;
    } else if (type == ringbuffer[(index - 1) & 1] + 1) {
      type_code = 1;
    } else {
      type_code = type + 2;
    }
    ringbuffer[index & 1] = type;
    ++index;
    split->type_codes_.push_back(type_code);
  }
}

void BuildAndEncodeBlockSplitCode(const BlockSplit& split,
                                  BlockSplitCode* code,
                                  int* storage_ix, uint8_t* storage) {
  if (split.num_types_ <= 1) {
    WriteBits(1, 0, storage_ix, storage);
    return;
  }
  WriteBits(1, 1, storage_ix, storage);
  HistogramLiteral type_histo;
  for (int i = 0; i < split.type_codes_.size(); ++i) {
    type_histo.Add(split.type_codes_[i]);
  }
  BuildEntropyCode(type_histo, 15, split.num_types_ + 2,
                   &code->block_type_code);
  HistogramBlockLength length_histo;
  for (int i = 0; i < split.lengths_.size(); ++i) {
    length_histo.Add(BlockLengthPrefix(split.lengths_[i]));
  }
  BuildEntropyCode(length_histo, 15, kNumBlockLenPrefixes,
                   &code->block_len_code);
  WriteBits(8, split.num_types_ - 1, storage_ix, storage);
  StoreHuffmanCode(code->block_type_code, split.num_types_ + 2,
                   storage_ix, storage);
  StoreHuffmanCode(code->block_len_code, kNumBlockLenPrefixes,
                   storage_ix, storage);
  EncodeBlockLength(code->block_len_code, split.lengths_[0],
                    storage_ix, storage);
}

void MoveAndEncode(const BlockSplitCode& code,
                   BlockSplitIterator* it,
                   int* storage_ix, uint8_t* storage) {
  if (it->length_ == 0) {
    ++it->idx_;
    it->type_ = it->split_.types_[it->idx_];
    it->length_ = it->split_.lengths_[it->idx_];
    uint8_t type_code = it->split_.type_codes_[it->idx_];
    EntropyEncode(type_code, code.block_type_code, storage_ix, storage);
    EncodeBlockLength(code.block_len_code, it->length_, storage_ix, storage);
  }
  --it->length_;
}

struct EncodingParams {
  int num_direct_distance_codes;
  int distance_postfix_bits;
  int literal_context_mode;
  int distance_context_mode;
};

struct MetaBlock {
  std::vector<Command> cmds;
  EncodingParams params;
  BlockSplit literal_split;
  BlockSplit command_split;
  BlockSplit distance_split;
  std::vector<int> literal_context_map;
  std::vector<int> distance_context_map;
  std::vector<HistogramLiteral> literal_histograms;
  std::vector<HistogramCommand> command_histograms;
  std::vector<HistogramDistance> distance_histograms;
};

void BuildMetaBlock(const EncodingParams& params,
                    const std::vector<Command>& cmds,
                    const uint8_t* input_buffer,
                    size_t pos,
                    MetaBlock* mb) {
  mb->cmds = cmds;
  mb->params = params;
  ComputeCommandPrefixes(&mb->cmds,
                         mb->params.num_direct_distance_codes,
                         mb->params.distance_postfix_bits);
  SplitBlock(mb->cmds,
             input_buffer + pos,
             &mb->literal_split,
             &mb->command_split,
             &mb->distance_split);
  ComputeBlockTypeShortCodes(&mb->literal_split);
  ComputeBlockTypeShortCodes(&mb->command_split);
  ComputeBlockTypeShortCodes(&mb->distance_split);

  int num_literal_contexts_per_block_type =
      NumContexts(mb->params.literal_context_mode);
  int num_literal_contexts =
      mb->literal_split.num_types_ *
      num_literal_contexts_per_block_type;
  int num_distance_contexts_per_block_type =
      (mb->params.distance_context_mode > 0 ? 4 : 1);
  int num_distance_contexts =
      mb->distance_split.num_types_ *
      num_distance_contexts_per_block_type;
  std::vector<HistogramLiteral> literal_histograms(num_literal_contexts);
  mb->command_histograms.resize(mb->command_split.num_types_);
  std::vector<HistogramDistance> distance_histograms(num_distance_contexts);
  BuildHistograms(mb->cmds,
                  mb->literal_split,
                  mb->command_split,
                  mb->distance_split,
                  input_buffer,
                  pos,
                  mb->params.literal_context_mode,
                  mb->params.distance_context_mode,
                  &literal_histograms,
                  &mb->command_histograms,
                  &distance_histograms);

  // Histogram ids need to fit in one byte and there are 16 ids reserved for
  // run length codes, which leaves a maximum number of 240 histograms.
  static const int kMaxNumberOfHistograms = 240;

  mb->literal_histograms = literal_histograms;
  if (mb->params.literal_context_mode > 0) {
    ClusterHistograms(literal_histograms,
                      num_literal_contexts_per_block_type,
                      mb->literal_split.num_types_,
                      kMaxNumberOfHistograms,
                      &mb->literal_histograms,
                      &mb->literal_context_map);
  }

  mb->distance_histograms = distance_histograms;
  if (mb->params.distance_context_mode > 0) {
    ClusterHistograms(distance_histograms,
                      num_distance_contexts_per_block_type,
                      mb->distance_split.num_types_,
                      kMaxNumberOfHistograms,
                      &mb->distance_histograms,
                      &mb->distance_context_map);
  }
}

size_t MetaBlockLength(const std::vector<Command>& cmds) {
  size_t length = 0;
  for (int i = 0; i < cmds.size(); ++i) {
    const Command& cmd = cmds[i];
    length += cmd.insert_length_ + cmd.copy_length_;
  }
  return length;
}

void StoreMetaBlock(const MetaBlock& mb,
                    const uint8_t* input_buffer,
                    int input_size_bits,
                    bool is_last,
                    size_t* pos,
                    int* storage_ix, uint8_t* storage) {
  size_t length = MetaBlockLength(mb.cmds);
  const size_t end_pos = *pos + length;
  EncodeMetaBlockLength(input_size_bits, length - 1, is_last,
                        storage_ix, storage);
  BlockSplitCode literal_split_code;
  BlockSplitCode command_split_code;
  BlockSplitCode distance_split_code;
  BuildAndEncodeBlockSplitCode(mb.literal_split, &literal_split_code,
                               storage_ix, storage);
  BuildAndEncodeBlockSplitCode(mb.command_split, &command_split_code,
                               storage_ix, storage);
  BuildAndEncodeBlockSplitCode(mb.distance_split, &distance_split_code,
                               storage_ix, storage);
  WriteBits(2, mb.params.distance_postfix_bits, storage_ix, storage);
  WriteBits(4,
            mb.params.num_direct_distance_codes >>
            mb.params.distance_postfix_bits, storage_ix, storage);
  int num_distance_codes =
      kNumDistanceShortCodes + mb.params.num_direct_distance_codes +
      (48 << mb.params.distance_postfix_bits);
  EncodeContextMap(mb.literal_context_map, mb.params.literal_context_mode, 4,
                   mb.literal_histograms.size(), storage_ix, storage);
  EncodeContextMap(mb.distance_context_map, mb.params.distance_context_mode, 0,
                   mb.distance_histograms.size(), storage_ix, storage);
  std::vector<EntropyCodeLiteral> literal_codes;
  std::vector<EntropyCodeCommand> command_codes;
  std::vector<EntropyCodeDistance> distance_codes;
  BuildEntropyCodes(mb.literal_histograms, 256, &literal_codes);
  BuildEntropyCodes(mb.command_histograms, kNumCommandPrefixes,
                    &command_codes);
  BuildEntropyCodes(mb.distance_histograms, num_distance_codes,
                    &distance_codes);
  StoreHuffmanCodes(literal_codes, 256, storage_ix, storage);
  StoreHuffmanCodes(command_codes, kNumCommandPrefixes, storage_ix, storage);
  StoreHuffmanCodes(distance_codes, num_distance_codes, storage_ix, storage);
  BlockSplitIterator literal_it(mb.literal_split);
  BlockSplitIterator command_it(mb.command_split);
  BlockSplitIterator distance_it(mb.distance_split);
  for (int i = 0; i < mb.cmds.size(); ++i) {
    const Command& cmd = mb.cmds[i];
    MoveAndEncode(command_split_code, &command_it, storage_ix, storage);
    EncodeCommand(cmd, command_codes[command_it.type_], storage_ix, storage);
    for (int j = 0; j < cmd.insert_length_; ++j) {
      MoveAndEncode(literal_split_code, &literal_it, storage_ix, storage);
      int histogram_idx = literal_it.type_;
      if (mb.params.literal_context_mode > 0) {
        uint8_t prev_byte = *pos > 0 ? input_buffer[*pos - 1] : 0;
        uint8_t prev_byte2 = *pos > 1 ? input_buffer[*pos - 2] : 0;
        uint8_t prev_byte3 = *pos > 2 ? input_buffer[*pos - 3] : 0;
        int context = (literal_it.type_ *
                       NumContexts(mb.params.literal_context_mode) +
                       Context(prev_byte, prev_byte2, prev_byte3,
                               mb.params.literal_context_mode));
        histogram_idx = mb.literal_context_map[context];
      }
      EntropyEncode(input_buffer[(*pos)++],
                    literal_codes[histogram_idx], storage_ix, storage);
    }
    if (*pos < end_pos && cmd.distance_prefix_ != 0xffff) {
      MoveAndEncode(distance_split_code, &distance_it, storage_ix, storage);
      int histogram_index = distance_it.type_;
      if (mb.params.distance_context_mode > 0) {
        int context = distance_it.type_ << 2;
        context += (cmd.copy_length_ > 4) ? 3 : cmd.copy_length_ - 2;
        histogram_index = mb.distance_context_map[context];
      }
      EncodeCopyDistance(cmd, distance_codes[histogram_index],
                         storage_ix, storage);
    }
    *pos += cmd.copy_length_;
  }
}

int BrotliCompressBuffer(size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer) {
  int storage_ix = 0;
  uint8_t* storage = encoded_buffer;
  WriteBitsPrepareStorage(storage_ix, storage);
  EncodeSize(input_size, &storage_ix, storage);

  if (input_size == 0) {
    *encoded_size = (storage_ix + 7) >> 3;
    return 1;
  }
  int input_size_bits = Log2Ceiling(input_size);

  std::vector<Command> all_commands;
  CreateBackwardReferences(input_buffer, input_size, &all_commands);
  ComputeDistanceShortCodes(&all_commands);

  std::vector<std::vector<Command> > meta_block_commands;
  SplitBlockByTotalLength(all_commands, input_size, 2 << 20,
                          &meta_block_commands);

  size_t pos = 0;
  for (int block_idx = 0; block_idx < meta_block_commands.size(); ++block_idx) {
    const std::vector<Command>& commands = meta_block_commands[block_idx];
    bool is_last_meta_block = (block_idx + 1 == meta_block_commands.size());
    EncodingParams params;
    params.num_direct_distance_codes = 12;
    params.distance_postfix_bits = 1;
    params.literal_context_mode = CONTEXT_SIGNED_MIXED_3BYTE;
    params.distance_context_mode = 1;
    MetaBlock mb;
    BuildMetaBlock(params, commands, input_buffer, pos, &mb);
    StoreMetaBlock(mb, input_buffer, input_size_bits, is_last_meta_block,
                   &pos, &storage_ix, storage);
  }

  *encoded_size = (storage_ix + 7) >> 3;
  return 1;
}

}  // namespace brotli
