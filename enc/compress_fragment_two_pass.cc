/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Function for fast encoding of an input fragment, independently from the input
// history. This function uses two-pass processing: in the first pass we save
// the found backward matches and literal bytes into a buffer, and in the
// second pass we emit them into the bit stream using prefix codes built based
// on the actual command and literal byte histograms.

#include "./compress_fragment_two_pass.h"

#include <algorithm>

#include "./brotli_bit_stream.h"
#include "./bit_cost.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./find_match_length.h"
#include "./port.h"
#include "./types.h"
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
  const uint64_t h = (BROTLI_UNALIGNED_LOAD64(p) << 16) * kHashMul32;
  return static_cast<uint32_t>(h >> shift);
}

static inline uint32_t HashBytesAtOffset(uint64_t v, int offset, size_t shift) {
  assert(offset >= 0);
  assert(offset <= 2);
  const uint64_t h = ((v >> (8 * offset)) << 16) * kHashMul32;
  return static_cast<uint32_t>(h >> shift);
}

static inline int IsMatch(const uint8_t* p1, const uint8_t* p2) {
  return (BROTLI_UNALIGNED_LOAD32(p1) == BROTLI_UNALIGNED_LOAD32(p2) &&
          p1[4] == p2[4] &&
          p1[5] == p2[5]);
}

// Builds a command and distance prefix code (each 64 symbols) into "depth" and
// "bits" based on "histogram" and stores it into the bit stream.
static void BuildAndStoreCommandPrefixCode(
    const uint32_t histogram[128],
    uint8_t depth[128], uint16_t bits[128],
    size_t* storage_ix, uint8_t* storage) {
  // Tree size for building a tree over 64 symbols is 2 * 64 + 1.
  static const size_t kTreeSize = 129;
  HuffmanTree tree[kTreeSize];
  CreateHuffmanTree(histogram, 64, 15, tree, depth);
  CreateHuffmanTree(&histogram[64], 64, 14, tree, &depth[64]);
  // We have to jump through a few hoopes here in order to compute
  // the command bits because the symbols are in a different order than in
  // the full alphabet. This looks complicated, but having the symbols
  // in this order in the command bits saves a few branches in the Emit*
  // functions.
  uint8_t cmd_depth[64];
  uint16_t cmd_bits[64];
  memcpy(cmd_depth, depth + 24, 24);
  memcpy(cmd_depth + 24, depth, 8);
  memcpy(cmd_depth + 32, depth + 48, 8);
  memcpy(cmd_depth + 40, depth + 8, 8);
  memcpy(cmd_depth + 48, depth + 56, 8);
  memcpy(cmd_depth + 56, depth + 16, 8);
  ConvertBitDepthsToSymbols(cmd_depth, 64, cmd_bits);
  memcpy(bits, cmd_bits + 24, 16);
  memcpy(bits + 8, cmd_bits + 40, 16);
  memcpy(bits + 16, cmd_bits + 56, 16);
  memcpy(bits + 24, cmd_bits, 48);
  memcpy(bits + 48, cmd_bits + 32, 16);
  memcpy(bits + 56, cmd_bits + 48, 16);
  ConvertBitDepthsToSymbols(&depth[64], 64, &bits[64]);
  {
    // Create the bit length array for the full command alphabet.
    uint8_t cmd_depth[704] = { 0 };
    memcpy(cmd_depth, depth + 24, 8);
    memcpy(cmd_depth + 64, depth + 32, 8);
    memcpy(cmd_depth + 128, depth + 40, 8);
    memcpy(cmd_depth + 192, depth + 48, 8);
    memcpy(cmd_depth + 384, depth + 56, 8);
    for (size_t i = 0; i < 8; ++i) {
      cmd_depth[128 + 8 * i] = depth[i];
      cmd_depth[256 + 8 * i] = depth[8 + i];
      cmd_depth[448 + 8 * i] = depth[16 + i];
    }
    StoreHuffmanTree(cmd_depth, 704, tree, storage_ix, storage);
  }
  StoreHuffmanTree(&depth[64], 64, tree, storage_ix, storage);
}

inline void EmitInsertLen(uint32_t insertlen, uint32_t** commands) {
  if (insertlen < 6) {
    **commands = insertlen;
  } else if (insertlen < 130) {
    insertlen -= 2;
    const uint32_t nbits = Log2FloorNonZero(insertlen) - 1u;
    const uint32_t prefix = insertlen >> nbits;
    const uint32_t inscode = (nbits << 1) + prefix + 2;
    const uint32_t extra = insertlen - (prefix << nbits);
    **commands = inscode | (extra << 8);
  } else if (insertlen < 2114) {
    insertlen -= 66;
    const uint32_t nbits = Log2FloorNonZero(insertlen);
    const uint32_t code = nbits + 10;
    const uint32_t extra = insertlen - (1 << nbits);
    **commands = code | (extra << 8);
  } else if (insertlen < 6210) {
    const uint32_t extra = insertlen - 2114;
    **commands = 21 | (extra << 8);
  } else if (insertlen < 22594) {
    const uint32_t extra = insertlen - 6210;
    **commands = 22 | (extra << 8);
  } else {
    const uint32_t extra = insertlen - 22594;
    **commands = 23 | (extra << 8);
  }
  ++(*commands);
}

inline void EmitCopyLen(size_t copylen, uint32_t** commands) {
  if (copylen < 10) {
    **commands = static_cast<uint32_t>(copylen + 38);
  } else if (copylen < 134) {
    copylen -= 6;
    const size_t nbits = Log2FloorNonZero(copylen) - 1;
    const size_t prefix = copylen >> nbits;
    const size_t code = (nbits << 1) + prefix + 44;
    const size_t extra = copylen - (prefix << nbits);
    **commands = static_cast<uint32_t>(code | (extra << 8));
  } else if (copylen < 2118) {
    copylen -= 70;
    const size_t nbits = Log2FloorNonZero(copylen);
    const size_t code = nbits + 52;
    const size_t extra = copylen - (1 << nbits);
    **commands = static_cast<uint32_t>(code | (extra << 8));
  } else {
    const size_t extra = copylen - 2118;
    **commands = static_cast<uint32_t>(63 | (extra << 8));
  }
  ++(*commands);
}

inline void EmitCopyLenLastDistance(size_t copylen, uint32_t** commands) {
  if (copylen < 12) {
    **commands = static_cast<uint32_t>(copylen + 20);
    ++(*commands);
  } else if (copylen < 72) {
    copylen -= 8;
    const size_t nbits = Log2FloorNonZero(copylen) - 1;
    const size_t prefix = copylen >> nbits;
    const size_t code = (nbits << 1) + prefix + 28;
    const size_t extra = copylen - (prefix << nbits);
    **commands = static_cast<uint32_t>(code | (extra << 8));
    ++(*commands);
  } else if (copylen < 136) {
    copylen -= 8;
    const size_t code = (copylen >> 5) + 54;
    const size_t extra = copylen & 31;
    **commands = static_cast<uint32_t>(code | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  } else if (copylen < 2120) {
    copylen -= 72;
    const size_t nbits = Log2FloorNonZero(copylen);
    const size_t code = nbits + 52;
    const size_t extra = copylen - (1 << nbits);
    **commands = static_cast<uint32_t>(code | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  } else {
    const size_t extra = copylen - 2120;
    **commands = static_cast<uint32_t>(63 | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  }
}

inline void EmitDistance(uint32_t distance, uint32_t** commands) {
  distance += 3;
  uint32_t nbits = Log2FloorNonZero(distance) - 1;
  const uint32_t prefix = (distance >> nbits) & 1;
  const uint32_t offset = (2 + prefix) << nbits;
  const uint32_t distcode = 2 * (nbits - 1) + prefix + 80;
  uint32_t extra = distance - offset;
  **commands = distcode | (extra << 8);
  ++(*commands);
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

static void CreateCommands(const uint8_t* input, size_t block_size,
                           size_t input_size, const uint8_t* base_ip,
                           int* table, size_t table_size,
                           uint8_t** literals, uint32_t** commands) {
  // "ip" is the input pointer.
  const uint8_t* ip = input;
  assert(table_size);
  assert(table_size <= (1u << 31));
  assert((table_size & (table_size - 1)) == 0);  // table must be power of two
  const size_t shift = 64u - Log2FloorNonZero(table_size);
  assert(table_size - 1 == static_cast<size_t>(
      MAKE_UINT64_T(0xFFFFFFFF, 0xFFFFFF) >> shift));
  const uint8_t* ip_end = input + block_size;
  // "next_emit" is a pointer to the first byte that is not covered by a
  // previous copy. Bytes between "next_emit" and the start of the next copy or
  // the end of the input will be emitted as literal bytes.
  const uint8_t* next_emit = input;

  int last_distance = -1;
  const size_t kInputMarginBytes = 16;
  const size_t kMinMatchLen = 6;
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
      // Step 1: Scan forward in the input looking for a 6-byte-long match.
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
      // "next_emit", and then see if we can find a next macth immediately
      // afterwards. Repeat until we find no match for the input
      // without emitting some literal bytes.
      uint64_t input_bytes;

      {
        // We have a 6-byte match at ip, and we need to emit bytes in
        // [next_emit, ip).
        const uint8_t* base = ip;
        size_t matched = 6 + FindMatchLengthWithLimit(
            candidate + 6, ip + 6, static_cast<size_t>(ip_end - ip) - 6);
        ip += matched;
        int distance = static_cast<int>(base - candidate);  /* > 0 */
        int insert = static_cast<int>(base - next_emit);
        assert(0 == memcmp(base, candidate, matched));
        EmitInsertLen(static_cast<uint32_t>(insert), commands);
        memcpy(*literals, next_emit, static_cast<size_t>(insert));
        *literals += insert;
        if (distance == last_distance) {
          **commands = 64;
          ++(*commands);
        } else {
          EmitDistance(static_cast<uint32_t>(distance), commands);
          last_distance = distance;
        }
        EmitCopyLenLastDistance(matched, commands);

        next_emit = ip;
        if (PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        // We could immediately start working at ip now, but to improve
        // compression we first update "table" with the hashes of some positions
        // within the last copy.
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 5);
        uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 5);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 4);
        prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 3);
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 1);

        uint32_t cur_hash = HashBytesAtOffset(input_bytes, 2, shift);
        candidate = base_ip + table[cur_hash];
        table[cur_hash] = static_cast<int>(ip - base_ip);
      }

      while (IsMatch(ip, candidate)) {
        // We have a 6-byte match at ip, and no need to emit any
        // literal bytes prior to ip.
        const uint8_t* base = ip;
        size_t matched = 6 + FindMatchLengthWithLimit(
            candidate + 6, ip + 6, static_cast<size_t>(ip_end - ip) - 6);
        ip += matched;
        last_distance = static_cast<int>(base - candidate);  /* > 0 */
        assert(0 == memcmp(base, candidate, matched));
        EmitCopyLen(matched, commands);
        EmitDistance(static_cast<uint32_t>(last_distance), commands);

        next_emit = ip;
        if (PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        // We could immediately start working at ip now, but to improve
        // compression we first update "table" with the hashes of some positions
        // within the last copy.
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 5);
        uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 5);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 4);
        prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 3);
        input_bytes = BROTLI_UNALIGNED_LOAD64(ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 2);
        prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
        table[prev_hash] = static_cast<int>(ip - base_ip - 1);

        uint32_t cur_hash = HashBytesAtOffset(input_bytes, 2, shift);
        candidate = base_ip + table[cur_hash];
        table[cur_hash] = static_cast<int>(ip - base_ip);
      }

      next_hash = Hash(++ip, shift);
    }
  }

emit_remainder:
  assert(next_emit <= ip_end);
  // Emit the remaining bytes as literals.
  if (next_emit < ip_end) {
    const uint32_t insert = static_cast<uint32_t>(ip_end - next_emit);
    EmitInsertLen(insert, commands);
    memcpy(*literals, next_emit, insert);
    *literals += insert;
  }
}

static void StoreCommands(const uint8_t* literals, const size_t num_literals,
                          const uint32_t* commands, const size_t num_commands,
                          size_t* storage_ix, uint8_t* storage) {
  uint8_t lit_depths[256] = { 0 };
  uint16_t lit_bits[256] = { 0 };
  uint32_t lit_histo[256] = { 0 };
  for (size_t i = 0; i < num_literals; ++i) {
    ++lit_histo[literals[i]];
  }
  BuildAndStoreHuffmanTreeFast(lit_histo, num_literals,
                               /* max_bits = */ 8,
                               lit_depths, lit_bits,
                               storage_ix, storage);

  uint8_t cmd_depths[128] = { 0 };
  uint16_t cmd_bits[128] = { 0 };
  uint32_t cmd_histo[128] = { 0 };
  for (size_t i = 0; i < num_commands; ++i) {
    ++cmd_histo[commands[i] & 0xff];
  }
  cmd_histo[1] += 1;
  cmd_histo[2] += 1;
  cmd_histo[64] += 1;
  cmd_histo[84] += 1;
  BuildAndStoreCommandPrefixCode(cmd_histo, cmd_depths, cmd_bits,
                                 storage_ix, storage);

  static const uint32_t kNumExtraBits[128] = {
    0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
    17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24,
  };
  static const uint32_t kInsertOffset[24] = {
    0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98, 130, 194, 322, 578,
    1090, 2114, 6210, 22594,
  };

  for (size_t i = 0; i < num_commands; ++i) {
    const uint32_t cmd = commands[i];
    const uint32_t code = cmd & 0xff;
    const uint32_t extra = cmd >> 8;
    WriteBits(cmd_depths[code], cmd_bits[code], storage_ix, storage);
    WriteBits(kNumExtraBits[code], extra, storage_ix, storage);
    if (code < 24) {
      const uint32_t insert = kInsertOffset[code] + extra;
      for (uint32_t j = 0; j < insert; ++j) {
        const uint8_t lit = *literals;
        WriteBits(lit_depths[lit], lit_bits[lit], storage_ix, storage);
        ++literals;
      }
    }
  }
}

static bool ShouldCompress(const uint8_t* input, size_t input_size,
                           size_t num_literals) {
  static const double kAcceptableLossForUncompressibleSpeedup = 0.02;
  static const double kMaxRatioOfLiterals =
      1.0 - kAcceptableLossForUncompressibleSpeedup;
  if (num_literals < kMaxRatioOfLiterals * static_cast<double>(input_size)) {
    return true;
  }
  uint32_t literal_histo[256] = { 0 };
  static const uint32_t kSampleRate = 43;
  static const double kMaxEntropy =
      8 * (1.0 - kAcceptableLossForUncompressibleSpeedup);
  const double max_total_bit_cost =
      static_cast<double>(input_size) * kMaxEntropy / kSampleRate;
  for (size_t i = 0; i < input_size; i += kSampleRate) {
    ++literal_histo[input[i]];
  }
  return BitsEntropy(literal_histo, 256) < max_total_bit_cost;
}

void BrotliCompressFragmentTwoPass(const uint8_t* input, size_t input_size,
                                   bool is_last,
                                   uint32_t* command_buf, uint8_t* literal_buf,
                                   int* table, size_t table_size,
                                   size_t* storage_ix, uint8_t* storage) {
  // Save the start of the first block for position and distance computations.
  const uint8_t* base_ip = input;

  while (input_size > 0) {
    size_t block_size = std::min(input_size, kCompressFragmentTwoPassBlockSize);
    uint32_t* commands = command_buf;
    uint8_t* literals = literal_buf;
    CreateCommands(input, block_size, input_size, base_ip, table, table_size,
                   &literals, &commands);
    const size_t num_literals = static_cast<size_t>(literals - literal_buf);
    const size_t num_commands = static_cast<size_t>(commands - command_buf);
    if (ShouldCompress(input, block_size, num_literals)) {
      StoreMetaBlockHeader(block_size, 0, storage_ix, storage);
      // No block splits, no contexts.
      WriteBits(13, 0, storage_ix, storage);
      StoreCommands(literal_buf, num_literals, command_buf, num_commands,
                    storage_ix, storage);
    } else {
      // Since we did not find many backward references and the entropy of
      // the data is close to 8 bits, we can simply emit an uncompressed block.
      // This makes compression speed of uncompressible data about 3x faster.
      StoreMetaBlockHeader(block_size, 1, storage_ix, storage);
      *storage_ix = (*storage_ix + 7u) & ~7u;
      memcpy(&storage[*storage_ix >> 3], input, block_size);
      *storage_ix += block_size << 3;
      storage[*storage_ix >> 3] = 0;
    }
    input += block_size;
    input_size -= block_size;
  }

  if (is_last) {
    WriteBits(1, 1, storage_ix, storage);  // islast
    WriteBits(1, 1, storage_ix, storage);  // isempty
    *storage_ix = (*storage_ix + 7u) & ~7u;
  }
}

}  // namespace brotli
