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

namespace brotli {

struct BlockSplit {
  int num_types_;
  std::vector<uint8_t> types_;
  std::vector<int> type_codes_;
  std::vector<int> lengths_;
};

struct BlockSplitIterator {
  explicit BlockSplitIterator(const BlockSplit& split)
      : split_(split), idx_(0), type_(0), length_(0) {
    if (!split.lengths_.empty()) {
      length_ = split.lengths_[0];
    }
  }

  void Next() {
    if (length_ == 0) {
      ++idx_;
      type_ = split_.types_[idx_];
      length_ = split_.lengths_[idx_];
    }
    --length_;
  }

  const BlockSplit& split_;
  int idx_;
  int type_;
  int length_;
};

void CopyLiteralsToByteArray(const std::vector<Command>& cmds,
                             const uint8_t* data,
                             std::vector<uint8_t>* literals);

void SplitBlock(const std::vector<Command>& cmds,
                const uint8_t* data,
                BlockSplit* literal_split,
                BlockSplit* insert_and_copy_split,
                BlockSplit* dist_split);

void SplitBlockByTotalLength(const std::vector<Command>& all_commands,
                             int input_size,
                             int target_length,
                             std::vector<std::vector<Command> >* blocks);

}  // namespace brotli

#endif  // BROTLI_ENC_BLOCK_SPLITTER_H_
