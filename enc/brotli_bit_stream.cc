// Copyright 2014 Google Inc. All Rights Reserved.
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
// Brotli bit stream functions to support the low level format. There are no
// compression algorithms here, just the right ordering of bits to match the
// specs.

#include "./brotli_bit_stream.h"

#include <vector>

#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./write_bits.h"

namespace brotli {

// returns false if fail
// nibblesbits represents the 2 bits to encode MNIBBLES (0-3)
bool EncodeMlen(size_t length, int* bits, int* numbits, int* nibblesbits) {
  length--;  // MLEN - 1 is encoded
  int lg = length == 0 ? 1 : Log2Floor(length) + 1;
  if (lg > 28) return false;
  int mnibbles = (lg < 16 ? 16 : (lg + 3)) / 4;
  *nibblesbits = mnibbles - 4;
  *numbits = mnibbles * 4;
  *bits = length;
  return true;
}

void StoreVarLenUint8(int n, int* storage_ix, uint8_t* storage) {
  if (n == 0) {
    WriteBits(1, 0, storage_ix, storage);
  } else {
    WriteBits(1, 1, storage_ix, storage);
    int nbits = Log2Floor(n);
    WriteBits(3, nbits, storage_ix, storage);
    WriteBits(nbits, n - (1 << nbits), storage_ix, storage);
  }
}

void StoreCompressedMetaBlockHeader(bool final_block,
                                    int length,
                                    int* storage_ix,
                                    uint8_t* storage) {
  // Write ISLAST bit.
  WriteBits(1, final_block, storage_ix, storage);
  // Write ISEMPTY bit.
  if (final_block) WriteBits(1, length == 0, storage_ix, storage);

  int lenbits;
  int nlenbits;
  int nibblesbits;
  EncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);

  WriteBits(2, nibblesbits, storage_ix, storage);
  WriteBits(nlenbits, lenbits, storage_ix, storage);

  if (!final_block) {
    // Write ISUNCOMPRESSED bit.
    WriteBits(1, 0, storage_ix, storage);
  }
}

void StoreUncompressedMetaBlockHeader(int length,
                                      int* storage_ix,
                                      uint8_t* storage) {
  // Write ISLAST bit. Uncompressed block cannot be the last one, so set to 0.
  WriteBits(1, 0, storage_ix, storage);
  int lenbits;
  int nlenbits;
  int nibblesbits;
  EncodeMlen(length, &lenbits, &nlenbits, &nibblesbits);
  WriteBits(2, nibblesbits, storage_ix, storage);
  WriteBits(nlenbits, lenbits, storage_ix, storage);
  // Write ISUNCOMPRESSED bit.
  WriteBits(1, 1, storage_ix, storage);
}

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const int num_codes,
    const uint8_t *code_length_bitdepth,
    int *storage_ix,
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
  int codes_to_store = kCodeLengthCodes;
  if (num_codes > 1) {
    for (; codes_to_store > 0; --codes_to_store) {
      if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
        break;
      }
    }
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
    uint8_t l = code_length_bitdepth[kStorageOrder[i]];
    WriteBits(kHuffmanBitLengthHuffmanCodeBitLengths[l],
              kHuffmanBitLengthHuffmanCodeSymbols[l], storage_ix, storage);
  }
}

void StoreHuffmanTreeToBitMask(
    const std::vector<uint8_t> &huffman_tree,
    const std::vector<uint8_t> &huffman_tree_extra_bits,
    const uint8_t *code_length_bitdepth,
    const std::vector<uint16_t> &code_length_bitdepth_symbols,
    int * __restrict storage_ix,
    uint8_t * __restrict storage) {
  for (int i = 0; i < huffman_tree.size(); ++i) {
    int ix = huffman_tree[i];
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

void StoreSimpleHuffmanTree(const uint8_t* depths,
                            int symbols[4],
                            int num_symbols,
                            int max_bits,
                            int *storage_ix, uint8_t *storage) {
  // value of 1 indicates a simple Huffman code
  WriteBits(2, 1, storage_ix, storage);
  WriteBits(2, num_symbols - 1, storage_ix, storage);  // NSYM - 1

  // Sort
  for (int i = 0; i < num_symbols; i++) {
    for (int j = i + 1; j < num_symbols; j++) {
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
                      int quality,
                      int *storage_ix, uint8_t *storage) {
  // Write the Huffman tree into the brotli-representation.
  std::vector<uint8_t> huffman_tree;
  std::vector<uint8_t> huffman_tree_extra_bits;
  // TODO: Consider allocating these from stack.
  huffman_tree.reserve(256);
  huffman_tree_extra_bits.reserve(256);
  WriteHuffmanTree(depths, num, &huffman_tree, &huffman_tree_extra_bits);

  // Calculate the statistics of the Huffman tree in brotli-representation.
  int huffman_tree_histogram[kCodeLengthCodes] = { 0 };
  for (int i = 0; i < huffman_tree.size(); ++i) {
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
  // TODO: Consider allocating these from stack.
  uint8_t code_length_bitdepth[kCodeLengthCodes] = { 0 };
  std::vector<uint16_t> code_length_bitdepth_symbols(kCodeLengthCodes);
  CreateHuffmanTree(&huffman_tree_histogram[0], kCodeLengthCodes,
                    5, quality, &code_length_bitdepth[0]);
  ConvertBitDepthsToSymbols(code_length_bitdepth, kCodeLengthCodes,
                            code_length_bitdepth_symbols.data());

  // Now, we have all the data, let's start storing it
  StoreHuffmanTreeOfHuffmanTreeToBitMask(num_codes, code_length_bitdepth,
                                         storage_ix, storage);

  if (num_codes == 1) {
    code_length_bitdepth[code] = 0;
  }

  // Store the real huffman tree now.
  StoreHuffmanTreeToBitMask(huffman_tree,
                            huffman_tree_extra_bits,
                            &code_length_bitdepth[0],
                            code_length_bitdepth_symbols,
                            storage_ix, storage);
}

void BuildAndStoreHuffmanTree(const int *histogram,
                              const int length,
                              const int quality,
                              uint8_t* depth,
                              uint16_t* bits,
                              int* storage_ix,
                              uint8_t* storage) {
  int count = 0;
  int s4[4] = { 0 };
  for (size_t i = 0; i < length; i++) {
    if (histogram[i]) {
      if (count < 4) {
        s4[count] = i;
      } else if (quality < 3 && count > 4) {
        break;
      }
      count++;
    }
  }

  int max_bits_counter = length - 1;
  int max_bits = 0;
  while (max_bits_counter) {
    max_bits_counter >>= 1;
    ++max_bits;
  }

  if (count <= 1) {
    WriteBits(4, 1, storage_ix, storage);
    WriteBits(max_bits, s4[0], storage_ix, storage);
    return;
  }

  if (length >= 50 && count >= 16 && quality >= 3) {
    std::vector<int> counts(length);
    memcpy(&counts[0], histogram, sizeof(counts[0]) * length);
    OptimizeHuffmanCountsForRle(length, &counts[0]);
    CreateHuffmanTree(&counts[0], length, 15, quality, depth);
  } else {
    CreateHuffmanTree(histogram, length, 15, quality, depth);
  }
  ConvertBitDepthsToSymbols(depth, length, bits);

  if (count <= 4) {
    StoreSimpleHuffmanTree(depth, s4, count, max_bits, storage_ix, storage);
  } else {
    StoreHuffmanTree(depth, length, quality, storage_ix, storage);
  }
}

void StoreTrivialContextMap(int num_types,
                            int context_bits,
                            int* storage_ix,
                            uint8_t* storage) {
  StoreVarLenUint8(num_types - 1, storage_ix, storage);
  if (num_types > 1) {
    int repeat_code = context_bits - 1;
    int repeat_bits = (1 << repeat_code) - 1;
    int alphabet_size = num_types + repeat_code;
    std::vector<int> histogram(alphabet_size);
    std::vector<uint8_t> depths(alphabet_size);
    std::vector<uint16_t> bits(alphabet_size);
    // Write RLEMAX.
    WriteBits(1, 1, storage_ix, storage);
    WriteBits(4, repeat_code - 1, storage_ix, storage);
    histogram[repeat_code] = num_types;
    histogram[0] = 1;
    for (int i = context_bits; i < alphabet_size; ++i) {
      histogram[i] = 1;
    }
    BuildAndStoreHuffmanTree(&histogram[0], alphabet_size, 1,
                             &depths[0], &bits[0],
                             storage_ix, storage);
    for (int i = 0; i < num_types; ++i) {
      int code = (i == 0 ? 0 : i + context_bits - 1);
      WriteBits(depths[code], bits[code], storage_ix, storage);
      WriteBits(depths[repeat_code], bits[repeat_code], storage_ix, storage);
      WriteBits(repeat_code, repeat_bits, storage_ix, storage);
    }
    // Write IMTF (inverse-move-to-front) bit.
    WriteBits(1, 1, storage_ix, storage);
  }
}

}  // namespace brotli
