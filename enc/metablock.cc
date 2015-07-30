// Copyright 2015 Google Inc. All Rights Reserved.
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
// Algorithms for distributing the literals and commands of a metablock between
// block types and contexts.

#include "./metablock.h"

#include "./block_splitter.h"
#include "./context.h"
#include "./cluster.h"
#include "./histogram.h"

namespace brotli {

void BuildMetaBlock(const uint8_t* ringbuffer,
                    const size_t pos,
                    const size_t mask,
                    uint8_t prev_byte,
                    uint8_t prev_byte2,
                    const Command* cmds,
                    size_t num_commands,
                    int literal_context_mode,
                    MetaBlockSplit* mb) {
  SplitBlock(cmds, num_commands,
             ringbuffer, pos, mask,
             &mb->literal_split,
             &mb->command_split,
             &mb->distance_split);

  std::vector<int> literal_context_modes(mb->literal_split.num_types,
                                         literal_context_mode);

  int num_literal_contexts =
      mb->literal_split.num_types << kLiteralContextBits;
  int num_distance_contexts =
      mb->distance_split.num_types << kDistanceContextBits;
  std::vector<HistogramLiteral> literal_histograms(num_literal_contexts);
  mb->command_histograms.resize(mb->command_split.num_types);
  std::vector<HistogramDistance> distance_histograms(num_distance_contexts);
  BuildHistograms(cmds, num_commands,
                  mb->literal_split,
                  mb->command_split,
                  mb->distance_split,
                  ringbuffer,
                  pos,
                  mask,
                  prev_byte,
                  prev_byte2,
                  literal_context_modes,
                  &literal_histograms,
                  &mb->command_histograms,
                  &distance_histograms);

  // Histogram ids need to fit in one byte.
  static const int kMaxNumberOfHistograms = 256;

  mb->literal_histograms = literal_histograms;
  ClusterHistograms(literal_histograms,
                    1 << kLiteralContextBits,
                    mb->literal_split.num_types,
                    kMaxNumberOfHistograms,
                    &mb->literal_histograms,
                    &mb->literal_context_map);

  mb->distance_histograms = distance_histograms;
  ClusterHistograms(distance_histograms,
                    1 << kDistanceContextBits,
                    mb->distance_split.num_types,
                    kMaxNumberOfHistograms,
                    &mb->distance_histograms,
                    &mb->distance_context_map);
}

// Greedy block splitter for one block category (literal, command or distance).
template<typename HistogramType>
class BlockSplitter {
 public:
  BlockSplitter(int alphabet_size,
                int min_block_size,
                double split_threshold,
                int num_symbols,
                BlockSplit* split,
                std::vector<HistogramType>* histograms)
      : alphabet_size_(alphabet_size),
        min_block_size_(min_block_size),
        split_threshold_(split_threshold),
        num_blocks_(0),
        split_(split),
        histograms_(histograms),
        target_block_size_(min_block_size),
        block_size_(0),
        curr_histogram_ix_(0),
        merge_last_count_(0) {
    int max_num_blocks = num_symbols / min_block_size + 1;
    // We have to allocate one more histogram than the maximum number of block
    // types for the current histogram when the meta-block is too big.
    int max_num_types = std::min(max_num_blocks, kMaxBlockTypes + 1);
    split_->lengths.resize(max_num_blocks);
    split_->types.resize(max_num_blocks);
    histograms_->resize(max_num_types);
    last_histogram_ix_[0] = last_histogram_ix_[1] = 0;
  }

  // Adds the next symbol to the current histogram. When the current histogram
  // reaches the target size, decides on merging the block.
  void AddSymbol(int symbol) {
    (*histograms_)[curr_histogram_ix_].Add(symbol);
    ++block_size_;
    if (block_size_ == target_block_size_) {
      FinishBlock(/* is_final = */ false);
    }
  }

  // Does either of three things:
  //   (1) emits the current block with a new block type;
  //   (2) emits the current block with the type of the second last block;
  //   (3) merges the current block with the last block.
  void FinishBlock(bool is_final) {
    if (block_size_ < min_block_size_) {
      block_size_ = min_block_size_;
    }
    if (num_blocks_ == 0) {
      // Create first block.
      split_->lengths[0] = block_size_;
      split_->types[0] = 0;
      last_entropy_[0] =
          BitsEntropy(&(*histograms_)[0].data_[0], alphabet_size_);
      last_entropy_[1] = last_entropy_[0];
      ++num_blocks_;
      ++split_->num_types;
      ++curr_histogram_ix_;
      block_size_ = 0;
    } else if (block_size_ > 0) {
      double entropy = BitsEntropy(&(*histograms_)[curr_histogram_ix_].data_[0],
                                   alphabet_size_);
      HistogramType combined_histo[2];
      double combined_entropy[2];
      double diff[2];
      for (int j = 0; j < 2; ++j) {
        int last_histogram_ix = last_histogram_ix_[j];
        combined_histo[j] = (*histograms_)[curr_histogram_ix_];
        combined_histo[j].AddHistogram((*histograms_)[last_histogram_ix]);
        combined_entropy[j] = BitsEntropy(
            &combined_histo[j].data_[0], alphabet_size_);
        diff[j] = combined_entropy[j] - entropy - last_entropy_[j];
      }

      if (split_->num_types < kMaxBlockTypes &&
          diff[0] > split_threshold_ &&
          diff[1] > split_threshold_) {
        // Create new block.
        split_->lengths[num_blocks_] = block_size_;
        split_->types[num_blocks_] = split_->num_types;
        last_histogram_ix_[1] = last_histogram_ix_[0];
        last_histogram_ix_[0] = split_->num_types;
        last_entropy_[1] = last_entropy_[0];
        last_entropy_[0] = entropy;
        ++num_blocks_;
        ++split_->num_types;
        ++curr_histogram_ix_;
        block_size_ = 0;
        merge_last_count_ = 0;
        target_block_size_ = min_block_size_;
      } else if (diff[1] < diff[0] - 20.0) {
        // Combine this block with second last block.
        split_->lengths[num_blocks_] = block_size_;
        split_->types[num_blocks_] = split_->types[num_blocks_ - 2];
        std::swap(last_histogram_ix_[0], last_histogram_ix_[1]);
        (*histograms_)[last_histogram_ix_[0]] = combined_histo[1];
        last_entropy_[1] = last_entropy_[0];
        last_entropy_[0] = combined_entropy[1];
        ++num_blocks_;
        block_size_ = 0;
        (*histograms_)[curr_histogram_ix_].Clear();
        merge_last_count_ = 0;
        target_block_size_ = min_block_size_;
      } else {
        // Combine this block with last block.
        split_->lengths[num_blocks_ - 1] += block_size_;
        (*histograms_)[last_histogram_ix_[0]] = combined_histo[0];
        last_entropy_[0] = combined_entropy[0];
        if (split_->num_types == 1) {
          last_entropy_[1] = last_entropy_[0];
        }
        block_size_ = 0;
        (*histograms_)[curr_histogram_ix_].Clear();
        if (++merge_last_count_ > 1) {
          target_block_size_ += min_block_size_;
        }
      }
    }
    if (is_final) {
      (*histograms_).resize(split_->num_types);
      split_->types.resize(num_blocks_);
      split_->lengths.resize(num_blocks_);
    }
  }

 private:
  static const int kMaxBlockTypes = 256;

  // Alphabet size of particular block category.
  const int alphabet_size_;
  // We collect at least this many symbols for each block.
  const int min_block_size_;
  // We merge histograms A and B if
  //   entropy(A+B) < entropy(A) + entropy(B) + split_threshold_,
  // where A is the current histogram and B is the histogram of the last or the
  // second last block type.
  const double split_threshold_;

  int num_blocks_;
  BlockSplit* split_;  // not owned
  std::vector<HistogramType>* histograms_;  // not owned

  // The number of symbols that we want to collect before deciding on whether
  // or not to merge the block with a previous one or emit a new block.
  int target_block_size_;
  // The number of symbols in the current histogram.
  int block_size_;
  // Offset of the current histogram.
  int curr_histogram_ix_;
  // Offset of the histograms of the previous two block types.
  int last_histogram_ix_[2];
  // Entropy of the previous two block types.
  double last_entropy_[2];
  // The number of times we merged the current block with the last one.
  int merge_last_count_;
};

void BuildMetaBlockGreedy(const uint8_t* ringbuffer,
                          size_t pos,
                          size_t mask,
                          const Command *commands,
                          size_t n_commands,
                          MetaBlockSplit* mb) {
  int num_literals = 0;
  for (int i = 0; i < n_commands; ++i) {
    num_literals += commands[i].insert_len_;
  }

  BlockSplitter<HistogramLiteral> lit_blocks(
      256, 512, 400.0, num_literals,
      &mb->literal_split, &mb->literal_histograms);
  BlockSplitter<HistogramCommand> cmd_blocks(
      kNumCommandPrefixes, 1024, 500.0, n_commands,
      &mb->command_split, &mb->command_histograms);
  BlockSplitter<HistogramDistance> dist_blocks(
      64, 512, 100.0, n_commands,
      &mb->distance_split, &mb->distance_histograms);

  for (int i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    cmd_blocks.AddSymbol(cmd.cmd_prefix_);
    for (int j = 0; j < cmd.insert_len_; ++j) {
      lit_blocks.AddSymbol(ringbuffer[pos & mask]);
      ++pos;
    }
    pos += cmd.copy_len_;
    if (cmd.copy_len_ > 0 && cmd.cmd_prefix_ >= 128) {
      dist_blocks.AddSymbol(cmd.dist_prefix_);
    }
  }

  lit_blocks.FinishBlock(/* is_final = */ true);
  cmd_blocks.FinishBlock(/* is_final = */ true);
  dist_blocks.FinishBlock(/* is_final = */ true);
}

// Greedy block splitter for one block category (literal, command or distance).
// Gathers histograms for all context buckets.
template<typename HistogramType>
class ContextBlockSplitter {
 public:
  ContextBlockSplitter(int alphabet_size,
                       int num_contexts,
                       int min_block_size,
                       double split_threshold,
                       int num_symbols,
                       BlockSplit* split,
                       std::vector<HistogramType>* histograms)
      : alphabet_size_(alphabet_size),
        num_contexts_(num_contexts),
        max_block_types_(kMaxBlockTypes / num_contexts),
        min_block_size_(min_block_size),
        split_threshold_(split_threshold),
        num_blocks_(0),
        split_(split),
        histograms_(histograms),
        target_block_size_(min_block_size),
        block_size_(0),
        curr_histogram_ix_(0),
        last_entropy_(2 * num_contexts),
        merge_last_count_(0) {
    int max_num_blocks = num_symbols / min_block_size + 1;
    // We have to allocate one more histogram than the maximum number of block
    // types for the current histogram when the meta-block is too big.
    int max_num_types = std::min(max_num_blocks, max_block_types_ + 1);
    split_->lengths.resize(max_num_blocks);
    split_->types.resize(max_num_blocks);
    histograms_->resize(max_num_types * num_contexts);
    last_histogram_ix_[0] = last_histogram_ix_[1] = 0;
  }

  // Adds the next symbol to the current block type and context. When the
  // current block reaches the target size, decides on merging the block.
  void AddSymbol(int symbol, int context) {
    (*histograms_)[curr_histogram_ix_ + context].Add(symbol);
    ++block_size_;
    if (block_size_ == target_block_size_) {
      FinishBlock(/* is_final = */ false);
    }
  }

  // Does either of three things:
  //   (1) emits the current block with a new block type;
  //   (2) emits the current block with the type of the second last block;
  //   (3) merges the current block with the last block.
  void FinishBlock(bool is_final) {
    if (block_size_ < min_block_size_) {
      block_size_ = min_block_size_;
    }
    if (num_blocks_ == 0) {
      // Create first block.
      split_->lengths[0] = block_size_;
      split_->types[0] = 0;
      for (int i = 0; i < num_contexts_; ++i) {
        last_entropy_[i] =
            BitsEntropy(&(*histograms_)[i].data_[0], alphabet_size_);
        last_entropy_[num_contexts_ + i] = last_entropy_[i];
      }
      ++num_blocks_;
      ++split_->num_types;
      curr_histogram_ix_ += num_contexts_;
      block_size_ = 0;
    } else if (block_size_ > 0) {
      // Try merging the set of histograms for the current block type with the
      // respective set of histograms for the last and second last block types.
      // Decide over the split based on the total reduction of entropy across
      // all contexts.
      std::vector<double> entropy(num_contexts_);
      std::vector<HistogramType> combined_histo(2 * num_contexts_);
      std::vector<double> combined_entropy(2 * num_contexts_);
      double diff[2] = { 0.0 };
      for (int i = 0; i < num_contexts_; ++i) {
        int curr_histo_ix = curr_histogram_ix_ + i;
        entropy[i] = BitsEntropy(&(*histograms_)[curr_histo_ix].data_[0],
                                 alphabet_size_);
        for (int j = 0; j < 2; ++j) {
          int jx = j * num_contexts_ + i;
          int last_histogram_ix = last_histogram_ix_[j] + i;
          combined_histo[jx] = (*histograms_)[curr_histo_ix];
          combined_histo[jx].AddHistogram((*histograms_)[last_histogram_ix]);
          combined_entropy[jx] = BitsEntropy(
              &combined_histo[jx].data_[0], alphabet_size_);
          diff[j] += combined_entropy[jx] - entropy[i] - last_entropy_[jx];
        }
      }

      if (split_->num_types < max_block_types_ &&
          diff[0] > split_threshold_ &&
          diff[1] > split_threshold_) {
        // Create new block.
        split_->lengths[num_blocks_] = block_size_;
        split_->types[num_blocks_] = split_->num_types;
        last_histogram_ix_[1] = last_histogram_ix_[0];
        last_histogram_ix_[0] = split_->num_types * num_contexts_;
        for (int i = 0; i < num_contexts_; ++i) {
          last_entropy_[num_contexts_ + i] = last_entropy_[i];
          last_entropy_[i] = entropy[i];
        }
        ++num_blocks_;
        ++split_->num_types;
        curr_histogram_ix_ += num_contexts_;
        block_size_ = 0;
        merge_last_count_ = 0;
        target_block_size_ = min_block_size_;
      } else if (diff[1] < diff[0] - 20.0) {
        // Combine this block with second last block.
        split_->lengths[num_blocks_] = block_size_;
        split_->types[num_blocks_] = split_->types[num_blocks_ - 2];
        std::swap(last_histogram_ix_[0], last_histogram_ix_[1]);
        for (int i = 0; i < num_contexts_; ++i) {
          (*histograms_)[last_histogram_ix_[0] + i] =
              combined_histo[num_contexts_ + i];
          last_entropy_[num_contexts_ + i] = last_entropy_[i];
          last_entropy_[i] = combined_entropy[num_contexts_ + i];
          (*histograms_)[curr_histogram_ix_ + i].Clear();
        }
        ++num_blocks_;
        block_size_ = 0;
        merge_last_count_ = 0;
        target_block_size_ = min_block_size_;
      } else {
        // Combine this block with last block.
        split_->lengths[num_blocks_ - 1] += block_size_;
        for (int i = 0; i < num_contexts_; ++i) {
          (*histograms_)[last_histogram_ix_[0] + i] = combined_histo[i];
          last_entropy_[i] = combined_entropy[i];
          if (split_->num_types == 1) {
            last_entropy_[num_contexts_ + i] = last_entropy_[i];
          }
          (*histograms_)[curr_histogram_ix_ + i].Clear();
        }
        block_size_ = 0;
        if (++merge_last_count_ > 1) {
          target_block_size_ += min_block_size_;
        }
      }
    }
    if (is_final) {
      (*histograms_).resize(split_->num_types * num_contexts_);
      split_->types.resize(num_blocks_);
      split_->lengths.resize(num_blocks_);
    }
  }

 private:
  static const int kMaxBlockTypes = 256;

  // Alphabet size of particular block category.
  const int alphabet_size_;
  const int num_contexts_;
  const int max_block_types_;
  // We collect at least this many symbols for each block.
  const int min_block_size_;
  // We merge histograms A and B if
  //   entropy(A+B) < entropy(A) + entropy(B) + split_threshold_,
  // where A is the current histogram and B is the histogram of the last or the
  // second last block type.
  const double split_threshold_;

  int num_blocks_;
  BlockSplit* split_;  // not owned
  std::vector<HistogramType>* histograms_;  // not owned

  // The number of symbols that we want to collect before deciding on whether
  // or not to merge the block with a previous one or emit a new block.
  int target_block_size_;
  // The number of symbols in the current histogram.
  int block_size_;
  // Offset of the current histogram.
  int curr_histogram_ix_;
  // Offset of the histograms of the previous two block types.
  int last_histogram_ix_[2];
  // Entropy of the previous two block types.
  std::vector<double> last_entropy_;
  // The number of times we merged the current block with the last one.
  int merge_last_count_;
};

void BuildMetaBlockGreedyWithContexts(const uint8_t* ringbuffer,
                                      size_t pos,
                                      size_t mask,
                                      uint8_t prev_byte,
                                      uint8_t prev_byte2,
                                      int literal_context_mode,
                                      int num_contexts,
                                      const int* static_context_map,
                                      const Command *commands,
                                      size_t n_commands,
                                      MetaBlockSplit* mb) {
  int num_literals = 0;
  for (int i = 0; i < n_commands; ++i) {
    num_literals += commands[i].insert_len_;
  }

  ContextBlockSplitter<HistogramLiteral> lit_blocks(
      256, num_contexts, 512, 400.0, num_literals,
      &mb->literal_split, &mb->literal_histograms);
  BlockSplitter<HistogramCommand> cmd_blocks(
      kNumCommandPrefixes, 1024, 500.0, n_commands,
      &mb->command_split, &mb->command_histograms);
  BlockSplitter<HistogramDistance> dist_blocks(
      64, 512, 100.0, n_commands,
      &mb->distance_split, &mb->distance_histograms);

  for (int i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    cmd_blocks.AddSymbol(cmd.cmd_prefix_);
    for (int j = 0; j < cmd.insert_len_; ++j) {
      int context = Context(prev_byte, prev_byte2, literal_context_mode);
      uint8_t literal = ringbuffer[pos & mask];
      lit_blocks.AddSymbol(literal, static_context_map[context]);
      prev_byte2 = prev_byte;
      prev_byte = literal;
      ++pos;
    }
    pos += cmd.copy_len_;
    if (cmd.copy_len_ > 0) {
      prev_byte2 = ringbuffer[(pos - 2) & mask];
      prev_byte = ringbuffer[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        dist_blocks.AddSymbol(cmd.dist_prefix_);
      }
    }
  }

  lit_blocks.FinishBlock(/* is_final = */ true);
  cmd_blocks.FinishBlock(/* is_final = */ true);
  dist_blocks.FinishBlock(/* is_final = */ true);

  mb->literal_context_map.resize(
      mb->literal_split.num_types << kLiteralContextBits);
  for (int i = 0; i < mb->literal_split.num_types; ++i) {
    for (int j = 0; j < (1 << kLiteralContextBits); ++j) {
      mb->literal_context_map[(i << kLiteralContextBits) + j] =
          i * num_contexts + static_context_map[j];
    }
  }
}

void OptimizeHistograms(int num_direct_distance_codes,
                        int distance_postfix_bits,
                        MetaBlockSplit* mb) {
  for (int i = 0; i < mb->literal_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(256, &mb->literal_histograms[i].data_[0]);
  }
  for (int i = 0; i < mb->command_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(kNumCommandPrefixes,
                                &mb->command_histograms[i].data_[0]);
  }
  int num_distance_codes =
      kNumDistanceShortCodes + num_direct_distance_codes +
      (48 << distance_postfix_bits);
  for (int i = 0; i < mb->distance_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(num_distance_codes,
                                &mb->distance_histograms[i].data_[0]);
  }
}

}  // namespace brotli
