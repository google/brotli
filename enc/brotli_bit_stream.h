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
// Functions to convert brotli-related data structures into the
// brotli bit stream. The functions here operate under
// assumption that there is enough space in the storage, i.e., there are
// no out-of-range checks anywhere.
//
// These functions do bit addressing into a byte array. The byte array
// is called "storage" and the index to the bit is called storage_ix
// in function arguments.

#ifndef BROTLI_ENC_BROTLI_BIT_STREAM_H_
#define BROTLI_ENC_BROTLI_BIT_STREAM_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace brotli {

// All Store functions here will use a storage_ix, which is always the bit
// position for the current storage.

// Stores a number between 0 and 255.
void StoreVarLenUint8(int n, int* storage_ix, uint8_t* storage);

// Stores the compressed meta-block header.
bool StoreCompressedMetaBlockHeader(bool final_block,
                                    int length,
                                    int* storage_ix,
                                    uint8_t* storage);

// Stores the uncompressed meta-block header.
bool StoreUncompressedMetaBlockHeader(int length,
                                      int* storage_ix,
                                      uint8_t* storage);

// Stores a context map where the histogram type is always the block type.
void StoreTrivialContextMap(int num_types,
                            int context_bits,
                            int* storage_ix,
                            uint8_t* storage);

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const int num_codes,
    const uint8_t *code_length_bitdepth,
    int *storage_ix,
    uint8_t *storage);

// Builds a Huffman tree from histogram[0:length] into depth[0:length] and
// bits[0:length] and stores the encoded tree to the bit stream.
void BuildAndStoreHuffmanTree(const int *histogram,
                              const int length,
                              const int quality,
                              uint8_t* depth,
                              uint16_t* bits,
                              int* storage_ix,
                              uint8_t* storage);

// Encodes the given context map to the bit stream. The number of different
// histogram ids is given by num_clusters.
void EncodeContextMap(const std::vector<int>& context_map,
                      int num_clusters,
                      int* storage_ix, uint8_t* storage);

// Data structure that stores everything that is needed to encode each block
// block switch command.
struct BlockSplitCode {
  std::vector<int> type_code;
  std::vector<int> length_prefix;
  std::vector<int> length_nextra;
  std::vector<int> length_extra;
  std::vector<uint8_t> type_depths;
  std::vector<uint16_t> type_bits;
  std::vector<uint8_t> length_depths;
  std::vector<uint16_t> length_bits;
};

// Builds a BlockSplitCode data structure from the block split given by the
// vector of block types and block lengths and stores it to the bit stream.
void BuildAndStoreBlockSplitCode(const std::vector<int>& types,
                                 const std::vector<int>& lengths,
                                 const int num_types,
                                 const int quality,
                                 BlockSplitCode* code,
                                 int* storage_ix,
                                 uint8_t* storage);

// Stores the block switch command with index block_ix to the bit stream.
void StoreBlockSwitch(const BlockSplitCode& code,
                      const int block_ix,
                      int* storage_ix,
                      uint8_t* storage);

}  // namespace brotli

#endif  // BROTLI_ENC_BROTLI_BIT_STREAM_H_
