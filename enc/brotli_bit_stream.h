/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

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

#include <vector>

#include "./entropy_encode.h"
#include "./metablock.h"
#include "./types.h"

namespace brotli {

// All Store functions here will use a storage_ix, which is always the bit
// position for the current storage.

// Stores a number between 0 and 255.
void StoreVarLenUint8(size_t n, size_t* storage_ix, uint8_t* storage);

// Stores the compressed meta-block header.
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreCompressedMetaBlockHeader(bool final_block,
                                    size_t length,
                                    size_t* storage_ix,
                                    uint8_t* storage);

// Stores the uncompressed meta-block header.
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreUncompressedMetaBlockHeader(size_t length,
                                      size_t* storage_ix,
                                      uint8_t* storage);

// Stores a context map where the histogram type is always the block type.
void StoreTrivialContextMap(size_t num_types,
                            size_t context_bits,
                            HuffmanTree* tree,
                            size_t* storage_ix,
                            uint8_t* storage);

void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    const int num_codes,
    const uint8_t *code_length_bitdepth,
    size_t *storage_ix,
    uint8_t *storage);

void StoreHuffmanTree(const uint8_t* depths, size_t num, HuffmanTree* tree,
                      size_t *storage_ix, uint8_t *storage);

// Builds a Huffman tree from histogram[0:length] into depth[0:length] and
// bits[0:length] and stores the encoded tree to the bit stream.
void BuildAndStoreHuffmanTree(const uint32_t *histogram,
                              const size_t length,
                              HuffmanTree* tree,
                              uint8_t* depth,
                              uint16_t* bits,
                              size_t* storage_ix,
                              uint8_t* storage);

void BuildAndStoreHuffmanTreeFast(const uint32_t *histogram,
                                  const size_t histogram_total,
                                  const size_t max_bits,
                                  uint8_t* depth,
                                  uint16_t* bits,
                                  size_t* storage_ix,
                                  uint8_t* storage);

// Encodes the given context map to the bit stream. The number of different
// histogram ids is given by num_clusters.
void EncodeContextMap(const std::vector<uint32_t>& context_map,
                      size_t num_clusters,
                      HuffmanTree* tree,
                      size_t* storage_ix, uint8_t* storage);

// Data structure that stores everything that is needed to encode each block
// switch command.
struct BlockSplitCode {
  std::vector<uint32_t> type_code;
  std::vector<uint32_t> length_prefix;
  std::vector<uint32_t> length_nextra;
  std::vector<uint32_t> length_extra;
  std::vector<uint8_t> type_depths;
  std::vector<uint16_t> type_bits;
  uint8_t length_depths[kNumBlockLenPrefixes];
  uint16_t length_bits[kNumBlockLenPrefixes];
};

// Builds a BlockSplitCode data structure from the block split given by the
// vector of block types and block lengths and stores it to the bit stream.
void BuildAndStoreBlockSplitCode(const std::vector<uint8_t>& types,
                                 const std::vector<uint32_t>& lengths,
                                 const size_t num_types,
                                 BlockSplitCode* code,
                                 size_t* storage_ix,
                                 uint8_t* storage);

// Stores the block switch command with index block_ix to the bit stream.
void StoreBlockSwitch(const BlockSplitCode& code,
                      const size_t block_ix,
                      size_t* storage_ix,
                      uint8_t* storage);

// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreMetaBlock(const uint8_t* input,
                    size_t start_pos,
                    size_t length,
                    size_t mask,
                    uint8_t prev_byte,
                    uint8_t prev_byte2,
                    bool final_block,
                    uint32_t num_direct_distance_codes,
                    uint32_t distance_postfix_bits,
                    ContextType literal_context_mode,
                    const brotli::Command *commands,
                    size_t n_commands,
                    const MetaBlockSplit& mb,
                    size_t *storage_ix,
                    uint8_t *storage);

// Stores the meta-block without doing any block splitting, just collects
// one histogram per block category and uses that for entropy coding.
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreMetaBlockTrivial(const uint8_t* input,
                           size_t start_pos,
                           size_t length,
                           size_t mask,
                           bool is_last,
                           const brotli::Command *commands,
                           size_t n_commands,
                           size_t *storage_ix,
                           uint8_t *storage);

// Same as above, but uses static prefix codes for histograms with a only a few
// symbols, and uses static code length prefix codes for all other histograms.
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreMetaBlockFast(const uint8_t* input,
                        size_t start_pos,
                        size_t length,
                        size_t mask,
                        bool is_last,
                        const brotli::Command *commands,
                        size_t n_commands,
                        size_t *storage_ix,
                        uint8_t *storage);

// This is for storing uncompressed blocks (simple raw storage of
// bytes-as-bytes).
// REQUIRES: length > 0
// REQUIRES: length <= (1 << 24)
void StoreUncompressedMetaBlock(bool final_block,
                                const uint8_t* input,
                                size_t position, size_t mask,
                                size_t len,
                                size_t* storage_ix,
                                uint8_t* storage);

// Stores an empty metadata meta-block and syncs to a byte boundary.
void StoreSyncMetaBlock(size_t* storage_ix, uint8_t* storage);

}  // namespace brotli

#endif  // BROTLI_ENC_BROTLI_BIT_STREAM_H_
