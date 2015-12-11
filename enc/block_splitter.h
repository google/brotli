/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Block split point selection utilities.

#ifndef BROTLI_ENC_BLOCK_SPLITTER_H_
#define BROTLI_ENC_BLOCK_SPLITTER_H_

#include <string.h>
#include <vector>
#include <utility>

#include "./command.h"
#include "./metablock.h"
#include "./types.h"

namespace brotli {

struct BlockSplitIterator {
  explicit BlockSplitIterator(const BlockSplit& split)
      : split_(split), idx_(0), type_(0), length_(0) {
    if (!split.lengths.empty()) {
      length_ = split.lengths[0];
    }
  }

  void Next() {
    if (length_ == 0) {
      ++idx_;
      type_ = split_.types[idx_];
      length_ = split_.lengths[idx_];
    }
    --length_;
  }

  const BlockSplit& split_;
  int idx_;
  int type_;
  int length_;
};

void CopyLiteralsToByteArray(const Command* cmds,
                             const size_t num_commands,
                             const uint8_t* data,
                             const size_t offset,
                             const size_t mask,
                             std::vector<uint8_t>* literals);

void SplitBlock(const Command* cmds,
                const size_t num_commands,
                const uint8_t* data,
                const size_t offset,
                const size_t mask,
                BlockSplit* literal_split,
                BlockSplit* insert_and_copy_split,
                BlockSplit* dist_split);

void SplitBlockByTotalLength(const Command* all_commands,
                             const size_t num_commands,
                             int input_size,
                             int target_length,
                             std::vector<std::vector<Command> >* blocks);

}  // namespace brotli

#endif  // BROTLI_ENC_BLOCK_SPLITTER_H_
