/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

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
                    ContextType literal_context_mode,
                    MetaBlockSplit* mb) {
  SplitBlock(cmds, num_commands,
             ringbuffer, pos, mask,
             &mb->literal_split,
             &mb->command_split,
             &mb->distance_split);

  std::vector<ContextType> literal_context_modes(mb->literal_split.num_types,
                                                 literal_context_mode);

  size_t num_literal_contexts =
      mb->literal_split.num_types << kLiteralContextBits;
  size_t num_distance_contexts =
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
  static const size_t kMaxNumberOfHistograms = 256;

  ClusterHistograms(literal_histograms,
                    1u << kLiteralContextBits,
                    mb->literal_split.num_types,
                    kMaxNumberOfHistograms,
                    &mb->literal_histograms,
                    &mb->literal_context_map);

  ClusterHistograms(distance_histograms,
                    1u << kDistanceContextBits,
                    mb->distance_split.num_types,
                    kMaxNumberOfHistograms,
                    &mb->distance_histograms,
                    &mb->distance_context_map);
}

// Greedy block splitter for one block category (literal, command or distance).
template<typename HistogramType>
class BlockSplitter {
 public:
  BlockSplitter(size_t alphabet_size,
                size_t min_block_size,
                double split_threshold,
                size_t num_symbols,
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
    size_t max_num_blocks = num_symbols / min_block_size + 1;
    // We have to allocate one more histogram than the maximum number of block
    // types for the current histogram when the meta-block is too big.
    size_t max_num_types = std::min<size_t>(max_num_blocks, kMaxBlockTypes + 1);
    split_->lengths.resize(max_num_blocks);
    split_->types.resize(max_num_blocks);
    histograms_->resize(max_num_types);
    last_histogram_ix_[0] = last_histogram_ix_[1] = 0;
  }

  // Adds the next symbol to the current histogram. When the current histogram
  // reaches the target size, decides on merging the block.
  void AddSymbol(size_t symbol) {
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
      split_->lengths[0] = static_cast<uint32_t>(block_size_);
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
      for (size_t j = 0; j < 2; ++j) {
        size_t last_histogram_ix = last_histogram_ix_[j];
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
        split_->lengths[num_blocks_] = static_cast<uint32_t>(block_size_);
        split_->types[num_blocks_] = static_cast<uint8_t>(split_->num_types);
        last_histogram_ix_[1] = last_histogram_ix_[0];
        last_histogram_ix_[0] = static_cast<uint8_t>(split_->num_types);
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
        split_->lengths[num_blocks_] = static_cast<uint32_t>(block_size_);
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
        split_->lengths[num_blocks_ - 1] += static_cast<uint32_t>(block_size_);
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
  static const uint16_t kMaxBlockTypes = 256;

  // Alphabet size of particular block category.
  const size_t alphabet_size_;
  // We collect at least this many symbols for each block.
  const size_t min_block_size_;
  // We merge histograms A and B if
  //   entropy(A+B) < entropy(A) + entropy(B) + split_threshold_,
  // where A is the current histogram and B is the histogram of the last or the
  // second last block type.
  const double split_threshold_;

  size_t num_blocks_;
  BlockSplit* split_;  // not owned
  std::vector<HistogramType>* histograms_;  // not owned

  // The number of symbols that we want to collect before deciding on whether
  // or not to merge the block with a previous one or emit a new block.
  size_t target_block_size_;
  // The number of symbols in the current histogram.
  size_t block_size_;
  // Offset of the current histogram.
  size_t curr_histogram_ix_;
  // Offset of the histograms of the previous two block types.
  size_t last_histogram_ix_[2];
  // Entropy of the previous two block types.
  double last_entropy_[2];
  // The number of times we merged the current block with the last one.
  size_t merge_last_count_;
};

void BuildMetaBlockGreedy(const uint8_t* ringbuffer,
                          size_t pos,
                          size_t mask,
                          const Command *commands,
                          size_t n_commands,
                          MetaBlockSplit* mb) {
  size_t num_literals = 0;
  for (size_t i = 0; i < n_commands; ++i) {
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

  for (size_t i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    cmd_blocks.AddSymbol(cmd.cmd_prefix_);
    for (size_t j = cmd.insert_len_; j != 0; --j) {
      lit_blocks.AddSymbol(ringbuffer[pos & mask]);
      ++pos;
    }
    pos += cmd.copy_len();
    if (cmd.copy_len() && cmd.cmd_prefix_ >= 128) {
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
  ContextBlockSplitter(size_t alphabet_size,
                       size_t num_contexts,
                       size_t min_block_size,
                       double split_threshold,
                       size_t num_symbols,
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
    size_t max_num_blocks = num_symbols / min_block_size + 1;
    // We have to allocate one more histogram than the maximum number of block
    // types for the current histogram when the meta-block is too big.
    size_t max_num_types = std::min(max_num_blocks, max_block_types_ + 1);
    split_->lengths.resize(max_num_blocks);
    split_->types.resize(max_num_blocks);
    histograms_->resize(max_num_types * num_contexts);
    last_histogram_ix_[0] = last_histogram_ix_[1] = 0;
  }

  // Adds the next symbol to the current block type and context. When the
  // current block reaches the target size, decides on merging the block.
  void AddSymbol(size_t symbol, size_t context) {
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
      split_->lengths[0] = static_cast<uint32_t>(block_size_);
      split_->types[0] = 0;
      for (size_t i = 0; i < num_contexts_; ++i) {
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
      for (size_t i = 0; i < num_contexts_; ++i) {
        size_t curr_histo_ix = curr_histogram_ix_ + i;
        entropy[i] = BitsEntropy(&(*histograms_)[curr_histo_ix].data_[0],
                                 alphabet_size_);
        for (size_t j = 0; j < 2; ++j) {
          size_t jx = j * num_contexts_ + i;
          size_t last_histogram_ix = last_histogram_ix_[j] + i;
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
        split_->lengths[num_blocks_] = static_cast<uint32_t>(block_size_);
        split_->types[num_blocks_] = static_cast<uint8_t>(split_->num_types);
        last_histogram_ix_[1] = last_histogram_ix_[0];
        last_histogram_ix_[0] = split_->num_types * num_contexts_;
        for (size_t i = 0; i < num_contexts_; ++i) {
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
        split_->lengths[num_blocks_] = static_cast<uint32_t>(block_size_);
        split_->types[num_blocks_] = split_->types[num_blocks_ - 2];
        std::swap(last_histogram_ix_[0], last_histogram_ix_[1]);
        for (size_t i = 0; i < num_contexts_; ++i) {
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
        split_->lengths[num_blocks_ - 1] += static_cast<uint32_t>(block_size_);
        for (size_t i = 0; i < num_contexts_; ++i) {
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
  const size_t alphabet_size_;
  const size_t num_contexts_;
  const size_t max_block_types_;
  // We collect at least this many symbols for each block.
  const size_t min_block_size_;
  // We merge histograms A and B if
  //   entropy(A+B) < entropy(A) + entropy(B) + split_threshold_,
  // where A is the current histogram and B is the histogram of the last or the
  // second last block type.
  const double split_threshold_;

  size_t num_blocks_;
  BlockSplit* split_;  // not owned
  std::vector<HistogramType>* histograms_;  // not owned

  // The number of symbols that we want to collect before deciding on whether
  // or not to merge the block with a previous one or emit a new block.
  size_t target_block_size_;
  // The number of symbols in the current histogram.
  size_t block_size_;
  // Offset of the current histogram.
  size_t curr_histogram_ix_;
  // Offset of the histograms of the previous two block types.
  size_t last_histogram_ix_[2];
  // Entropy of the previous two block types.
  std::vector<double> last_entropy_;
  // The number of times we merged the current block with the last one.
  size_t merge_last_count_;
};

void BuildMetaBlockGreedyWithContexts(const uint8_t* ringbuffer,
                                      size_t pos,
                                      size_t mask,
                                      uint8_t prev_byte,
                                      uint8_t prev_byte2,
                                      ContextType literal_context_mode,
                                      size_t num_contexts,
                                      const uint32_t* static_context_map,
                                      const Command *commands,
                                      size_t n_commands,
                                      MetaBlockSplit* mb) {
  size_t num_literals = 0;
  for (size_t i = 0; i < n_commands; ++i) {
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

  for (size_t i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    cmd_blocks.AddSymbol(cmd.cmd_prefix_);
    for (size_t j = cmd.insert_len_; j != 0; --j) {
      size_t context = Context(prev_byte, prev_byte2, literal_context_mode);
      uint8_t literal = ringbuffer[pos & mask];
      lit_blocks.AddSymbol(literal, static_context_map[context]);
      prev_byte2 = prev_byte;
      prev_byte = literal;
      ++pos;
    }
    pos += cmd.copy_len();
    if (cmd.copy_len()) {
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
  for (size_t i = 0; i < mb->literal_split.num_types; ++i) {
    for (size_t j = 0; j < (1u << kLiteralContextBits); ++j) {
      mb->literal_context_map[(i << kLiteralContextBits) + j] =
          static_cast<uint32_t>(i * num_contexts) + static_context_map[j];
    }
  }
}

void OptimizeHistograms(size_t num_direct_distance_codes,
                        size_t distance_postfix_bits,
                        MetaBlockSplit* mb) {
  uint8_t* good_for_rle = new uint8_t[kNumCommandPrefixes];
  for (size_t i = 0; i < mb->literal_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(256, &mb->literal_histograms[i].data_[0],
                                good_for_rle);
  }
  for (size_t i = 0; i < mb->command_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(kNumCommandPrefixes,
                                &mb->command_histograms[i].data_[0],
                                good_for_rle);
  }
  size_t num_distance_codes =
      kNumDistanceShortCodes + num_direct_distance_codes +
      (48u << distance_postfix_bits);
  for (size_t i = 0; i < mb->distance_histograms.size(); ++i) {
    OptimizeHuffmanCountsForRle(num_distance_codes,
                                &mb->distance_histograms[i].data_[0],
                                good_for_rle);
  }
  delete[] good_for_rle;
}

}  // namespace brotli
