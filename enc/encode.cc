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

void EncodeVarLenUint8(int n, int* storage_ix, uint8_t* storage) {
  if (n == 0) {
    WriteBits(1, 0, storage_ix, storage);
  } else {
    WriteBits(1, 1, storage_ix, storage);
    int nbits = Log2Floor(n);
    WriteBits(3, nbits, storage_ix, storage);
    if (nbits > 0) {
      WriteBits(nbits, n - (1 << nbits), storage_ix, storage);
    }
  }
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

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const uint8_t* code_length_bitdepth,
    int* storage_ix, uint8_t* storage) {
  static const uint8_t kStorageOrder[kCodeLengthCodes] = {
    1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  };
  // Throw away trailing zeros:
  int codes_to_store = kCodeLengthCodes;
  for (; codes_to_store > 0; --codes_to_store) {
    if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
      break;
    }
  }
  int num_codes = 0;
  for (int i = 0; i < codes_to_store; ++i) {
    if (code_length_bitdepth[kStorageOrder[i]] != 0) {
      ++num_codes;
    }
  }
  if (num_codes == 1) {
    codes_to_store = kCodeLengthCodes;
  }
  int skip_some = 0;  // skips none.
  if (code_length_bitdepth[kStorageOrder[0]] == 0 &&
      code_length_bitdepth[kStorageOrder[1]] == 0) {
    skip_some = 2;  // skips two.
    if (code_length_bitdepth[kStorageOrder[2]] == 0) {
      skip_some = 3;  // skips three.
    }
  }
  WriteBits(2, skip_some, storage_ix, storage);
  for (int i = skip_some; i < codes_to_store; ++i) {
    uint8_t len[] = { 2, 4, 3, 2, 2, 4 };
    uint8_t bits[] = { 0, 7, 3, 2, 1, 15 };
    int v = code_length_bitdepth[kStorageOrder[i]];
    WriteBits(len[v], bits[v], storage_ix, storage);
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
    if (entropy.count_ > 1) {
      WriteBits(entropy.depth_[ix], entropy.bits_[ix], storage_ix, storage);
    }
    switch (ix) {
      case 16:
        WriteBits(2, extra_bits, storage_ix, storage);
        break;
      case 17:
        WriteBits(3, extra_bits, storage_ix, storage);
        break;
    }
  }
}

template<int kSize>
void StoreHuffmanCodeSimple(
    const EntropyCode<kSize>& code, int alphabet_size,
    int max_bits, int* storage_ix, uint8_t* storage) {
  const uint8_t *depth = &code.depth_[0];
  int symbols[4];
  // Quadratic sort.
  int k, j;
  for (k = 0; k < code.count_; ++k) {
    symbols[k] = code.symbols_[k];
  }
  for (k = 0; k < code.count_; ++k) {
    for (j = k + 1; j < code.count_; ++j) {
      if (depth[symbols[j]] < depth[symbols[k]]) {
        int t = symbols[k];
        symbols[k] = symbols[j];
        symbols[j] = t;
      }
    }
  }
  // Small tree marker to encode 1-4 symbols.
  WriteBits(2, 1, storage_ix, storage);
  WriteBits(2, code.count_ - 1, storage_ix, storage);
  for (int i = 0; i < code.count_; ++i) {
    WriteBits(max_bits, symbols[i], storage_ix, storage);
  }
  if (code.count_ == 4) {
    if (depth[symbols[0]] == 2 &&
        depth[symbols[1]] == 2 &&
        depth[symbols[2]] == 2 &&
        depth[symbols[3]] == 2) {
      WriteBits(1, 0, storage_ix, storage);
    } else {
      WriteBits(1, 1, storage_ix, storage);
    }
  }
}

template<int kSize>
void StoreHuffmanCodeComplex(
    const EntropyCode<kSize>& code, int alphabet_size,
    int* storage_ix, uint8_t* storage) {
  const uint8_t *depth = &code.depth_[0];
  uint8_t huffman_tree[kSize];
  uint8_t huffman_tree_extra_bits[kSize];
  int huffman_tree_size = 0;
  WriteHuffmanTree(depth,
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
  BuildEntropyCode(huffman_tree_histogram, 5, kCodeLengthCodes,
                   &huffman_tree_entropy);
  StoreHuffmanTreeOfHuffmanTreeToBitMask(
      &huffman_tree_entropy.depth_[0], storage_ix, storage);
  StoreHuffmanTreeToBitMask(&huffman_tree[0], &huffman_tree_extra_bits[0],
                            huffman_tree_size, huffman_tree_entropy,
                            storage_ix, storage);
}

template<int kSize>
void BuildAndStoreEntropyCode(const Histogram<kSize>& histogram,
                              const int tree_limit,
                              const int alphabet_size,
                              EntropyCode<kSize>* code,
                              int* storage_ix, uint8_t* storage) {
  memset(code->depth_, 0, sizeof(code->depth_));
  memset(code->bits_, 0, sizeof(code->bits_));
  memset(code->symbols_, 0, sizeof(code->symbols_));
  code->count_ = 0;

  int max_bits_counter = alphabet_size - 1;
  int max_bits = 0;
  while (max_bits_counter) {
    max_bits_counter >>= 1;
    ++max_bits;
  }

  for (size_t i = 0; i < alphabet_size; i++) {
    if (histogram.data_[i] > 0) {
      if (code->count_ < 4) code->symbols_[code->count_] = i;
      ++code->count_;
    }
  }

  if (code->count_ <= 1) {
    WriteBits(2, 1, storage_ix, storage);
    WriteBits(2, 0, storage_ix, storage);
    WriteBits(max_bits, code->symbols_[0], storage_ix, storage);
    return;
  }

  if (alphabet_size >= 50 && code->count_ >= 16) {
    std::vector<int> counts(alphabet_size);
    memcpy(&counts[0], histogram.data_, sizeof(counts[0]) * alphabet_size);
    OptimizeHuffmanCountsForRle(alphabet_size, &counts[0]);
    CreateHuffmanTree(&counts[0], alphabet_size, tree_limit, code->depth_);
  } else {
    CreateHuffmanTree(histogram.data_, alphabet_size, tree_limit, code->depth_);
  }
  ConvertBitDepthsToSymbols(code->depth_, alphabet_size, code->bits_);

  if (code->count_ <= 4) {
    StoreHuffmanCodeSimple(*code, alphabet_size, max_bits, storage_ix, storage);
  } else {
    StoreHuffmanCodeComplex(*code, alphabet_size, storage_ix, storage);
  }
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
  int code = cmd.command_prefix_;
  WriteBits(entropy.depth_[code], entropy.bits_[code], storage_ix, storage);
  if (code >= 128) {
    code -= 128;
  }
  int insert_extra_bits = InsertLengthExtraBits(code);
  uint64_t insert_extra_bits_val =
      cmd.insert_length_ - InsertLengthOffset(code);
  int copy_extra_bits = CopyLengthExtraBits(code);
  uint64_t copy_extra_bits_val = cmd.copy_length_code_ - CopyLengthOffset(code);
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
  WriteBits(entropy.depth_[code], entropy.bits_[code], storage_ix, storage);
  if (extra_bits > 0) {
    WriteBits(extra_bits, extra_bits_val, storage_ix, storage);
  }
}

void ComputeDistanceShortCodes(std::vector<Command>* cmds,
                               size_t pos,
                               const size_t max_backward,
                               int* dist_ringbuffer,
                               size_t* ringbuffer_idx) {
  static const int kIndexOffset[16] = {
    3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
  };
  static const int kValueOffset[16] = {
    0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
  };
  for (int i = 0; i < cmds->size(); ++i) {
    pos += (*cmds)[i].insert_length_;
    size_t max_distance = std::min(pos, max_backward);
    int cur_dist = (*cmds)[i].copy_distance_;
    int dist_code = cur_dist + 16;
    if (cur_dist <= max_distance) {
      if (cur_dist == 0) break;
      int limits[16] = { 0, 0, 0, 0,
                         6, 6, 11, 11,
                         11, 11, 11, 11,
                         12, 12, 12, 12 };
      for (int k = 0; k < 16; ++k) {
        // Only accept more popular choices.
        if (cur_dist < limits[k]) {
          // Typically unpopular ranges, don't replace a short distance
          // with them.
          continue;
        }
        int comp = (dist_ringbuffer[(*ringbuffer_idx + kIndexOffset[k]) & 3] +
                    kValueOffset[k]);
        if (cur_dist == comp) {
          dist_code = k + 1;
          break;
        }
      }
      if (dist_code > 1) {
        dist_ringbuffer[*ringbuffer_idx & 3] = cur_dist;
        ++(*ringbuffer_idx);
      }
      pos += (*cmds)[i].copy_length_;
    } else {
      int word_idx = cur_dist - max_distance - 1;
      const std::string word =
          GetTransformedDictionaryWord((*cmds)[i].copy_length_code_, word_idx);
      pos += word.size();
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
                                         cmd->copy_length_code_);
    if (cmd->copy_length_code_ > 0) {
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
    HistogramContextMap histogram;
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
                      int num_clusters,
                      int* storage_ix, uint8_t* storage) {
  EncodeVarLenUint8(num_clusters - 1, storage_ix, storage);

  if (num_clusters == 1) {
    return;
  }

  std::vector<int> transformed_symbols = MoveToFrontTransform(context_map);
  std::vector<int> rle_symbols;
  std::vector<int> extra_bits;
  int max_run_length_prefix = BestMaxZeroRunLengthPrefix(transformed_symbols);
  RunLengthCodeZeros(transformed_symbols, &max_run_length_prefix,
                     &rle_symbols, &extra_bits);
  HistogramContextMap symbol_histogram;
  for (int i = 0; i < rle_symbols.size(); ++i) {
    symbol_histogram.Add(rle_symbols[i]);
  }
  bool use_rle = max_run_length_prefix > 0;
  WriteBits(1, use_rle, storage_ix, storage);
  if (use_rle) {
    WriteBits(4, max_run_length_prefix - 1, storage_ix, storage);
  }
  EntropyCodeContextMap symbol_code;
  BuildAndStoreEntropyCode(symbol_histogram, 15,
                           num_clusters + max_run_length_prefix,
                           &symbol_code,
                           storage_ix, storage);
  for (int i = 0; i < rle_symbols.size(); ++i) {
    WriteBits(symbol_code.depth_[rle_symbols[i]],
              symbol_code.bits_[rle_symbols[i]],
              storage_ix, storage);
    if (rle_symbols[i] > 0 && rle_symbols[i] <= max_run_length_prefix) {
      WriteBits(rle_symbols[i], extra_bits[i], storage_ix, storage);
    }
  }
  WriteBits(1, 1, storage_ix, storage);  // use move-to-front
}

struct BlockSplitCode {
  EntropyCodeBlockType block_type_code;
  EntropyCodeBlockLength block_len_code;
};

void EncodeBlockLength(const EntropyCodeBlockLength& entropy,
                       int length,
                       int* storage_ix, uint8_t* storage) {
  int len_code = BlockLengthPrefix(length);
  int extra_bits = BlockLengthExtraBits(len_code);
  int extra_bits_value = length - BlockLengthOffset(len_code);
  WriteBits(entropy.depth_[len_code], entropy.bits_[len_code],
            storage_ix, storage);
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
  EncodeVarLenUint8(split.num_types_ - 1, storage_ix, storage);

  if (split.num_types_ == 1) {
    return;
  }

  HistogramBlockType type_histo;
  for (int i = 1; i < split.type_codes_.size(); ++i) {
    type_histo.Add(split.type_codes_[i]);
  }
  HistogramBlockLength length_histo;
  for (int i = 0; i < split.lengths_.size(); ++i) {
    length_histo.Add(BlockLengthPrefix(split.lengths_[i]));
  }
  BuildAndStoreEntropyCode(type_histo, 15, split.num_types_ + 2,
                           &code->block_type_code,
                           storage_ix, storage);
  BuildAndStoreEntropyCode(length_histo, 15, kNumBlockLenPrefixes,
                           &code->block_len_code,
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
    int type_code = it->split_.type_codes_[it->idx_];
    WriteBits(code.block_type_code.depth_[type_code],
              code.block_type_code.bits_[type_code],
              storage_ix, storage);
    EncodeBlockLength(code.block_len_code, it->length_, storage_ix, storage);
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
  ComputeCommandPrefixes(&mb->cmds,
                         mb->params.num_direct_distance_codes,
                         mb->params.distance_postfix_bits);
  SplitBlock(mb->cmds,
             &ringbuffer[pos & mask],
             &mb->literal_split,
             &mb->command_split,
             &mb->distance_split);
  ComputeBlockTypeShortCodes(&mb->literal_split);
  ComputeBlockTypeShortCodes(&mb->command_split);
  ComputeBlockTypeShortCodes(&mb->distance_split);

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
    length += cmd.insert_length_ + cmd.copy_length_;
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
  BuildAndEncodeBlockSplitCode(mb.literal_split, &literal_split_code,
                               storage_ix, storage);
  BuildAndEncodeBlockSplitCode(mb.command_split, &command_split_code,
                               storage_ix, storage);
  BuildAndEncodeBlockSplitCode(mb.distance_split, &distance_split_code,
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
    for (int j = 0; j < cmd.insert_length_; ++j) {
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
    if (*pos < end_pos && cmd.distance_prefix_ != 0xffff) {
      MoveAndEncode(distance_split_code, &distance_it, storage_ix, storage);
      int context = (distance_it.type_ << 2) +
          ((cmd.copy_length_code_ > 4) ? 3 : cmd.copy_length_code_ - 2);
      int histogram_index = mb.distance_context_map[context];
      size_t max_distance = std::min(*pos, (size_t)kMaxBackwardDistance);
      EncodeCopyDistance(cmd, distance_codes[histogram_index],
                         storage_ix, storage);
    }
    *pos += cmd.copy_length_;
  }
}

BrotliCompressor::BrotliCompressor(BrotliParams params)
    : params_(params),
      window_bits_(kWindowBits),
      hashers_(new Hashers()),
      dist_ringbuffer_idx_(0),
      input_pos_(0),
      ringbuffer_(kRingBufferBits, kMetaBlockSizeBits),
      literal_cost_(1 << kRingBufferBits),
      storage_ix_(0),
      storage_(new uint8_t[2 << kMetaBlockSizeBits]) {
  dist_ringbuffer_[0] = 16;
  dist_ringbuffer_[1] = 15;
  dist_ringbuffer_[2] = 11;
  dist_ringbuffer_[3] = 4;
  storage_[0] = 0;
  switch (params.mode) {
    case BrotliParams::MODE_TEXT: hash_type_ = Hashers::HASH_15_8_4; break;
    case BrotliParams::MODE_FONT: hash_type_ = Hashers::HASH_15_8_2; break;
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
  std::vector<Command> commands;
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
    CreateBackwardReferences(
        input_size, input_pos_,
        ringbuffer_.start(),
        &literal_cost_[0],
        kRingBufferMask, kMaxBackwardDistance,
        hashers_.get(),
        hash_type_,
        &commands);
    ComputeDistanceShortCodes(&commands, input_pos_, kMaxBackwardDistance,
                              dist_ringbuffer_,
                              &dist_ringbuffer_idx_);
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
