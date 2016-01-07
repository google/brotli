/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Entropy encoding (Huffman) utilities.

#include "./entropy_encode.h"

#include <algorithm>
#include <limits>
#include <vector>
#include <cstdlib>

#include "./histogram.h"
#include "./port.h"
#include "./types.h"

namespace brotli {

void SetDepth(const HuffmanTree &p,
              HuffmanTree *pool,
              uint8_t *depth,
              uint8_t level) {
  if (p.index_left_ >= 0) {
    ++level;
    SetDepth(pool[p.index_left_], pool, depth, level);
    SetDepth(pool[p.index_right_or_value_], pool, depth, level);
  } else {
    depth[p.index_right_or_value_] = level;
  }
}

// This function will create a Huffman tree.
//
// The catch here is that the tree cannot be arbitrarily deep.
// Brotli specifies a maximum depth of 15 bits for "code trees"
// and 7 bits for "code length code trees."
//
// count_limit is the value that is to be faked as the minimum value
// and this minimum value is raised until the tree matches the
// maximum length requirement.
//
// This algorithm is not of excellent performance for very long data blocks,
// especially when population counts are longer than 2**tree_limit, but
// we are not planning to use this with extremely long blocks.
//
// See http://en.wikipedia.org/wiki/Huffman_coding
void CreateHuffmanTree(const uint32_t *data,
                       const size_t length,
                       const int tree_limit,
                       uint8_t *depth) {
  // For block sizes below 64 kB, we never need to do a second iteration
  // of this loop. Probably all of our block sizes will be smaller than
  // that, so this loop is mostly of academic interest. If we actually
  // would need this, we would be better off with the Katajainen algorithm.
  for (uint32_t count_limit = 1; ; count_limit *= 2) {
    std::vector<HuffmanTree> tree;
    tree.reserve(2 * length + 1);

    for (size_t i = length; i != 0;) {
      --i;
      if (data[i]) {
        const uint32_t count = std::max(data[i], count_limit);
        tree.push_back(HuffmanTree(count, -1, static_cast<int16_t>(i)));
      }
    }

    const size_t n = tree.size();
    if (n == 1) {
      depth[tree[0].index_right_or_value_] = 1;      // Only one element.
      break;
    }

    std::stable_sort(tree.begin(), tree.end(), SortHuffmanTree);

    // The nodes are:
    // [0, n): the sorted leaf nodes that we start with.
    // [n]: we add a sentinel here.
    // [n + 1, 2n): new parent nodes are added here, starting from
    //              (n+1). These are naturally in ascending order.
    // [2n]: we add a sentinel at the end as well.
    // There will be (2n+1) elements at the end.
    const HuffmanTree sentinel(std::numeric_limits<uint32_t>::max(), -1, -1);
    tree.push_back(sentinel);
    tree.push_back(sentinel);

    size_t i = 0;      // Points to the next leaf node.
    size_t j = n + 1;  // Points to the next non-leaf node.
    for (size_t k = n - 1; k != 0; --k) {
      size_t left, right;
      if (tree[i].total_count_ <= tree[j].total_count_) {
        left = i;
        ++i;
      } else {
        left = j;
        ++j;
      }
      if (tree[i].total_count_ <= tree[j].total_count_) {
        right = i;
        ++i;
      } else {
        right = j;
        ++j;
      }

      // The sentinel node becomes the parent node.
      size_t j_end = tree.size() - 1;
      tree[j_end].total_count_ =
          tree[left].total_count_ + tree[right].total_count_;
      tree[j_end].index_left_ = static_cast<int16_t>(left);
      tree[j_end].index_right_or_value_ = static_cast<int16_t>(right);

      // Add back the last sentinel node.
      tree.push_back(sentinel);
    }
    assert(tree.size() == 2 * n + 1);
    SetDepth(tree[2 * n - 1], &tree[0], depth, 0);

    // We need to pack the Huffman tree in tree_limit bits.
    // If this was not successful, add fake entities to the lowest values
    // and retry.
    if (*std::max_element(&depth[0], &depth[length]) <= tree_limit) {
      break;
    }
  }
}

void Reverse(std::vector<uint8_t>* v, size_t start, size_t end) {
  --end;
  while (start < end) {
    uint8_t tmp = (*v)[start];
    (*v)[start] = (*v)[end];
    (*v)[end] = tmp;
    ++start;
    --end;
  }
}

void WriteHuffmanTreeRepetitions(
    const uint8_t previous_value,
    const uint8_t value,
    size_t repetitions,
    std::vector<uint8_t> *tree,
    std::vector<uint8_t> *extra_bits_data) {
  assert(repetitions > 0);
  if (previous_value != value) {
    tree->push_back(value);
    extra_bits_data->push_back(0);
    --repetitions;
  }
  if (repetitions == 7) {
    tree->push_back(value);
    extra_bits_data->push_back(0);
    --repetitions;
  }
  if (repetitions < 3) {
    for (size_t i = 0; i < repetitions; ++i) {
      tree->push_back(value);
      extra_bits_data->push_back(0);
    }
  } else {
    repetitions -= 3;
    size_t start = tree->size();
    while (true) {
      tree->push_back(16);
      extra_bits_data->push_back(repetitions & 0x3);
      repetitions >>= 2;
      if (repetitions == 0) {
        break;
      }
      --repetitions;
    }
    Reverse(tree, start, tree->size());
    Reverse(extra_bits_data, start, tree->size());
  }
}

void WriteHuffmanTreeRepetitionsZeros(
    size_t repetitions,
    std::vector<uint8_t> *tree,
    std::vector<uint8_t> *extra_bits_data) {
  if (repetitions == 11) {
    tree->push_back(0);
    extra_bits_data->push_back(0);
    --repetitions;
  }
  if (repetitions < 3) {
    for (size_t i = 0; i < repetitions; ++i) {
      tree->push_back(0);
      extra_bits_data->push_back(0);
    }
  } else {
    repetitions -= 3;
    size_t start = tree->size();
    while (true) {
      tree->push_back(17);
      extra_bits_data->push_back(repetitions & 0x7);
      repetitions >>= 3;
      if (repetitions == 0) {
        break;
      }
      --repetitions;
    }
    Reverse(tree, start, tree->size());
    Reverse(extra_bits_data, start, tree->size());
  }
}

bool OptimizeHuffmanCountsForRle(size_t length, uint32_t* counts) {
  size_t nonzero_count = 0;
  size_t stride;
  size_t limit;
  size_t sum;
  const size_t streak_limit = 1240;
  uint8_t* good_for_rle;
  // Let's make the Huffman code more compatible with rle encoding.
  size_t i;
  for (i = 0; i < length; i++) {
    if (counts[i]) {
      ++nonzero_count;
    }
  }
  if (nonzero_count < 16) {
    return 1;
  }
  while (length != 0 && counts[length - 1] == 0) {
    --length;
  }
  if (length == 0) {
    return 1;  // All zeros.
  }
  // Now counts[0..length - 1] does not have trailing zeros.
  {
    size_t nonzeros = 0;
    uint32_t smallest_nonzero = 1 << 30;
    for (i = 0; i < length; ++i) {
      if (counts[i] != 0) {
        ++nonzeros;
        if (smallest_nonzero > counts[i]) {
          smallest_nonzero = counts[i];
        }
      }
    }
    if (nonzeros < 5) {
      // Small histogram will model it well.
      return 1;
    }
    size_t zeros = length - nonzeros;
    if (smallest_nonzero < 4) {
      if (zeros < 6) {
        for (i = 1; i < length - 1; ++i) {
          if (counts[i - 1] != 0 && counts[i] == 0 && counts[i + 1] != 0) {
            counts[i] = 1;
          }
        }
      }
    }
    if (nonzeros < 28) {
      return 1;
    }
  }
  // 2) Let's mark all population counts that already can be encoded
  // with an rle code.
  good_for_rle = (uint8_t*)calloc(length, 1);
  if (good_for_rle == NULL) {
    return 0;
  }
  {
    // Let's not spoil any of the existing good rle codes.
    // Mark any seq of 0's that is longer as 5 as a good_for_rle.
    // Mark any seq of non-0's that is longer as 7 as a good_for_rle.
    uint32_t symbol = counts[0];
    size_t step = 0;
    for (i = 0; i <= length; ++i) {
      if (i == length || counts[i] != symbol) {
        if ((symbol == 0 && step >= 5) ||
            (symbol != 0 && step >= 7)) {
          size_t k;
          for (k = 0; k < step; ++k) {
            good_for_rle[i - k - 1] = 1;
          }
        }
        step = 1;
        if (i != length) {
          symbol = counts[i];
        }
      } else {
        ++step;
      }
    }
  }
  // 3) Let's replace those population counts that lead to more rle codes.
  // Math here is in 24.8 fixed point representation.
  stride = 0;
  limit = 256 * (counts[0] + counts[1] + counts[2]) / 3 + 420;
  sum = 0;
  for (i = 0; i <= length; ++i) {
    if (i == length || good_for_rle[i] ||
        (i != 0 && good_for_rle[i - 1]) ||
        (256 * counts[i] - limit + streak_limit) >= 2 * streak_limit) {
      if (stride >= 4 || (stride >= 3 && sum == 0)) {
        size_t k;
        // The stride must end, collapse what we have, if we have enough (4).
        size_t count = (sum + stride / 2) / stride;
        if (count == 0) {
          count = 1;
        }
        if (sum == 0) {
          // Don't make an all zeros stride to be upgraded to ones.
          count = 0;
        }
        for (k = 0; k < stride; ++k) {
          // We don't want to change value at counts[i],
          // that is already belonging to the next stride. Thus - 1.
          counts[i - k - 1] = static_cast<uint32_t>(count);
        }
      }
      stride = 0;
      sum = 0;
      if (i < length - 2) {
        // All interesting strides have a count of at least 4,
        // at least when non-zeros.
        limit = 256 * (counts[i] + counts[i + 1] + counts[i + 2]) / 3 + 420;
      } else if (i < length) {
        limit = 256 * counts[i];
      } else {
        limit = 0;
      }
    }
    ++stride;
    if (i != length) {
      sum += counts[i];
      if (stride >= 4) {
        limit = (256 * sum + stride / 2) / stride;
      }
      if (stride == 4) {
        limit += 120;
      }
    }
  }
  free(good_for_rle);
  return 1;
}

static void DecideOverRleUse(const uint8_t* depth, const size_t length,
                             bool *use_rle_for_non_zero,
                             bool *use_rle_for_zero) {
  size_t total_reps_zero = 0;
  size_t total_reps_non_zero = 0;
  size_t count_reps_zero = 1;
  size_t count_reps_non_zero = 1;
  for (size_t i = 0; i < length;) {
    const uint8_t value = depth[i];
    size_t reps = 1;
    for (size_t k = i + 1; k < length && depth[k] == value; ++k) {
      ++reps;
    }
    if (reps >= 3 && value == 0) {
      total_reps_zero += reps;
      ++count_reps_zero;
    }
    if (reps >= 4 && value != 0) {
      total_reps_non_zero += reps;
      ++count_reps_non_zero;
    }
    i += reps;
  }
  *use_rle_for_non_zero = total_reps_non_zero > count_reps_non_zero * 2;
  *use_rle_for_zero = total_reps_zero > count_reps_zero * 2;
}

void WriteHuffmanTree(const uint8_t* depth,
                      size_t length,
                      std::vector<uint8_t> *tree,
                      std::vector<uint8_t> *extra_bits_data) {
  uint8_t previous_value = 8;

  // Throw away trailing zeros.
  size_t new_length = length;
  for (size_t i = 0; i < length; ++i) {
    if (depth[length - i - 1] == 0) {
      --new_length;
    } else {
      break;
    }
  }

  // First gather statistics on if it is a good idea to do rle.
  bool use_rle_for_non_zero = false;
  bool use_rle_for_zero = false;
  if (length > 50) {
    // Find rle coding for longer codes.
    // Shorter codes seem not to benefit from rle.
    DecideOverRleUse(depth, new_length,
                     &use_rle_for_non_zero, &use_rle_for_zero);
  }

  // Actual rle coding.
  for (size_t i = 0; i < new_length;) {
    const uint8_t value = depth[i];
    size_t reps = 1;
    if ((value != 0 && use_rle_for_non_zero) ||
        (value == 0 && use_rle_for_zero)) {
      for (size_t k = i + 1; k < new_length && depth[k] == value; ++k) {
        ++reps;
      }
    }
    if (value == 0) {
      WriteHuffmanTreeRepetitionsZeros(reps, tree, extra_bits_data);
    } else {
      WriteHuffmanTreeRepetitions(previous_value,
                                  value, reps, tree, extra_bits_data);
      previous_value = value;
    }
    i += reps;
  }
}

namespace {

uint16_t ReverseBits(int num_bits, uint16_t bits) {
  static const size_t kLut[16] = {  // Pre-reversed 4-bit values.
    0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
    0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
  };
  size_t retval = kLut[bits & 0xf];
  for (int i = 4; i < num_bits; i += 4) {
    retval <<= 4;
    bits = static_cast<uint16_t>(bits >> 4);
    retval |= kLut[bits & 0xf];
  }
  retval >>= (-num_bits & 0x3);
  return static_cast<uint16_t>(retval);
}

}  // namespace

void ConvertBitDepthsToSymbols(const uint8_t *depth,
                               size_t len,
                               uint16_t *bits) {
  // In Brotli, all bit depths are [1..15]
  // 0 bit depth means that the symbol does not exist.
  const int kMaxBits = 16;  // 0..15 are values for bits
  uint16_t bl_count[kMaxBits] = { 0 };
  {
    for (size_t i = 0; i < len; ++i) {
      ++bl_count[depth[i]];
    }
    bl_count[0] = 0;
  }
  uint16_t next_code[kMaxBits];
  next_code[0] = 0;
  {
    int code = 0;
    for (int bits = 1; bits < kMaxBits; ++bits) {
      code = (code + bl_count[bits - 1]) << 1;
      next_code[bits] = static_cast<uint16_t>(code);
    }
  }
  for (size_t i = 0; i < len; ++i) {
    if (depth[i]) {
      bits[i] = ReverseBits(depth[i], next_code[depth[i]]++);
    }
  }
}

}  // namespace brotli
