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
// Block split point selection utilities.

#ifndef BROTLI_ENC_BLOCK_SPLITTER_H_
#define BROTLI_ENC_BLOCK_SPLITTER_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <utility>

#include "./command.h"
#include "./metablock.h"

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
