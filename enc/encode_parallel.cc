/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Implementation of parallel Brotli compressor. */

#include "./encode_parallel.h"

#include <vector>

#include "./backward_references.h"
#include "./brotli_bit_stream.h"
#include "./context.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./metablock.h"
#include "./port.h"
#include "./prefix.h"
#include "./quality.h"
#include "./utf8_util.h"

namespace brotli {

namespace {

static void RecomputeDistancePrefixes(Command* cmds, size_t num_commands,
                                      uint32_t num_direct_distance_codes,
                                      uint32_t distance_postfix_bits) {
  if (num_direct_distance_codes == 0 &&
      distance_postfix_bits == 0) {
    return;
  }
  for (size_t i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
    if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(CommandDistanceCode(cmd),
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

/* Returns 1 on success, otherwise 0. */
int WriteMetaBlockParallel(const BrotliEncoderParams* params,
                           const uint32_t input_size,
                           const uint8_t* input_buffer,
                           const uint32_t prefix_size,
                           const uint8_t* prefix_buffer,
                           const int is_first,
                           const int is_last,
                           size_t* encoded_size,
                           uint8_t* encoded_buffer) {
  if (input_size == 0) {
    return 0;
  }

  MemoryManager memory_manager;
  MemoryManager* m = &memory_manager;
  BrotliInitMemoryManager(m, 0, 0, 0);

  uint8_t* storage;
  size_t storage_ix;
  uint8_t first_byte;
  size_t first_byte_bits;
  size_t output_size;
  uint32_t num_direct_distance_codes;
  uint32_t distance_postfix_bits;
  ContextType literal_context_mode;
  size_t last_insert_len = 0;
  size_t num_commands = 0;
  size_t num_literals = 0;
  int dist_cache[4] = { -4, -4, -4, -4 };
  Command* commands;
  Hashers* hashers;
  int use_utf8_mode;
  uint8_t prev_byte;
  uint8_t prev_byte2;
  const uint32_t mask = BROTLI_UINT32_MAX >> 1;

  /* Copy prefix + next input block into a continuous area. */
  uint32_t input_pos = prefix_size;
  /* CreateBackwardReferences reads up to 3 bytes past the end of input if the
     mask points past the end of input.
     FindMatchLengthWithLimit could do another 8 bytes look-forward. */
  uint8_t* input = BROTLI_ALLOC(m, uint8_t, prefix_size + input_size + 4 + 8);
  if (BROTLI_IS_OOM(m)) goto oom;
  memcpy(input, prefix_buffer, prefix_size);
  memcpy(input + input_pos, input_buffer, input_size);
  /* Since we don't have a ringbuffer, masking is a no-op.
     We use one less bit than the full range because some of the code uses
     mask + 1 as the size of the ringbuffer. */

  prev_byte = input_pos > 0 ? input[(input_pos - 1) & mask] : 0;
  prev_byte2 = input_pos > 1 ? input[(input_pos - 2) & mask] : 0;

  /* Decide about UTF8 mode. */
  static const double kMinUTF8Ratio = 0.75;
  use_utf8_mode = BrotliIsMostlyUTF8(
      input, input_pos, mask, input_size, kMinUTF8Ratio);

  /* Initialize hashers. */
  hashers = BROTLI_ALLOC(m, Hashers, 1);
  if (BROTLI_IS_OOM(m)) goto oom;
  InitHashers(hashers);
  HashersSetup(m, hashers, ChooseHasher(params));
  if (BROTLI_IS_OOM(m)) goto oom;

  /* Compute backward references. */
  commands = BROTLI_ALLOC(m, Command, ((input_size + 1) >> 1));
  if (BROTLI_IS_OOM(m)) goto oom;
  BrotliCreateBackwardReferences(m, input_size, input_pos,
      TO_BROTLI_BOOL(is_last), input, mask, params,
      hashers, dist_cache, &last_insert_len, commands, &num_commands,
      &num_literals);
  if (BROTLI_IS_OOM(m)) goto oom;
  DestroyHashers(m, hashers);
  BROTLI_FREE(m, hashers);
  if (last_insert_len > 0) {
    InitInsertCommand(&commands[num_commands++], last_insert_len);
    num_literals += last_insert_len;
  }
  assert(num_commands != 0);

  /* Build the meta-block. */
  MetaBlockSplit mb;
  InitMetaBlockSplit(&mb);
  num_direct_distance_codes = params->mode == BROTLI_MODE_FONT ? 12 : 0;
  distance_postfix_bits = params->mode == BROTLI_MODE_FONT ? 1 : 0;
  literal_context_mode = use_utf8_mode ? CONTEXT_UTF8 : CONTEXT_SIGNED;
  RecomputeDistancePrefixes(commands, num_commands,
                            num_direct_distance_codes,
                            distance_postfix_bits);
  if (params->quality < MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING) {
    BrotliBuildMetaBlockGreedy(m, input, input_pos, mask,
                               commands, num_commands,
                               &mb);
    if (BROTLI_IS_OOM(m)) goto oom;
  } else {
    BrotliBuildMetaBlock(m, input, input_pos, mask, params,
                         prev_byte, prev_byte2,
                         commands, num_commands,
                         literal_context_mode,
                         &mb);
    if (BROTLI_IS_OOM(m)) goto oom;
  }

  /* Set up the temporary output storage. */
  storage = BROTLI_ALLOC(m, uint8_t, 2 * input_size + 500);
  if (BROTLI_IS_OOM(m)) goto oom;
  first_byte = 0;
  first_byte_bits = 0;
  if (is_first) {
    if (params->lgwin == 16) {
      first_byte = 0;
      first_byte_bits = 1;
    } else if (params->lgwin == 17) {
      first_byte = 1;
      first_byte_bits = 7;
    } else {
      first_byte = static_cast<uint8_t>(((params->lgwin - 17) << 1) | 1);
      first_byte_bits = 4;
    }
  }
  storage[0] = static_cast<uint8_t>(first_byte);
  storage_ix = first_byte_bits;

  /* Store the meta-block to the temporary output. */
  BrotliStoreMetaBlock(m, input, input_pos, input_size, mask,
                       prev_byte, prev_byte2,
                       TO_BROTLI_BOOL(is_last),
                       num_direct_distance_codes,
                       distance_postfix_bits,
                       literal_context_mode,
                       commands, num_commands,
                       &mb,
                       &storage_ix, storage);
  if (BROTLI_IS_OOM(m)) goto oom;
  DestroyMetaBlockSplit(m, &mb);
  BROTLI_FREE(m, commands);

  /* If this is not the last meta-block, store an empty metadata
     meta-block so that the meta-block will end at a byte boundary. */
  if (!is_last) {
    BrotliStoreSyncMetaBlock(&storage_ix, storage);
  }

  /* If the compressed data is too large, fall back to an uncompressed
     meta-block. */
  output_size = storage_ix >> 3;
  if (input_size + 4 < output_size) {
    storage[0] = static_cast<uint8_t>(first_byte);
    storage_ix = first_byte_bits;
    BrotliStoreUncompressedMetaBlock(
        TO_BROTLI_BOOL(is_last), input, input_pos, mask, input_size,
        &storage_ix, storage);
    output_size = storage_ix >> 3;
  }

  /* Copy the temporary output with size-check to the output. */
  if (output_size > *encoded_size) {
    BROTLI_FREE(m, storage);
    BROTLI_FREE(m, input);
    return 0;
  }
  memcpy(encoded_buffer, storage, output_size);
  *encoded_size = output_size;
  BROTLI_FREE(m, storage);
  BROTLI_FREE(m, input);
  return 1;

oom:
  BrotliWipeOutMemoryManager(m);
  return 0;
}

}  /* namespace */

int BrotliCompressBufferParallel(BrotliParams compressor_params,
                                 size_t input_size,
                                 const uint8_t* input_buffer,
                                 size_t* encoded_size,
                                 uint8_t* encoded_buffer) {
  if (*encoded_size == 0) {
    /* Output buffer needs at least one byte. */
    return 0;
  } else if (input_size == 0) {
    encoded_buffer[0] = 6;
    *encoded_size = 1;
    return 1;
  }

  BrotliEncoderParams params;
  params.mode = (BrotliEncoderMode)compressor_params.mode;
  params.quality = compressor_params.quality;
  params.lgwin = compressor_params.lgwin;
  params.lgblock = compressor_params.lgblock;

  SanitizeParams(&params);
  params.lgblock = ComputeLgBlock(&params);
  size_t max_input_block_size = 1 << params.lgblock;
  size_t max_prefix_size = 1u << params.lgwin;

  std::vector<std::vector<uint8_t> > compressed_pieces;

  /* Compress block-by-block independently. */
  for (size_t pos = 0; pos < input_size; ) {
    uint32_t input_block_size = static_cast<uint32_t>(
        BROTLI_MIN(size_t, max_input_block_size, input_size - pos));
    uint32_t prefix_size =
        static_cast<uint32_t>(BROTLI_MIN(size_t, max_prefix_size, pos));
    size_t out_size = input_block_size + (input_block_size >> 3) + 1024;
    std::vector<uint8_t> out(out_size);
    if (!WriteMetaBlockParallel(&params,
                                input_block_size,
                                &input_buffer[pos],
                                prefix_size,
                                &input_buffer[pos - prefix_size],
                                (pos == 0) ? 1 : 0,
                                (pos + input_block_size == input_size) ? 1 : 0,
                                &out_size,
                                &out[0])) {
      return 0;
    }
    out.resize(out_size);
    compressed_pieces.push_back(out);
    pos += input_block_size;
  }

  /* Piece together the output. */
  size_t out_pos = 0;
  for (size_t i = 0; i < compressed_pieces.size(); ++i) {
    const std::vector<uint8_t>& out = compressed_pieces[i];
    if (out_pos + out.size() > *encoded_size) {
      return 0;
    }
    memcpy(&encoded_buffer[out_pos], &out[0], out.size());
    out_pos += out.size();
  }
  *encoded_size = out_pos;

  return 1;
}

}  /* namespace brotli */
