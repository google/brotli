/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Function for fast encoding of an input fragment, independently from the input
// history. This function uses one-pass processing: when we find a backward
// match, we immediately emit the corresponding command and literal codes to
// the bit stream.
//
// Adapted from the CompressFragment() function in
// https://github.com/google/snappy/blob/master/snappy.cc

#include "./compress_fragment.h"

#include <algorithm>
#include <cstring>

#include "./brotli_bit_stream.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./port.h"
#include "./write_bits.h"

namespace brotli {

// kHashMul32 multiplier has these properties:
// * The multiplier must be odd. Otherwise we may lose the highest bit.
// * No long streaks of 1s or 0s.
// * There is no effort to ensure that it is a prime, the oddity is enough
//   for this use.
// * The number has been tuned heuristically against compression benchmarks.
static const uint32_t kHashMul32 = 0x1e35a7bd;

static inline uint32_t Hash(const uint8_t* p, size_t shift) {
  const uint64_t h = (BROTLI_UNALIGNED_LOAD64(p) << 24) * kHashMul32;
  return static_cast<uint32_t>(h >> shift);
}

static inline uint32_t HashBytesAtOffset(uint64_t v, int offset, size_t shift) {
  assert(offset >= 0);
  assert(offset <= 3);
  const uint64_t h = ((v >> (8 * offset)) << 24) * kHashMul32;
  return static_cast<uint32_t>(h >> shift);
}

static inline int IsMatch(const uint8_t* p1, const uint8_t* p2) {
  return (BROTLI_UNALIGNED_LOAD32(p1) == BROTLI_UNALIGNED_LOAD32(p2) &&
          p1[4] == p2[4]);
}

// Builds a literal prefix code into "depths" and "bits" based on the statistics
// of the "input" string and stores it into the bit stream.
// Note that the prefix code here is built from the pre-LZ77 input, therefore
// we can only approximate the statistics of the actual literal stream.
// Moreover, for long inputs we build a histogram from a sample of the input
// and thus have to assign a non-zero depth for each literal.
void BuildAndStoreLiteralPrefixCode(const uint8_t* input,
                                    const size_t input_size,
                                    uint8_t depths[256],
                                    uint16_t bits[256],
                                    size_t* storage_ix,
                                    uint8_t* storage) {
  uint32_t histogram[256] = { 0 };
  size_t histogram_total;
  if (input_size < (1 << 15)) {
    for (size_t i = 0; i < input_size; ++i)  {
      ++histogram[input[i]];
    }
    histogram_total = input_size;
    for (size_t i = 0; i < 256; ++i) {
      // We weigh the first 11 samples with weight 3 to account for the
      // balancing effect of the LZ77 phase on the histogram.
      const uint32_t adjust = 2 * std::min(histogram[i], 11u);
      histogram[i] += adjust;
      histogram_total += adjust;
    }
  } else {
    static const size_t kSampleRate = 29;
    for (size_t i = 0; i < input_size; i += kSampleRate) {
      ++histogram[input[i]];
    }
    histogram_total = (input_size + kSampleRate - 1) / kSampleRate;
    for (size_t i = 0; i < 256; ++i) {
      // We add 1 to each population count to avoid 0 bit depths (since this is
      // only a sample and we don't know if the symbol appears or not), and we
      // weigh the first 11 samples with weight 3 to account for the balancing
      // effect of the LZ77 phase on the histogram (more frequent symbols are
      // more likely to be in backward references instead as literals).
      const uint32_t adjust = 1 + 2 * std::min(histogram[i], 11u);
      histogram[i] += adjust;
      histogram_total += adjust;
    }
  }
  BuildAndStoreHuffmanTreeFast(histogram, histogram_total,
                               /* max_bits = */ 8,
                               depths, bits, storage_ix, storage);
}

// Builds a command and distance prefix code (each 64 symbols) into "depth" and
// "bits" based on "histogram" and stores it into the bit stream.
void BuildAndStoreCommandPrefixCode(const uint32_t histogram[128],
                                    uint8_t depth[128], uint16_t bits[128],
                                    size_t* storage_ix, uint8_t* storage) {
  CreateHuffmanTree(histogram, 64, 15, depth);
  CreateHuffmanTree(&histogram[64], 64, 14, &depth[64]);
  // We have to jump through a few hoopes here in order to compute
  // the command bits because the symbols are in a different order than in
  // the full alphabet. This looks complicated, but having the symbols
  // in this order in the command bits saves a few branches in the Emit*
  // functions.
  uint8_t cmd_depth[64];
  uint16_t cmd_bits[64];
  memcpy(cmd_depth, depth, 24);
  memcpy(cmd_depth + 24, depth + 40, 8);
  memcpy(cmd_depth + 32, depth + 24, 8);
  memcpy(cmd_depth + 40, depth + 48, 8);
  memcpy(cmd_depth + 48, depth + 32, 8);
  memcpy(cmd_depth + 56, depth + 56, 8);
  ConvertBitDepthsToSymbols(cmd_depth, 64, cmd_bits);
  memcpy(bits, cmd_bits, 48);
  memcpy(bits + 24, cmd_bits + 32, 16);
  memcpy(bits + 32, cmd_bits + 48, 16);
  memcpy(bits + 40, cmd_bits + 24, 16);
  memcpy(bits + 48, cmd_bits + 40, 16);
  memcpy(bits + 56, cmd_bits + 56, 16);
  ConvertBitDepthsToSymbols(&depth[64], 64, &bits[64]);
  {
    // Create the bit length array for the full command alphabet.
    uint8_t cmd_depth[704] = { 0 };
    memcpy(cmd_depth, depth, 8);
    memcpy(cmd_depth + 64, depth + 8, 8);
    memcpy(cmd_depth + 128, depth + 16, 8);
    memcpy(cmd_depth + 192, depth + 24, 8);
    memcpy(cmd_depth + 384, depth + 32, 8);
    for (size_t i = 0; i < 8; ++i) {
      cmd_depth[128 + 8 * i] = depth[40 + i];
      cmd_depth[256 + 8 * i] = depth[48 + i];
      cmd_depth[448 + 8 * i] = depth[56 + i];
    }
    StoreHuffmanTree(cmd_depth, 704, storage_ix, storage);
  }
  StoreHuffmanTree(&depth[64], 64, storage_ix, storage);
}

// REQUIRES: insertlen < 6210
inline void EmitInsertLen(size_t insertlen,
                          const uint8_t depth[128],
                          const uint16_t bits[128],
                          uint32_t histo[128],
                          size_t* storage_ix,
                          uint8_t* storage) {
  if (insertlen < 6) {
    const size_t code = insertlen + 40;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    ++histo[code];
  } else if (insertlen < 130) {
    insertlen -= 2;
    const uint32_t nbits = Log2FloorNonZero(insertlen) - 1u;
    const size_t prefix = insertlen >> nbits;
    const size_t inscode = (nbits << 1) + prefix + 42;
    WriteBits(depth[inscode], bits[inscode], storage_ix, storage);
    WriteBits(nbits, insertlen - (prefix << nbits), storage_ix, storage);
    ++histo[inscode];
  } else if (insertlen < 2114) {
    insertlen -= 66;
    const uint32_t nbits = Log2FloorNonZero(insertlen);
    const size_t code = nbits + 50;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(nbits, insertlen - (1 << nbits), storage_ix, storage);
    ++histo[code];
  } else {
    WriteBits(depth[61], bits[61], storage_ix, storage);
    WriteBits(12, insertlen - 2114, storage_ix, storage);
    ++histo[21];
  }
}

inline void EmitLongInsertLen(size_t insertlen,
                              const uint8_t depth[128],
                              const uint16_t bits[128],
                              uint32_t histo[128],
                              size_t* storage_ix,
                              uint8_t* storage) {
  if (insertlen < 22594) {
    WriteBits(depth[62], bits[62], storage_ix, storage);
    WriteBits(14, insertlen - 6210, storage_ix, storage);
    ++histo[22];
  } else {
    WriteBits(depth[63], bits[63], storage_ix, storage);
    WriteBits(24, insertlen - 22594, storage_ix, storage);
    ++histo[23];
  }
}

inline void EmitCopyLen(size_t copylen,
                        const uint8_t depth[128],
                        const uint16_t bits[128],
                        uint32_t histo[128],
                        size_t* storage_ix,
                        uint8_t* storage) {
  if (copylen < 10) {
    WriteBits(depth[copylen + 14], bits[copylen + 14], storage_ix, storage);
    ++histo[copylen + 14];
  } else if (copylen < 134) {
    copylen -= 6;
    const uint32_t nbits = Log2FloorNonZero(copylen) - 1u;
    const size_t prefix = copylen >> nbits;
    const size_t code = (nbits << 1) + prefix + 20;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(nbits, copylen - (prefix << nbits), storage_ix, storage);
    ++histo[code];
  } else if (copylen < 2118) {
    copylen -= 70;
    const uint32_t nbits = Log2FloorNonZero(copylen);
    const size_t code = nbits + 28;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(nbits, copylen - (1 << nbits), storage_ix, storage);
    ++histo[code];
  } else {
    WriteBits(depth[39], bits[39], storage_ix, storage);
    WriteBits(24, copylen - 2118, storage_ix, storage);
    ++histo[47];
  }
}

inline void EmitCopyLenLastDistance(size_t copylen,
                                    const uint8_t depth[128],
                                    const uint16_t bits[128],
                                    uint32_t histo[128],
                                    size_t* storage_ix,
                                    uint8_t* storage) {
  if (copylen < 12) {
    WriteBits(depth[copylen - 4], bits[copylen - 4], storage_ix, storage);
    ++histo[copylen - 4];
  } else if (copylen < 72) {
    copylen -= 8;
    const uint32_t nbits = Log2FloorNonZero(copylen) - 1;
    const size_t prefix = copylen >> nbits;
    const size_t code = (nbits << 1) + prefix + 4;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(nbits, copylen - (prefix << nbits), storage_ix, storage);
    ++histo[code];
  } else if (copylen < 136) {
    copylen -= 8;
    const size_t code = (copylen >> 5) + 30;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(5, copylen & 31, storage_ix, storage);
    WriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[code];
    ++histo[64];
  } else if (copylen < 2120) {
    copylen -= 72;
    const uint32_t nbits = Log2FloorNonZero(copylen);
    const size_t code = nbits + 28;
    WriteBits(depth[code], bits[code], storage_ix, storage);
    WriteBits(nbits, copylen - (1 << nbits), storage_ix, storage);
    WriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[code];
    ++histo[64];
  } else {
    WriteBits(depth[39], bits[39], storage_ix, storage);
    WriteBits(24, copylen - 2120, storage_ix, storage);
    WriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[47];
    ++histo[64];
  }
}

inline void EmitDistance(size_t distance,
                         const uint8_t depth[128],
                         const uint16_t bits[128],
                         uint32_t histo[128],
                         size_t* storage_ix, uint8_t* storage) {
  distance += 3;
  const uint32_t nbits = Log2FloorNonZero(distance) - 1u;
  const size_t prefix = (distance >> nbits) & 1;
  const size_t offset = (2 + prefix) << nbits;
  const size_t distcode = 2 * (nbits - 1) + prefix + 80;
  WriteBits(depth[distcode], bits[distcode], storage_ix, storage);
  WriteBits(nbits, distance - offset, storage_ix, storage);
  ++histo[distcode];
}

inline void EmitLiterals(const uint8_t* input, const size_t len,
                         const uint8_t depth[256], const uint16_t bits[256],
                         size_t* storage_ix, uint8_t* storage) {
  for (size_t j = 0; j < len; j++) {
    const uint8_t lit = input[j];
    WriteBits(depth[lit], bits[lit], storage_ix, storage);
  }
}

// REQUIRES: len <= 1 << 20.
static void StoreMetaBlockHeader(
    size_t len, bool is_uncompressed, size_t* storage_ix, uint8_t* storage) {
  // ISLAST
  WriteBits(1, 0, storage_ix, storage);
  if (len <= (1U << 16)) {
    // MNIBBLES is 4
    WriteBits(2, 0, storage_ix, storage);
    WriteBits(16, len - 1, storage_ix, storage);
  } else {
    // MNIBBLES is 5
    WriteBits(2, 1, storage_ix, storage);
    WriteBits(20, len - 1, storage_ix, storage);
  }
  // ISUNCOMPRESSED
  WriteBits(1, is_uncompressed, storage_ix, storage);
}

void UpdateBits(size_t n_bits,
                uint32_t bits,
                size_t pos,
                uint8_t *array) {
  while (n_bits > 0) {
    size_t byte_pos = pos >> 3;
    size_t n_unchanged_bits = pos & 7;
    size_t n_changed_bits = std::min(n_bits, 8 - n_unchanged_bits);
    size_t total_bits = n_unchanged_bits + n_changed_bits;
    uint32_t mask = (~((1 << total_bits) - 1)) | ((1 << n_unchanged_bits) - 1);
    uint32_t unchanged_bits = array[byte_pos] & mask;
    uint32_t changed_bits = bits & ((1 << n_changed_bits) - 1);
    array[byte_pos] =
        static_cast<uint8_t>((changed_bits << n_unchanged_bits) |
                             unchanged_bits);
    n_bits -= n_changed_bits;
    bits >>= n_changed_bits;
    pos += n_changed_bits;
  }
}

void RewindBitPosition(const size_t new_storage_ix,
                       size_t* storage_ix, uint8_t* storage) {
  const size_t bitpos = new_storage_ix & 7;
  const size_t mask = (1u << bitpos) - 1;
  storage[new_storage_ix >> 3] &= static_cast<uint8_t>(mask);
  *storage_ix = new_storage_ix;
}

bool ShouldMergeBlock(const uint8_t* data, size_t len, const uint8_t* depths) {
  size_t histo[256] = { 0 };
  static const size_t kSampleRate = 43;
  for (size_t i = 0; i < len; i += kSampleRate) {
    ++histo[data[i]];
  }
  const size_t total = (len + kSampleRate - 1) / kSampleRate;
  double r = (FastLog2(total) + 0.5) * static_cast<double>(total) + 200;
  for (size_t i = 0; i < 256; ++i) {
    r -= static_cast<double>(histo[i]) * (depths[i] + FastLog2(histo[i]));
  }
  return r >= 0.0;
}

inline bool ShouldUseUncompressedMode(const uint8_t* metablock_start,
                                      const uint8_t* next_emit,
                                      const size_t insertlen,
                                      const uint8_t literal_depths[256]) {
  const size_t compressed = static_cast<size_t>(next_emit - metablock_start);
  if (compressed * 50 > insertlen) {
    return false;
  }
  static const double kAcceptableLossForUncompressibleSpeedup = 0.02;
  static const double kMinEntropy =
      8 * (1.0 - kAcceptableLossForUncompressibleSpeedup);
  uint32_t sum = 0;
  for (int i = 0; i < 256; ++i) {
    const uint32_t n = literal_depths[i];
    sum += n << (15 - n);
  }
  return sum > static_cast<uint32_t>((1 << 15) * kMinEntropy);
}

void EmitUncompressedMetaBlock(const uint8_t* begin, const uint8_t* end,
                               const size_t storage_ix_start,
                               size_t* storage_ix, uint8_t* storage) {
  const size_t len = static_cast<size_t>(end - begin);
  RewindBitPosition(storage_ix_start, storage_ix, storage);
  StoreMetaBlockHeader(len, 1, storage_ix, storage);
  *storage_ix = (*storage_ix + 7u) & ~7u;
  memcpy(&storage[*storage_ix >> 3], begin, len);
  *storage_ix += len << 3;
  storage[*storage_ix >> 3] = 0;
}

void BrotliCompressFragmentFast(const uint8_t* input, size_t input_size,
                                bool is_last,
                                int* table, size_t table_size,
                                uint8_t cmd_depth[128], uint16_t cmd_bits[128],
                                size_t* cmd_code_numbits, uint8_t* cmd_code,
                                size_t* storage_ix, uint8_t* storage) {
  if (input_size == 0) {
    assert(is_last);
    WriteBits(1, 1, storage_ix, storage);  // islast
    WriteBits(1, 1, storage_ix, storage);  // isempty
    *storage_ix = (*storage_ix + 7u) & ~7u;
    return;
  }

  // "next_emit" is a pointer to the first byte that is not covered by a
  // previous copy. Bytes between "next_emit" and the start of the next copy or
  // the end of the input will be emitted as literal bytes.
  const uint8_t* next_emit = input;
  // Save the start of the first block for position and distance computations.
  const uint8_t* base_ip = input;

  static const size_t kFirstBlockSize = 3 << 15;
  static const size_t kMergeBlockSize = 1 << 16;

  const uint8_t* metablock_start = input;
  size_t block_size = std::min(input_size, kFirstBlockSize);
  size_t total_block_size = block_size;
  // Save the bit position of the MLEN field of the meta-block header, so that
  // we can update it later if we decide to extend this meta-block.
  size_t mlen_storage_ix = *storage_ix + 3;
  StoreMetaBlockHeader(block_size, 0, storage_ix, storage);
  // No block splits, no contexts.
  WriteBits(13, 0, storage_ix, storage);

  uint8_t lit_depth[256] = { 0 };
  uint16_t lit_bits[256] = { 0 };
  BuildAndStoreLiteralPrefixCode(input, block_size, lit_depth, lit_bits,
                                 storage_ix, storage);

  // Store the pre-compressed command and distance prefix codes.
  for (size_t i = 0; i + 7 < *cmd_code_numbits; i += 8) {
    WriteBits(8, cmd_code[i >> 3], storage_ix, storage);
  }
  WriteBits(*cmd_code_numbits & 7, cmd_code[*cmd_code_numbits >> 3],
            storage_ix, storage);

 emit_commands:
  // Initialize the command and distance histograms. We will gather
  // statistics of command and distance codes during the processing
  // of this block and use it to update the command and distance
  // prefix codes for the next block.
  uint32_t cmd_histo[128] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0,
  };

  // "ip" is the input pointer.
  const uint8_t* ip = input;
  assert(table_size);
  assert(table_size <= (1u << 31));
  assert((table_size & (table_size - 1)) == 0);  // table must be power of two
  const size_t shift = 64u - Log2FloorNonZero(table_size);
  assert(static_cast<size_t>(0xffffffffffffffffU >> shift) == table_size - 1);
  const uint8_t* ip_end = input + block_size;

  int last_distance = -1;
  const size_t kInputMarginBytes = 16;
  const size_t kMinMatchLen = 5;
  if (PREDICT_TRUE(block_size >= kInputMarginBytes)) {
    // For the last block, we need to keep a 16 bytes margin so that we can be
    // sure that all distances are at most window size - 16.
    // For all other blocks, we only need to keep a margin of 5 bytes so that
    // we don't go over the block size with a copy.
    const size_t len_limit = std::min(block_size - kMinMatchLen,
                                      input_size - kInputMarginBytes);
    const uint8_t* ip_limit = input + len_limit;

    for (uint32_t next_hash = Hash(++ip, shift); ; ) {
      assert(next_emit < ip);
      // Step 1: Scan forward in the input looking for a 5-byte-long match.
      // If we get close to exhausting the input then goto emit_remainder.
      //
      // Heuristic match skipping: If 32 bytes are scanned with no matches
      // found, start looking only at every other byte. If 32 more bytes are
      // scanned, look at every third byte, etc.. When a match is found,
      // immediately go back to looking at every byte. This is a small loss
      // (~5% performance, ~0.1% density) for compressible data due to more
      // bookkeeping, but for non-compressible data (such as JPEG) it's a huge
      // win since the compressor quickly "realizes" the data is incompressible
      // and doesn't bother looking for matches everywhere.
      //
      // The "skip" variable keeps track of how many bytes there are since the
      // last match; dividing it by 32 (ie. right-shifting by five) gives the
      // number of bytes to move ahead for each iteration.
      uint32_t skip = 32;

      const uint8_t* next_ip = ip;
      const uint8_t* candidate;
      do {
        ip = next_ip;
        uint32_t hash = next_hash;
        assert(hash == Hash(ip, shift));
        uint32_t bytes_between_hash_lookups = skip++ >> 5;
        next_ip = ip + bytes_between_hash_lookups;
        if (PREDICT_FALSE(next_ip > ip_limit)) {
          goto emit_remainder;
        }
        next_hash = Hash(next_ip, shift);
        candidate = ip - last_distance;
        if (IsMatch(ip, candidate)) {
          if (PREDICT_TRUE(candidate < ip)) {
            table[hash] = static_cast<int>(ip - base_ip);
            break;
          }
        }
        candidate = base_ip + table[hash];
        assert(candidate >= base_ip);
        assert(candidate < ip);

        table[hash] = static_cast<int>(ip - base_ip);
      } while (PREDICT_TRUE(!IsMatch(ip, candidate)));

      // Step 2: Emit the found match together with the literal bytes from
      // "next_emit" to the bit stream, and then see if we can find a next macth
      // immediately afterwards. Repeat until we find no match for the input
      // without emitting some literal bytes.
      uint64_t input_bytes;

      {
        // We have a 5-byte match at ip, and we need to emit bytes in
        // [next_emit, ip).
        const uint8_t* base = ip;
        size_t matched = 5 + FindMatchLengthWithLimit(
            candidate + 5, ip + 5, static_cast<size_t>(ip_end - ip) - 5);
        ip += matched;
        int distance = static_cast<int>(base - candidate);  /* > 0 */
        size_t insert = static_cast<size_t>(base - next_emit);
        assert(0 == memcmp(base, candidate, matched));
        if (PREDICT_TRUE(insert < 6210)) {
          EmitInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                        storage_ix, storage);
        } else if (ShouldUseUncompressedMode(metablock_start, next_emit, insert,
                                             lit_depth)) {
          EmitUncompressedMetaBlock(metablock_start, base, mlen_storage_ix - 3,
                                    storage_ix, storage);
          input_size -= static_cast<size_t>(base - input);
          input = base;
          next_emit = input;
          goto next_block;
        } else {
          EmitLongInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                            storage_ix, storage);
        }
        EmitLiterals(next_emit, insert, lit_depth, lit_bits,
                     storage_ix, storage);
        if (distance == last_distance) {
          WriteBits(cmd_depth[64], cmd_bits[64], storage_ix, storage);
          ++cmd_histo[64];
        } else {
          EmitDistance(static_cast<size_t>(distance), cmd_depth, cmd_bits,
                       cmd_histo, storage_ix, storage);
          last_distance = distance;
        }
        EmitCopyLenLastDistance(matched, cmd_depth, cmd_bits, cmd_histo,
                                storage_ix, storage);

        next_emit = ip;
        if (PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        // We could immediately start working at ip now, but to improve
        // compression we first update "table" with the hashes of some positions
        // within the last copy.
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 3);
        uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 3);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 1);

        uint32_t cur_hash = HashBytesAtOffset(input_bytes, 3, shift);
        candidate = base_ip + table[cur_hash];
        table[cur_hash] = static_cast<int>(ip - base_ip);
      }

      while (IsMatch(ip, candidate)) {
        // We have a 5-byte match at ip, and no need to emit any literal bytes
        // prior to ip.
        const uint8_t* base = ip;
        size_t matched = 5 + FindMatchLengthWithLimit(
            candidate + 5, ip + 5, static_cast<size_t>(ip_end - ip) - 5);
        ip += matched;
        last_distance = static_cast<int>(base - candidate);  /* > 0 */
        assert(0 == memcmp(base, candidate, matched));
        EmitCopyLen(matched, cmd_depth, cmd_bits, cmd_histo,
                    storage_ix, storage);
        EmitDistance(static_cast<size_t>(last_distance), cmd_depth, cmd_bits,
                     cmd_histo, storage_ix, storage);

        next_emit = ip;
        if (PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        // We could immediately start working at ip now, but to improve
        // compression we first update "table" with the hashes of some positions
        // within the last copy.
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 3);
        uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 3);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 1);

        uint32_t cur_hash = HashBytesAtOffset(input_bytes, 3, shift);
        candidate = base_ip + table[cur_hash];
        table[cur_hash] = static_cast<int>(ip - base_ip);
      }

      next_hash = Hash(++ip, shift);
    }
  }

 emit_remainder:
  assert(next_emit <= ip_end);
  input += block_size;
  input_size -= block_size;
  block_size = std::min(input_size, kMergeBlockSize);

  // Decide if we want to continue this meta-block instead of emitting the
  // last insert-only command.
  if (input_size > 0 &&
      total_block_size + block_size <= (1 << 20) &&
      ShouldMergeBlock(input, block_size, lit_depth)) {
    assert(total_block_size > (1 << 16));
    // Update the size of the current meta-block and continue emitting commands.
    // We can do this because the current size and the new size both have 5
    // nibbles.
    total_block_size += block_size;
    UpdateBits(20, static_cast<uint32_t>(total_block_size - 1),
               mlen_storage_ix, storage);
    goto emit_commands;
  }

  // Emit the remaining bytes as literals.
  if (next_emit < ip_end) {
    const size_t insert = static_cast<size_t>(ip_end - next_emit);
    if (PREDICT_TRUE(insert < 6210)) {
      EmitInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                    storage_ix, storage);
      EmitLiterals(next_emit, insert, lit_depth, lit_bits, storage_ix, storage);
    } else if (ShouldUseUncompressedMode(metablock_start, next_emit, insert,
                                         lit_depth)) {
      EmitUncompressedMetaBlock(metablock_start, ip_end, mlen_storage_ix - 3,
                                storage_ix, storage);
    } else {
      EmitLongInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                        storage_ix, storage);
      EmitLiterals(next_emit, insert, lit_depth, lit_bits,
                   storage_ix, storage);
    }
  }
  next_emit = ip_end;

next_block:
  // If we have more data, write a new meta-block header and prefix codes and
  // then continue emitting commands.
  if (input_size > 0) {
    metablock_start = input;
    block_size = std::min(input_size, kFirstBlockSize);
    total_block_size = block_size;
    // Save the bit position of the MLEN field of the meta-block header, so that
    // we can update it later if we decide to extend this meta-block.
    mlen_storage_ix = *storage_ix + 3;
    StoreMetaBlockHeader(block_size, 0, storage_ix, storage);
    // No block splits, no contexts.
    WriteBits(13, 0, storage_ix, storage);
    memset(lit_depth, 0, sizeof(lit_depth));
    memset(lit_bits, 0, sizeof(lit_bits));
    BuildAndStoreLiteralPrefixCode(input, block_size, lit_depth, lit_bits,
                                   storage_ix, storage);
    BuildAndStoreCommandPrefixCode(cmd_histo, cmd_depth, cmd_bits,
                                   storage_ix, storage);
    goto emit_commands;
  }

  if (is_last) {
    WriteBits(1, 1, storage_ix, storage);  // islast
    WriteBits(1, 1, storage_ix, storage);  // isempty
    *storage_ix = (*storage_ix + 7u) & ~7u;
  } else {
    // If this is not the last block, update the command and distance prefix
    // codes for the next block and store the compressed forms.
    cmd_code[0] = 0;
    *cmd_code_numbits = 0;
    BuildAndStoreCommandPrefixCode(cmd_histo, cmd_depth, cmd_bits,
                                   cmd_code_numbits, cmd_code);
  }
}

}  // namespace brotli
