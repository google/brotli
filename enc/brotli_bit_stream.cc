/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Brotli bit stream functions to support the low level format. There are no
// compression algorithms here, just the right ordering of bits to match the
// specs.

#include "./brotli_bit_stream.h"

#include <algorithm>
#include <cstdlib>  /* free, malloc */
#include <cstring>
#include <limits>
#include <vector>

#include "./bit_cost.h"
#include "./context.h"
#include "./entropy_encode.h"
#include "./entropy_encode_static.h"
#include "./fast_log.h"
#include "./prefix.h"
#include "./write_bits.h"

namespace brotli {

namespace {

static const size_t kMaxHuffmanTreeSize = 2 * kNumCommandPrefixes + 1;
// Context map alphabet has 256 context id symbols plus max 16 rle symbols.
static const size_t kContextMapAlphabetSize = 256 + 16;
// Block type alphabet has 256 block id symbols plus 2 special symbols.
static const size_t kBlockTypeAlphabetSize = 256 + 2;

// nibblesbits represents the 2 bits to encode MNIBBLES (0-3)
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void EncodeMlen(size_t length, uint64_t* bits,
                size_t* numbits, uint64_t* nibblesbits) {
  assert(length > 0);
  assert(length <= (1 << 24));
  length--;  // MLEN - 1 is encoded
  size_t lg = length == 0 ? 1 : Log2FloorNonZero(
      static_cast<uint32_t>(length)) + 1;
  assert(lg <= 24);
  size_t mnibbles = (lg < 16 ? 16 : (lg + 3)) / 4;
  *nibblesbits = mnibbles - 4;
  *numbits = mnibbles * 4;
  *bits = length;
}

static inline void StoreCommandExtra(
    const Command& cmd, size_t* storage_ix, uint8_t* storage) {
  uint32_t copylen_code = cmd.copy_len_code();
  uint16_t inscode = GetInsertLengthCode(cmd.insert_len_);
  uint16_t copycode = GetCopyLengthCode(copylen_code);
  uint32_t insnumextra = GetInsertExtra(inscode);
  uint64_t insextraval = cmd.insert_len_ - GetInsertBase(inscode);
  uint64_t copyextraval = copylen_code - GetCopyBase(copycode);
  uint64_t bits = (copyextraval << insnumextra) | insextraval;
  WriteBits(insnumextra + GetCopyExtra(copycode), bits, storage_ix, storage);
}

}  // namespace

void StoreVarLenUint8(size_t n, size_t* storage_ix, uint8_t* storage) {
  if (n == 0) {
    WriteBits(1, 0, storage_ix, storage);
  } else {
    WriteBits(1, 1, storage_ix, storage);
    size_t nbits = Log2FloorNonZero(n);
    WriteBits(3, nbits, storage_ix, storage);
    WriteBits(nbits, n - (1 << nbits), storage_ix, storage);
  }
}

void StoreCompressedMetaBlockHeader(bool final_block,
                                    size_t length,
                                    size_t* storage_ix,
                                    uint8_t* storage) {
  // Write ISLAST bit.
  WriteBits(1, final_block, storage_ix, storage);
  // Write ISEMPTY bit.
  if (final_block) {
    WriteBits(1, 0, storage_ix, storage);
  }

  uint64_t lenbits;
  size_t nlenbits;
  uint64_t nibblesbits;
  EncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);
  WriteBits(2, nibblesbits, storage_ix, storage);
  WriteBits(nlenbits, lenbits, storage_ix, storage);

  if (!final_block) {
    // Write ISUNCOMPRESSED bit.
    WriteBits(1, 0, storage_ix, storage);
  }
}

void StoreUncompressedMetaBlockHeader(size_t length,
                                      size_t* storage_ix,
                                      uint8_t* storage) {
  // Write ISLAST bit. Uncompressed block cannot be the last one, so set to 0.
  WriteBits(1, 0, storage_ix, storage);
  uint64_t lenbits;
  size_t nlenbits;
  uint64_t nibblesbits;
  EncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);
  WriteBits(2, nibblesbits, storage_ix, storage);
  WriteBits(nlenbits, lenbits, storage_ix, storage);
  // Write ISUNCOMPRESSED bit.
  WriteBits(1, 1, storage_ix, storage);
}

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const int num_codes,
    const uint8_t *code_length_bitdepth,
    size_t *storage_ix,
    uint8_t *storage) {
  static const uint8_t kStorageOrder[kCodeLengthCodes] = {
    1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
  };
  // The bit lengths of the Huffman code over the code length alphabet
  // are compressed with the following static Huffman code:
  //   Symbol   Code
  //   ------   ----
  //   0          00
  //   1        1110
  //   2         110
  //   3          01
  //   4          10
  //   5        1111
  static const uint8_t kHuffmanBitLengthHuffmanCodeSymbols[6] = {
     0, 7, 3, 2, 1, 15
  };
  static const uint8_t kHuffmanBitLengthHuffmanCodeBitLengths[6] = {
    2, 4, 3, 2, 2, 4
  };

  // Throw away trailing zeros:
  size_t codes_to_store = kCodeLengthCodes;
  if (num_codes > 1) {
    for (; codes_to_store > 0; --codes_to_store) {
      if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
        break;
      }
    }
  }
  size_t skip_some = 0;  // skips none.
  if (code_length_bitdepth[kStorageOrder[0]] == 0 &&
      code_length_bitdepth[kStorageOrder[1]] == 0) {
    skip_some = 2;  // skips two.
    if (code_length_bitdepth[kStorageOrder[2]] == 0) {
      skip_some = 3;  // skips three.
    }
  }
  WriteBits(2, skip_some, storage_ix, storage);
  for (size_t i = skip_some; i < codes_to_store; ++i) {
    size_t l = code_length_bitdepth[kStorageOrder[i]];
    WriteBits(kHuffmanBitLengthHuffmanCodeBitLengths[l],
              kHuffmanBitLengthHuffmanCodeSymbols[l], storage_ix, storage);
  }
}

static void StoreHuffmanTreeToBitMask(
    const size_t huffman_tree_size,
    const uint8_t* huffman_tree,
    const uint8_t* huffman_tree_extra_bits,
    const uint8_t* code_length_bitdepth,
    const uint16_t* code_length_bitdepth_symbols,
    size_t * __restrict storage_ix,
    uint8_t * __restrict storage) {
  for (size_t i = 0; i < huffman_tree_size; ++i) {
    size_t ix = huffman_tree[i];
    WriteBits(code_length_bitdepth[ix], code_length_bitdepth_symbols[ix],
              storage_ix, storage);
    // Extra bits
    switch (ix) {
      case 16:
        WriteBits(2, huffman_tree_extra_bits[i], storage_ix, storage);
        break;
      case 17:
        WriteBits(3, huffman_tree_extra_bits[i], storage_ix, storage);
        break;
    }
  }
}

static void StoreSimpleHuffmanTree(const uint8_t* depths,
                                   size_t symbols[4],
                                   size_t num_symbols,
                                   size_t max_bits,
                                   size_t *storage_ix, uint8_t *storage) {
  // value of 1 indicates a simple Huffman code
  WriteBits(2, 1, storage_ix, storage);
  WriteBits(2, num_symbols - 1, storage_ix, storage);  // NSYM - 1

  // Sort
  for (size_t i = 0; i < num_symbols; i++) {
    for (size_t j = i + 1; j < num_symbols; j++) {
      if (depths[symbols[j]] < depths[symbols[i]]) {
        std::swap(symbols[j], symbols[i]);
      }
    }
  }

  if (num_symbols == 2) {
    WriteBits(max_bits, symbols[0], storage_ix, storage);
    WriteBits(max_bits, symbols[1], storage_ix, storage);
  } else if (num_symbols == 3) {
    WriteBits(max_bits, symbols[0], storage_ix, storage);
    WriteBits(max_bits, symbols[1], storage_ix, storage);
    WriteBits(max_bits, symbols[2], storage_ix, storage);
  } else {
    WriteBits(max_bits, symbols[0], storage_ix, storage);
    WriteBits(max_bits, symbols[1], storage_ix, storage);
    WriteBits(max_bits, symbols[2], storage_ix, storage);
    WriteBits(max_bits, symbols[3], storage_ix, storage);
    // tree-select
    WriteBits(1, depths[symbols[0]] == 1 ? 1 : 0, storage_ix, storage);
  }
}

// num = alphabet size
// depths = symbol depths
void StoreHuffmanTree(const uint8_t* depths, size_t num,
                      HuffmanTree* tree,
                      size_t *storage_ix, uint8_t *storage) {
  // Write the Huffman tree into the brotli-representation.
  // The command alphabet is the largest, so this allocation will fit all
  // alphabets.
  assert(num <= kNumCommandPrefixes);
  uint8_t huffman_tree[kNumCommandPrefixes];
  uint8_t huffman_tree_extra_bits[kNumCommandPrefixes];
  size_t huffman_tree_size = 0;
  WriteHuffmanTree(depths, num, &huffman_tree_size, huffman_tree,
                   huffman_tree_extra_bits);

  // Calculate the statistics of the Huffman tree in brotli-representation.
  uint32_t huffman_tree_histogram[kCodeLengthCodes] = { 0 };
  for (size_t i = 0; i < huffman_tree_size; ++i) {
    ++huffman_tree_histogram[huffman_tree[i]];
  }

  int num_codes = 0;
  int code = 0;
  for (int i = 0; i < kCodeLengthCodes; ++i) {
    if (huffman_tree_histogram[i]) {
      if (num_codes == 0) {
        code = i;
        num_codes = 1;
      } else if (num_codes == 1) {
        num_codes = 2;
        break;
      }
    }
  }

  // Calculate another Huffman tree to use for compressing both the
  // earlier Huffman tree with.
  uint8_t code_length_bitdepth[kCodeLengthCodes] = { 0 };
  uint16_t code_length_bitdepth_symbols[kCodeLengthCodes] = { 0 };
  CreateHuffmanTree(&huffman_tree_histogram[0], kCodeLengthCodes,
                    5, tree, &code_length_bitdepth[0]);
  ConvertBitDepthsToSymbols(code_length_bitdepth, kCodeLengthCodes,
                            &code_length_bitdepth_symbols[0]);

  // Now, we have all the data, let's start storing it
  StoreHuffmanTreeOfHuffmanTreeToBitMask(num_codes, code_length_bitdepth,
                                         storage_ix, storage);

  if (num_codes == 1) {
    code_length_bitdepth[code] = 0;
  }

  // Store the real huffman tree now.
  StoreHuffmanTreeToBitMask(huffman_tree_size,
                            huffman_tree,
                            huffman_tree_extra_bits,
                            &code_length_bitdepth[0],
                            code_length_bitdepth_symbols,
                            storage_ix, storage);
}

void BuildAndStoreHuffmanTree(const uint32_t *histogram,
                              const size_t length,
                              HuffmanTree* tree,
                              uint8_t* depth,
                              uint16_t* bits,
                              size_t* storage_ix,
                              uint8_t* storage) {
  size_t count = 0;
  size_t s4[4] = { 0 };
  for (size_t i = 0; i < length; i++) {
    if (histogram[i]) {
      if (count < 4) {
        s4[count] = i;
      } else if (count > 4) {
        break;
      }
      count++;
    }
  }

  size_t max_bits_counter = length - 1;
  size_t max_bits = 0;
  while (max_bits_counter) {
    max_bits_counter >>= 1;
    ++max_bits;
  }

  if (count <= 1) {
    WriteBits(4, 1, storage_ix, storage);
    WriteBits(max_bits, s4[0], storage_ix, storage);
    return;
  }

  CreateHuffmanTree(histogram, length, 15, tree, depth);
  ConvertBitDepthsToSymbols(depth, length, bits);

  if (count <= 4) {
    StoreSimpleHuffmanTree(depth, s4, count, max_bits, storage_ix, storage);
  } else {
    StoreHuffmanTree(depth, length, tree, storage_ix, storage);
  }
}

static inline bool SortHuffmanTree(const HuffmanTree& v0,
                                   const HuffmanTree& v1) {
  return v0.total_count_ < v1.total_count_;
}

void BuildAndStoreHuffmanTreeFast(const uint32_t *histogram,
                                  const size_t histogram_total,
                                  const size_t max_bits,
                                  uint8_t* depth,
                                  uint16_t* bits,
                                  size_t* storage_ix,
                                  uint8_t* storage) {
  size_t count = 0;
  size_t symbols[4] = { 0 };
  size_t length = 0;
  size_t total = histogram_total;
  while (total != 0) {
    if (histogram[length]) {
      if (count < 4) {
        symbols[count] = length;
      }
      ++count;
      total -= histogram[length];
    }
    ++length;
  }

  if (count <= 1) {
    WriteBits(4, 1, storage_ix, storage);
    WriteBits(max_bits, symbols[0], storage_ix, storage);
    return;
  }

  const size_t max_tree_size = 2 * length + 1;
  HuffmanTree* const tree =
      static_cast<HuffmanTree*>(malloc(max_tree_size * sizeof(HuffmanTree)));
  for (uint32_t count_limit = 1; ; count_limit *= 2) {
    HuffmanTree* node = tree;
    for (size_t i = length; i != 0;) {
      --i;
      if (histogram[i]) {
        if (PREDICT_TRUE(histogram[i] >= count_limit)) {
          *node = HuffmanTree(histogram[i], -1, static_cast<int16_t>(i));
        } else {
          *node = HuffmanTree(count_limit, -1, static_cast<int16_t>(i));
        }
        ++node;
      }
    }
    const int n = static_cast<int>(node - tree);
    std::sort(tree, node, SortHuffmanTree);
    // The nodes are:
    // [0, n): the sorted leaf nodes that we start with.
    // [n]: we add a sentinel here.
    // [n + 1, 2n): new parent nodes are added here, starting from
    //              (n+1). These are naturally in ascending order.
    // [2n]: we add a sentinel at the end as well.
    // There will be (2n+1) elements at the end.
    const HuffmanTree sentinel(std::numeric_limits<int>::max(), -1, -1);
    *node++ = sentinel;
    *node++ = sentinel;

    int i = 0;      // Points to the next leaf node.
    int j = n + 1;  // Points to the next non-leaf node.
    for (int k = n - 1; k > 0; --k) {
      int left, right;
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
      node[-1].total_count_ =
          tree[left].total_count_ + tree[right].total_count_;
      node[-1].index_left_ = static_cast<int16_t>(left);
      node[-1].index_right_or_value_ = static_cast<int16_t>(right);
      // Add back the last sentinel node.
      *node++ = sentinel;
    }
    SetDepth(tree[2 * n - 1], &tree[0], depth, 0);
    // We need to pack the Huffman tree in 14 bits.
    // If this was not successful, add fake entities to the lowest values
    // and retry.
    if (PREDICT_TRUE(*std::max_element(&depth[0], &depth[length]) <= 14)) {
      break;
    }
  }
  free(tree);
  ConvertBitDepthsToSymbols(depth, length, bits);
  if (count <= 4) {
    // value of 1 indicates a simple Huffman code
    WriteBits(2, 1, storage_ix, storage);
    WriteBits(2, count - 1, storage_ix, storage);  // NSYM - 1

    // Sort
    for (size_t i = 0; i < count; i++) {
      for (size_t j = i + 1; j < count; j++) {
        if (depth[symbols[j]] < depth[symbols[i]]) {
          std::swap(symbols[j], symbols[i]);
        }
      }
    }

    if (count == 2) {
      WriteBits(max_bits, symbols[0], storage_ix, storage);
      WriteBits(max_bits, symbols[1], storage_ix, storage);
    } else if (count == 3) {
      WriteBits(max_bits, symbols[0], storage_ix, storage);
      WriteBits(max_bits, symbols[1], storage_ix, storage);
      WriteBits(max_bits, symbols[2], storage_ix, storage);
    } else {
      WriteBits(max_bits, symbols[0], storage_ix, storage);
      WriteBits(max_bits, symbols[1], storage_ix, storage);
      WriteBits(max_bits, symbols[2], storage_ix, storage);
      WriteBits(max_bits, symbols[3], storage_ix, storage);
      // tree-select
      WriteBits(1, depth[symbols[0]] == 1 ? 1 : 0, storage_ix, storage);
    }
  } else {
    // Complex Huffman Tree
    StoreStaticCodeLengthCode(storage_ix, storage);

    // Actual rle coding.
    uint8_t previous_value = 8;
    for (size_t i = 0; i < length;) {
      const uint8_t value = depth[i];
      size_t reps = 1;
      for (size_t k = i + 1; k < length && depth[k] == value; ++k) {
        ++reps;
      }
      i += reps;
      if (value == 0) {
        WriteBits(kZeroRepsDepth[reps], kZeroRepsBits[reps],
                  storage_ix, storage);
      } else {
        if (previous_value != value) {
          WriteBits(kCodeLengthDepth[value], kCodeLengthBits[value],
                    storage_ix, storage);
          --reps;
        }
        if (reps < 3) {
          while (reps != 0) {
            reps--;
            WriteBits(kCodeLengthDepth[value], kCodeLengthBits[value],
                      storage_ix, storage);
          }
        } else {
          reps -= 3;
          WriteBits(kNonZeroRepsDepth[reps], kNonZeroRepsBits[reps],
                    storage_ix, storage);
        }
        previous_value = value;
      }
    }
  }
}

static size_t IndexOf(const uint8_t* v, size_t v_size, uint8_t value) {
  size_t i = 0;
  for (; i < v_size; ++i) {
    if (v[i] == value) return i;
  }
  return i;
}

static void MoveToFront(uint8_t* v, size_t index) {
  uint8_t value = v[index];
  for (size_t i = index; i != 0; --i) {
    v[i] = v[i - 1];
  }
  v[0] = value;
}

static void MoveToFrontTransform(const uint32_t* __restrict v_in,
                                 const size_t v_size,
                                 uint32_t* v_out) {
  if (v_size == 0) {
    return;
  }
  uint32_t max_value = *std::max_element(v_in, v_in + v_size);
  assert(max_value < 256u);
  uint8_t mtf[256];
  size_t mtf_size = max_value + 1;
  for (uint32_t i = 0; i <= max_value; ++i) {
    mtf[i] = static_cast<uint8_t>(i);
  }
  for (size_t i = 0; i < v_size; ++i) {
    size_t index = IndexOf(mtf, mtf_size, static_cast<uint8_t>(v_in[i]));
    assert(index < mtf_size);
    v_out[i] = static_cast<uint32_t>(index);
    MoveToFront(mtf, index);
  }
}

// Finds runs of zeros in v[0..in_size) and replaces them with a prefix code of
// the run length plus extra bits (lower 9 bits is the prefix code and the rest
// are the extra bits). Non-zero values in v[] are shifted by
// *max_length_prefix. Will not create prefix codes bigger than the initial
// value of *max_run_length_prefix. The prefix code of run length L is simply
// Log2Floor(L) and the number of extra bits is the same as the prefix code.
static void RunLengthCodeZeros(const size_t in_size,
                               uint32_t* __restrict v,
                               size_t* __restrict out_size,
                               uint32_t* __restrict max_run_length_prefix) {
  uint32_t max_reps = 0;
  for (size_t i = 0; i < in_size;) {
    for (; i < in_size && v[i] != 0; ++i) ;
    uint32_t reps = 0;
    for (; i < in_size && v[i] == 0; ++i) {
      ++reps;
    }
    max_reps = std::max(reps, max_reps);
  }
  uint32_t max_prefix = max_reps > 0 ? Log2FloorNonZero(max_reps) : 0;
  max_prefix = std::min(max_prefix, *max_run_length_prefix);
  *max_run_length_prefix = max_prefix;
  *out_size = 0;
  for (size_t i = 0; i < in_size;) {
    assert(*out_size <= i);
    if (v[i] != 0) {
      v[*out_size] = v[i] + *max_run_length_prefix;
      ++i;
      ++(*out_size);
    } else {
      uint32_t reps = 1;
      for (size_t k = i + 1; k < in_size && v[k] == 0; ++k) {
        ++reps;
      }
      i += reps;
      while (reps != 0) {
        if (reps < (2u << max_prefix)) {
          uint32_t run_length_prefix = Log2FloorNonZero(reps);
          const uint32_t extra_bits = reps - (1u << run_length_prefix);
          v[*out_size] = run_length_prefix + (extra_bits << 9);
          ++(*out_size);
          break;
        } else {
          const uint32_t extra_bits = (1u << max_prefix) - 1u;
          v[*out_size] = max_prefix + (extra_bits << 9);
          reps -= (2u << max_prefix) - 1u;
          ++(*out_size);
        }
      }
    }
  }
}

void EncodeContextMap(const std::vector<uint32_t>& context_map,
                      size_t num_clusters,
                      HuffmanTree* tree,
                      size_t* storage_ix, uint8_t* storage) {
  StoreVarLenUint8(num_clusters - 1, storage_ix, storage);

  if (num_clusters == 1) {
    return;
  }

  uint32_t* rle_symbols = new uint32_t[context_map.size()];
  MoveToFrontTransform(&context_map[0], context_map.size(), rle_symbols);
  uint32_t max_run_length_prefix = 6;
  size_t num_rle_symbols = 0;
  RunLengthCodeZeros(context_map.size(), rle_symbols,
                     &num_rle_symbols, &max_run_length_prefix);
  uint32_t histogram[kContextMapAlphabetSize];
  memset(histogram, 0, sizeof(histogram));
  static const int kSymbolBits = 9;
  static const uint32_t kSymbolMask = (1u << kSymbolBits) - 1u;
  for (size_t i = 0; i < num_rle_symbols; ++i) {
    ++histogram[rle_symbols[i] & kSymbolMask];
  }
  bool use_rle = max_run_length_prefix > 0;
  WriteBits(1, use_rle, storage_ix, storage);
  if (use_rle) {
    WriteBits(4, max_run_length_prefix - 1, storage_ix, storage);
  }
  uint8_t depths[kContextMapAlphabetSize];
  uint16_t bits[kContextMapAlphabetSize];
  memset(depths, 0, sizeof(depths));
  memset(bits, 0, sizeof(bits));
  BuildAndStoreHuffmanTree(histogram, num_clusters + max_run_length_prefix,
                           tree, depths, bits, storage_ix, storage);
  for (size_t i = 0; i < num_rle_symbols; ++i) {
    const uint32_t rle_symbol = rle_symbols[i] & kSymbolMask;
    const uint32_t extra_bits_val = rle_symbols[i] >> kSymbolBits;
    WriteBits(depths[rle_symbol], bits[rle_symbol], storage_ix, storage);
    if (rle_symbol > 0 && rle_symbol <= max_run_length_prefix) {
      WriteBits(rle_symbol, extra_bits_val, storage_ix, storage);
    }
  }
  WriteBits(1, 1, storage_ix, storage);  // use move-to-front
  delete[] rle_symbols;
}

void StoreBlockSwitch(const BlockSplitCode& code,
                      const size_t block_ix,
                      size_t* storage_ix,
                      uint8_t* storage) {
  if (block_ix > 0) {
    size_t typecode = code.type_code[block_ix];
    WriteBits(code.type_depths[typecode], code.type_bits[typecode],
              storage_ix, storage);
  }
  size_t lencode = code.length_prefix[block_ix];
  WriteBits(code.length_depths[lencode], code.length_bits[lencode],
            storage_ix, storage);
  WriteBits(code.length_nextra[block_ix], code.length_extra[block_ix],
            storage_ix, storage);
}

static void BuildAndStoreBlockSplitCode(const std::vector<uint8_t>& types,
                                        const std::vector<uint32_t>& lengths,
                                        const size_t num_types,
                                        HuffmanTree* tree,
                                        BlockSplitCode* code,
                                        size_t* storage_ix,
                                        uint8_t* storage) {
  const size_t num_blocks = types.size();
  uint32_t type_histo[kBlockTypeAlphabetSize];
  uint32_t length_histo[kNumBlockLenPrefixes];
  memset(type_histo, 0, (num_types + 2) * sizeof(type_histo[0]));
  memset(length_histo, 0, sizeof(length_histo));
  size_t last_type = 1;
  size_t second_last_type = 0;
  code->type_code.resize(num_blocks);
  code->length_prefix.resize(num_blocks);
  code->length_nextra.resize(num_blocks);
  code->length_extra.resize(num_blocks);
  code->type_depths.resize(num_types + 2);
  code->type_bits.resize(num_types + 2);
  memset(code->length_depths, 0, sizeof(code->length_depths));
  memset(code->length_bits, 0, sizeof(code->length_bits));
  for (size_t i = 0; i < num_blocks; ++i) {
    size_t type = types[i];
    size_t type_code = (type == last_type + 1 ? 1 :
                     type == second_last_type ? 0 :
                     type + 2);
    second_last_type = last_type;
    last_type = type;
    code->type_code[i] = static_cast<uint32_t>(type_code);
    if (i != 0) ++type_histo[type_code];
    GetBlockLengthPrefixCode(lengths[i],
                             &code->length_prefix[i],
                             &code->length_nextra[i],
                             &code->length_extra[i]);
    ++length_histo[code->length_prefix[i]];
  }
  StoreVarLenUint8(num_types - 1, storage_ix, storage);
  if (num_types > 1) {
    BuildAndStoreHuffmanTree(&type_histo[0], num_types + 2, tree,
                             &code->type_depths[0], &code->type_bits[0],
                             storage_ix, storage);
    BuildAndStoreHuffmanTree(&length_histo[0], kNumBlockLenPrefixes, tree,
                             &code->length_depths[0], &code->length_bits[0],
                             storage_ix, storage);
    StoreBlockSwitch(*code, 0, storage_ix, storage);
  }
}

void StoreTrivialContextMap(size_t num_types,
                            size_t context_bits,
                            HuffmanTree* tree,
                            size_t* storage_ix,
                            uint8_t* storage) {
  StoreVarLenUint8(num_types - 1, storage_ix, storage);
  if (num_types > 1) {
    size_t repeat_code = context_bits - 1u;
    size_t repeat_bits = (1u << repeat_code) - 1u;
    size_t alphabet_size = num_types + repeat_code;
    uint32_t histogram[kContextMapAlphabetSize];
    uint8_t depths[kContextMapAlphabetSize];
    uint16_t bits[kContextMapAlphabetSize];
    memset(histogram, 0, alphabet_size * sizeof(histogram[0]));
    memset(depths, 0, alphabet_size * sizeof(depths[0]));
    memset(bits, 0, alphabet_size * sizeof(bits[0]));
    // Write RLEMAX.
    WriteBits(1, 1, storage_ix, storage);
    WriteBits(4, repeat_code - 1, storage_ix, storage);
    histogram[repeat_code] = static_cast<uint32_t>(num_types);
    histogram[0] = 1;
    for (size_t i = context_bits; i < alphabet_size; ++i) {
      histogram[i] = 1;
    }
    BuildAndStoreHuffmanTree(&histogram[0], alphabet_size, tree,
                             &depths[0], &bits[0],
                             storage_ix, storage);
    for (size_t i = 0; i < num_types; ++i) {
      size_t code = (i == 0 ? 0 : i + context_bits - 1);
      WriteBits(depths[code], bits[code], storage_ix, storage);
      WriteBits(depths[repeat_code], bits[repeat_code], storage_ix, storage);
      WriteBits(repeat_code, repeat_bits, storage_ix, storage);
    }
    // Write IMTF (inverse-move-to-front) bit.
    WriteBits(1, 1, storage_ix, storage);
  }
}

// Manages the encoding of one block category (literal, command or distance).
class BlockEncoder {
 public:
  BlockEncoder(size_t alphabet_size,
               size_t num_block_types,
               const std::vector<uint8_t>& block_types,
               const std::vector<uint32_t>& block_lengths)
      : alphabet_size_(alphabet_size),
        num_block_types_(num_block_types),
        block_types_(block_types),
        block_lengths_(block_lengths),
        block_ix_(0),
        block_len_(block_lengths.empty() ? 0 : block_lengths[0]),
        entropy_ix_(0) {}

  // Creates entropy codes of block lengths and block types and stores them
  // to the bit stream.
  void BuildAndStoreBlockSwitchEntropyCodes(HuffmanTree* tree,
                                            size_t* storage_ix,
                                            uint8_t* storage) {
    BuildAndStoreBlockSplitCode(
        block_types_, block_lengths_, num_block_types_,
        tree, &block_split_code_, storage_ix, storage);
  }

  // Creates entropy codes for all block types and stores them to the bit
  // stream.
  template<int kSize>
  void BuildAndStoreEntropyCodes(
      const std::vector<Histogram<kSize> >& histograms,
      HuffmanTree* tree,
      size_t* storage_ix, uint8_t* storage) {
    depths_.resize(histograms.size() * alphabet_size_);
    bits_.resize(histograms.size() * alphabet_size_);
    for (size_t i = 0; i < histograms.size(); ++i) {
      size_t ix = i * alphabet_size_;
      BuildAndStoreHuffmanTree(&histograms[i].data_[0], alphabet_size_,
                               tree,
                               &depths_[ix], &bits_[ix],
                               storage_ix, storage);
    }
  }

  // Stores the next symbol with the entropy code of the current block type.
  // Updates the block type and block length at block boundaries.
  void StoreSymbol(size_t symbol, size_t* storage_ix, uint8_t* storage) {
    if (block_len_ == 0) {
      ++block_ix_;
      block_len_ = block_lengths_[block_ix_];
      entropy_ix_ = block_types_[block_ix_] * alphabet_size_;
      StoreBlockSwitch(block_split_code_, block_ix_, storage_ix, storage);
    }
    --block_len_;
    size_t ix = entropy_ix_ + symbol;
    WriteBits(depths_[ix], bits_[ix], storage_ix, storage);
  }

  // Stores the next symbol with the entropy code of the current block type and
  // context value.
  // Updates the block type and block length at block boundaries.
  template<int kContextBits>
  void StoreSymbolWithContext(size_t symbol, size_t context,
                              const std::vector<uint32_t>& context_map,
                              size_t* storage_ix, uint8_t* storage) {
    if (block_len_ == 0) {
      ++block_ix_;
      block_len_ = block_lengths_[block_ix_];
      size_t block_type = block_types_[block_ix_];
      entropy_ix_ = block_type << kContextBits;
      StoreBlockSwitch(block_split_code_, block_ix_, storage_ix, storage);
    }
    --block_len_;
    size_t histo_ix = context_map[entropy_ix_ + context];
    size_t ix = histo_ix * alphabet_size_ + symbol;
    WriteBits(depths_[ix], bits_[ix], storage_ix, storage);
  }

 private:
  const size_t alphabet_size_;
  const size_t num_block_types_;
  const std::vector<uint8_t>& block_types_;
  const std::vector<uint32_t>& block_lengths_;
  BlockSplitCode block_split_code_;
  size_t block_ix_;
  size_t block_len_;
  size_t entropy_ix_;
  std::vector<uint8_t> depths_;
  std::vector<uint16_t> bits_;
};

static void JumpToByteBoundary(size_t* storage_ix, uint8_t* storage) {
  *storage_ix = (*storage_ix + 7u) & ~7u;
  storage[*storage_ix >> 3] = 0;
}

void StoreMetaBlock(const uint8_t* input,
                    size_t start_pos,
                    size_t length,
                    size_t mask,
                    uint8_t prev_byte,
                    uint8_t prev_byte2,
                    bool is_last,
                    uint32_t num_direct_distance_codes,
                    uint32_t distance_postfix_bits,
                    ContextType literal_context_mode,
                    const brotli::Command *commands,
                    size_t n_commands,
                    const MetaBlockSplit& mb,
                    size_t *storage_ix,
                    uint8_t *storage) {
  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  size_t num_distance_codes =
      kNumDistanceShortCodes + num_direct_distance_codes +
      (48u << distance_postfix_bits);

  HuffmanTree* tree = static_cast<HuffmanTree*>(
      malloc(kMaxHuffmanTreeSize * sizeof(HuffmanTree)));
  BlockEncoder literal_enc(256,
                           mb.literal_split.num_types,
                           mb.literal_split.types,
                           mb.literal_split.lengths);
  BlockEncoder command_enc(kNumCommandPrefixes,
                           mb.command_split.num_types,
                           mb.command_split.types,
                           mb.command_split.lengths);
  BlockEncoder distance_enc(num_distance_codes,
                            mb.distance_split.num_types,
                            mb.distance_split.types,
                            mb.distance_split.lengths);

  literal_enc.BuildAndStoreBlockSwitchEntropyCodes(tree, storage_ix, storage);
  command_enc.BuildAndStoreBlockSwitchEntropyCodes(tree, storage_ix, storage);
  distance_enc.BuildAndStoreBlockSwitchEntropyCodes(tree, storage_ix, storage);

  WriteBits(2, distance_postfix_bits, storage_ix, storage);
  WriteBits(4, num_direct_distance_codes >> distance_postfix_bits,
            storage_ix, storage);
  for (size_t i = 0; i < mb.literal_split.num_types; ++i) {
    WriteBits(2, literal_context_mode, storage_ix, storage);
  }

  size_t num_literal_histograms = mb.literal_histograms.size();
  if (mb.literal_context_map.empty()) {
    StoreTrivialContextMap(num_literal_histograms, kLiteralContextBits, tree,
                           storage_ix, storage);
  } else {
    EncodeContextMap(mb.literal_context_map, num_literal_histograms, tree,
                     storage_ix, storage);
  }

  size_t num_dist_histograms = mb.distance_histograms.size();
  if (mb.distance_context_map.empty()) {
    StoreTrivialContextMap(num_dist_histograms, kDistanceContextBits, tree,
                           storage_ix, storage);
  } else {
    EncodeContextMap(mb.distance_context_map, num_dist_histograms, tree,
                     storage_ix, storage);
  }

  literal_enc.BuildAndStoreEntropyCodes(mb.literal_histograms, tree,
                                        storage_ix, storage);
  command_enc.BuildAndStoreEntropyCodes(mb.command_histograms, tree,
                                        storage_ix, storage);
  distance_enc.BuildAndStoreEntropyCodes(mb.distance_histograms, tree,
                                         storage_ix, storage);
  free(tree);

  size_t pos = start_pos;
  for (size_t i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    size_t cmd_code = cmd.cmd_prefix_;
    command_enc.StoreSymbol(cmd_code, storage_ix, storage);
    StoreCommandExtra(cmd, storage_ix, storage);
    if (mb.literal_context_map.empty()) {
      for (size_t j = cmd.insert_len_; j != 0; --j) {
        literal_enc.StoreSymbol(input[pos & mask], storage_ix, storage);
        ++pos;
      }
    } else {
      for (size_t j = cmd.insert_len_; j != 0; --j) {
        size_t context = Context(prev_byte, prev_byte2, literal_context_mode);
        uint8_t literal = input[pos & mask];
        literal_enc.StoreSymbolWithContext<kLiteralContextBits>(
            literal, context, mb.literal_context_map, storage_ix, storage);
        prev_byte2 = prev_byte;
        prev_byte = literal;
        ++pos;
      }
    }
    pos += cmd.copy_len();
    if (cmd.copy_len()) {
      prev_byte2 = input[(pos - 2) & mask];
      prev_byte = input[(pos - 1) & mask];
      if (cmd.cmd_prefix_ >= 128) {
        size_t dist_code = cmd.dist_prefix_;
        uint32_t distnumextra = cmd.dist_extra_ >> 24;
        uint64_t distextra = cmd.dist_extra_ & 0xffffff;
        if (mb.distance_context_map.empty()) {
          distance_enc.StoreSymbol(dist_code, storage_ix, storage);
        } else {
          size_t context = cmd.DistanceContext();
          distance_enc.StoreSymbolWithContext<kDistanceContextBits>(
              dist_code, context, mb.distance_context_map, storage_ix, storage);
        }
        brotli::WriteBits(distnumextra, distextra, storage_ix, storage);
      }
    }
  }
  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

static void BuildHistograms(const uint8_t* input,
                            size_t start_pos,
                            size_t mask,
                            const brotli::Command *commands,
                            size_t n_commands,
                            HistogramLiteral* lit_histo,
                            HistogramCommand* cmd_histo,
                            HistogramDistance* dist_histo) {
  size_t pos = start_pos;
  for (size_t i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    cmd_histo->Add(cmd.cmd_prefix_);
    for (size_t j = cmd.insert_len_; j != 0; --j) {
      lit_histo->Add(input[pos & mask]);
      ++pos;
    }
    pos += cmd.copy_len();
    if (cmd.copy_len() && cmd.cmd_prefix_ >= 128) {
      dist_histo->Add(cmd.dist_prefix_);
    }
  }
}

static void StoreDataWithHuffmanCodes(const uint8_t* input,
                                      size_t start_pos,
                                      size_t mask,
                                      const brotli::Command *commands,
                                      size_t n_commands,
                                      const uint8_t* lit_depth,
                                      const uint16_t* lit_bits,
                                      const uint8_t* cmd_depth,
                                      const uint16_t* cmd_bits,
                                      const uint8_t* dist_depth,
                                      const uint16_t* dist_bits,
                                      size_t* storage_ix,
                                      uint8_t* storage) {
  size_t pos = start_pos;
  for (size_t i = 0; i < n_commands; ++i) {
    const Command cmd = commands[i];
    const size_t cmd_code = cmd.cmd_prefix_;
    WriteBits(cmd_depth[cmd_code], cmd_bits[cmd_code], storage_ix, storage);
    StoreCommandExtra(cmd, storage_ix, storage);
    for (size_t j = cmd.insert_len_; j != 0; --j) {
      const uint8_t literal = input[pos & mask];
      WriteBits(lit_depth[literal], lit_bits[literal], storage_ix, storage);
      ++pos;
    }
    pos += cmd.copy_len();
    if (cmd.copy_len() && cmd.cmd_prefix_ >= 128) {
      const size_t dist_code = cmd.dist_prefix_;
      const uint32_t distnumextra = cmd.dist_extra_ >> 24;
      const uint32_t distextra = cmd.dist_extra_ & 0xffffff;
      WriteBits(dist_depth[dist_code], dist_bits[dist_code],
                storage_ix, storage);
      WriteBits(distnumextra, distextra, storage_ix, storage);
    }
  }
}

void StoreMetaBlockTrivial(const uint8_t* input,
                           size_t start_pos,
                           size_t length,
                           size_t mask,
                           bool is_last,
                           const brotli::Command *commands,
                           size_t n_commands,
                           size_t *storage_ix,
                           uint8_t *storage) {
  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  HistogramLiteral lit_histo;
  HistogramCommand cmd_histo;
  HistogramDistance dist_histo;

  BuildHistograms(input, start_pos, mask, commands, n_commands,
                  &lit_histo, &cmd_histo, &dist_histo);

  WriteBits(13, 0, storage_ix, storage);

  std::vector<uint8_t> lit_depth(256);
  std::vector<uint16_t> lit_bits(256);
  std::vector<uint8_t> cmd_depth(kNumCommandPrefixes);
  std::vector<uint16_t> cmd_bits(kNumCommandPrefixes);
  std::vector<uint8_t> dist_depth(64);
  std::vector<uint16_t> dist_bits(64);

  HuffmanTree* tree = static_cast<HuffmanTree*>(
      malloc(kMaxHuffmanTreeSize * sizeof(HuffmanTree)));
  BuildAndStoreHuffmanTree(&lit_histo.data_[0], 256, tree,
                           &lit_depth[0], &lit_bits[0],
                           storage_ix, storage);
  BuildAndStoreHuffmanTree(&cmd_histo.data_[0], kNumCommandPrefixes, tree,
                           &cmd_depth[0], &cmd_bits[0],
                           storage_ix, storage);
  BuildAndStoreHuffmanTree(&dist_histo.data_[0], 64, tree,
                           &dist_depth[0], &dist_bits[0],
                           storage_ix, storage);
  free(tree);
  StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                            n_commands, &lit_depth[0], &lit_bits[0],
                            &cmd_depth[0], &cmd_bits[0],
                            &dist_depth[0], &dist_bits[0],
                            storage_ix, storage);
  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

void StoreMetaBlockFast(const uint8_t* input,
                        size_t start_pos,
                        size_t length,
                        size_t mask,
                        bool is_last,
                        const brotli::Command *commands,
                        size_t n_commands,
                        size_t *storage_ix,
                        uint8_t *storage) {
  StoreCompressedMetaBlockHeader(is_last, length, storage_ix, storage);

  WriteBits(13, 0, storage_ix, storage);

  if (n_commands <= 128) {
    uint32_t histogram[256] = { 0 };
    size_t pos = start_pos;
    size_t num_literals = 0;
    for (size_t i = 0; i < n_commands; ++i) {
      const Command cmd = commands[i];
      for (size_t j = cmd.insert_len_; j != 0; --j) {
        ++histogram[input[pos & mask]];
        ++pos;
      }
      num_literals += cmd.insert_len_;
      pos += cmd.copy_len();
    }
    uint8_t lit_depth[256] = { 0 };
    uint16_t lit_bits[256] = { 0 };
    BuildAndStoreHuffmanTreeFast(histogram, num_literals,
                                 /* max_bits = */ 8,
                                 lit_depth, lit_bits,
                                 storage_ix, storage);
    StoreStaticCommandHuffmanTree(storage_ix, storage);
    StoreStaticDistanceHuffmanTree(storage_ix, storage);
    StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                              n_commands, &lit_depth[0], &lit_bits[0],
                              kStaticCommandCodeDepth,
                              kStaticCommandCodeBits,
                              kStaticDistanceCodeDepth,
                              kStaticDistanceCodeBits,
                              storage_ix, storage);
  } else {
    HistogramLiteral lit_histo;
    HistogramCommand cmd_histo;
    HistogramDistance dist_histo;
    BuildHistograms(input, start_pos, mask, commands, n_commands,
                    &lit_histo, &cmd_histo, &dist_histo);
    std::vector<uint8_t> lit_depth(256);
    std::vector<uint16_t> lit_bits(256);
    std::vector<uint8_t> cmd_depth(kNumCommandPrefixes);
    std::vector<uint16_t> cmd_bits(kNumCommandPrefixes);
    std::vector<uint8_t> dist_depth(64);
    std::vector<uint16_t> dist_bits(64);
    BuildAndStoreHuffmanTreeFast(&lit_histo.data_[0], lit_histo.total_count_,
                                 /* max_bits = */ 8,
                                 &lit_depth[0], &lit_bits[0],
                                 storage_ix, storage);
    BuildAndStoreHuffmanTreeFast(&cmd_histo.data_[0], cmd_histo.total_count_,
                                 /* max_bits = */ 10,
                                 &cmd_depth[0], &cmd_bits[0],
                                 storage_ix, storage);
    BuildAndStoreHuffmanTreeFast(&dist_histo.data_[0], dist_histo.total_count_,
                                 /* max_bits = */ 6,
                                 &dist_depth[0], &dist_bits[0],
                                 storage_ix, storage);
    StoreDataWithHuffmanCodes(input, start_pos, mask, commands,
                              n_commands, &lit_depth[0], &lit_bits[0],
                              &cmd_depth[0], &cmd_bits[0],
                              &dist_depth[0], &dist_bits[0],
                              storage_ix, storage);
  }

  if (is_last) {
    JumpToByteBoundary(storage_ix, storage);
  }
}

// This is for storing uncompressed blocks (simple raw storage of
// bytes-as-bytes).
void StoreUncompressedMetaBlock(bool final_block,
                                const uint8_t * __restrict input,
                                size_t position, size_t mask,
                                size_t len,
                                size_t * __restrict storage_ix,
                                uint8_t * __restrict storage) {
  StoreUncompressedMetaBlockHeader(len, storage_ix, storage);
  JumpToByteBoundary(storage_ix, storage);

  size_t masked_pos = position & mask;
  if (masked_pos + len > mask + 1) {
    size_t len1 = mask + 1 - masked_pos;
    memcpy(&storage[*storage_ix >> 3], &input[masked_pos], len1);
    *storage_ix += len1 << 3;
    len -= len1;
    masked_pos = 0;
  }
  memcpy(&storage[*storage_ix >> 3], &input[masked_pos], len);
  *storage_ix += len << 3;

  // We need to clear the next 4 bytes to continue to be
  // compatible with WriteBits.
  brotli::WriteBitsPrepareStorage(*storage_ix, storage);

  // Since the uncompressed block itself may not be the final block, add an
  // empty one after this.
  if (final_block) {
    brotli::WriteBits(1, 1, storage_ix, storage);  // islast
    brotli::WriteBits(1, 1, storage_ix, storage);  // isempty
    JumpToByteBoundary(storage_ix, storage);
  }
}

void StoreSyncMetaBlock(size_t * __restrict storage_ix,
                        uint8_t * __restrict storage) {
  // Empty metadata meta-block bit pattern:
  //   1 bit:  is_last (0)
  //   2 bits: num nibbles (3)
  //   1 bit:  reserved (0)
  //   2 bits: metadata length bytes (0)
  WriteBits(6, 6, storage_ix, storage);
  JumpToByteBoundary(storage_ix, storage);
}

}  // namespace brotli
