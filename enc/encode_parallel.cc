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
// Implementation of parallel Brotli compressor.

#include "./encode_parallel.h"

#include <algorithm>
#include <limits>
#include <iostream>
#include <thread>
#include <utility>

#include "./backward_references.h"
#include "./bit_cost.h"
#include "./block_splitter.h"
#include "./brotli_bit_stream.h"
#include "./cluster.h"
#include "./context.h"
#include "./metablock.h"
#include "./transform.h"
#include "./entropy_encode.h"
#include "./fast_log.h"
#include "./hash.h"
#include "./histogram.h"
#include "./prefix.h"
#include "./utf8_util.h"
#include "./write_bits.h"

namespace brotli {

namespace {

void RecomputeDistancePrefixes(Command* cmds, size_t num_commands,
                               int num_direct_distance_codes,
                               int distance_postfix_bits) {
  if (num_direct_distance_codes == 0 &&
      distance_postfix_bits == 0) {
    return;
  }
  for (size_t i = 0; i < num_commands; ++i) {
    Command* cmd = &cmds[i];
    if (cmd->copy_len_ > 0 && cmd->cmd_prefix_ >= 128) {
      PrefixEncodeCopyDistance(cmd->DistanceCode(),
                               num_direct_distance_codes,
                               distance_postfix_bits,
                               &cmd->dist_prefix_,
                               &cmd->dist_extra_);
    }
  }
}

bool WriteMetaBlockParallel(const BrotliParams& params,
                            const size_t block_size,
                            const uint8_t* input_buffer,
                            const size_t prefix_size,
                            const uint8_t* prefix_buffer,
                            const bool is_first,
                            const bool is_last,
                            size_t* encoded_size,
                            uint8_t* encoded_buffer) {
  if (block_size == 0) {
    return false;
  }
  const size_t input_size = block_size;

  // Copy prefix + next input block into a continuous area.
  size_t input_pos = prefix_size;
  // CreateBackwardReferences reads up to 3 bytes past the end of input if the
  // mask points past the end of input.
  // FindMatchLengthWithLimit could do another 8 bytes look-forward.
  std::vector<uint8_t> input(prefix_size + input_size + 4 + 8);
  memcpy(&input[0], prefix_buffer, prefix_size);
  memcpy(&input[input_pos], input_buffer, input_size);
  // Since we don't have a ringbuffer, masking is a no-op.
  // We use one less bit than the full range because some of the code uses
  // mask + 1 as the size of the ringbuffer.
  const size_t mask = std::numeric_limits<size_t>::max() >> 1;

  uint8_t prev_byte = input_pos > 0 ? input[(input_pos - 1) & mask] : 0;
  uint8_t prev_byte2 = input_pos > 1 ? input[(input_pos - 2) & mask] : 0;

  // Decide about UTF8 mode.
  static const double kMinUTF8Ratio = 0.75;
  bool utf8_mode = IsMostlyUTF8(&input[0], input_pos, mask, input_size,
                                kMinUTF8Ratio);

  // Initialize hashers.
  int hash_type = std::min(9, params.quality);
  Hashers* hashers = new Hashers();
  hashers->Init(hash_type);

  // Compute backward references.
  int last_insert_len = 0;
  size_t num_commands = 0;
  int num_literals = 0;
  int max_backward_distance = (1 << params.lgwin) - 16;
  int dist_cache[4] = { -4, -4, -4, -4 };
  Command* commands = static_cast<Command*>(
      malloc(sizeof(Command) * ((input_size + 1) >> 1)));
  if (commands == 0) {
    delete hashers;
    return false;
  }
  CreateBackwardReferences(
      input_size, input_pos,
      &input[0], mask,
      max_backward_distance,
      params.quality,
      hashers,
      hash_type,
      dist_cache,
      &last_insert_len,
      commands,
      &num_commands,
      &num_literals);
  delete hashers;
  if (last_insert_len > 0) {
    commands[num_commands++] = Command(last_insert_len);
    num_literals += last_insert_len;
  }
  assert(num_commands != 0);

  // Build the meta-block.
  MetaBlockSplit mb;
  int num_direct_distance_codes =
      params.mode == BrotliParams::MODE_FONT ? 12 : 0;
  int distance_postfix_bits = params.mode == BrotliParams::MODE_FONT ? 1 : 0;
  int literal_context_mode = utf8_mode ? CONTEXT_UTF8 : CONTEXT_SIGNED;
  RecomputeDistancePrefixes(commands, num_commands,
                            num_direct_distance_codes,
                            distance_postfix_bits);
  if (params.quality <= 9) {
    BuildMetaBlockGreedy(&input[0], input_pos, mask,
                         commands, num_commands,
                         &mb);
  } else {
    BuildMetaBlock(&input[0], input_pos, mask,
                   prev_byte, prev_byte2,
                   commands, num_commands,
                   literal_context_mode,
                   &mb);
  }

  // Set up the temporary output storage.
  const size_t max_out_size = 2 * input_size + 500;
  std::vector<uint8_t> storage(max_out_size);
  int first_byte = 0;
  int first_byte_bits = 0;
  if (is_first) {
    if (params.lgwin == 16) {
      first_byte = 0;
      first_byte_bits = 1;
    } else if (params.lgwin == 17) {
      first_byte = 1;
      first_byte_bits = 7;
    } else {
      first_byte = ((params.lgwin - 17) << 1) | 1;
      first_byte_bits = 4;
    }
  }
  storage[0] = static_cast<uint8_t>(first_byte);
  int storage_ix = first_byte_bits;

  // Store the meta-block to the temporary output.
  if (!StoreMetaBlock(&input[0], input_pos, input_size, mask,
                      prev_byte, prev_byte2,
                      is_last,
                      num_direct_distance_codes,
                      distance_postfix_bits,
                      literal_context_mode,
                      commands, num_commands,
                      mb,
                      &storage_ix, &storage[0])) {
    free(commands);
    return false;
  }
  free(commands);

  // If this is not the last meta-block, store an empty metadata
  // meta-block so that the meta-block will end at a byte boundary.
  if (!is_last) {
    StoreSyncMetaBlock(&storage_ix, &storage[0]);
  }

  // If the compressed data is too large, fall back to an uncompressed
  // meta-block.
  size_t output_size = storage_ix >> 3;
  if (input_size + 4 < output_size) {
    storage[0] = static_cast<uint8_t>(first_byte);
    storage_ix = first_byte_bits;
    if (!StoreUncompressedMetaBlock(is_last, &input[0], input_pos, mask,
                                    input_size,
                                    &storage_ix, &storage[0])) {
      return false;
    }
    output_size = storage_ix >> 3;
  }

  // Copy the temporary output with size-check to the output.
  if (output_size > *encoded_size) {
    return false;
  }
  memcpy(encoded_buffer, &storage[0], output_size);
  *encoded_size = output_size;
  return true;
}

}  // namespace

std::pair<size_t, size_t>  consecutiveMapping(const size_t tid, const size_t nThreads, const size_t nElements){

    const size_t elementsPerThread = static_cast<size_t>(ceil(static_cast<double>(nElements) / nThreads));
    const size_t min_i             = tid * elementsPerThread;
    const size_t max_i             = std::min(min_i + elementsPerThread, nElements);


    if(tid > nElements - 1){
	return std::make_pair(0,0);
    }
	
    if(min_i > nElements){
	return std::make_pair(0,0);
    }

	
    return std::make_pair(min_i, max_i);
	
}

    
    
int BrotliCompressBufferParallel(BrotliParams params,
                                 size_t input_size,
                                 const uint8_t* input_buffer,
                                 size_t* encoded_size,
                                 uint8_t* encoded_buffer) {
  if (*encoded_size == 0) {
    // Output buffer needs at least one byte.
    return 0;
  } else  if (input_size == 0) {
    encoded_buffer[0] = 6;
    *encoded_size = 1;
    return 1;
  }

  // Sanitize params.
  if (params.lgwin < kMinWindowBits) {
    params.lgwin = kMinWindowBits;
  } else if (params.lgwin > kMaxWindowBits) {
    params.lgwin = kMaxWindowBits;
  }
  
  if (params.lgblock == 0) {
    params.lgblock = 16;
    if (params.quality >= 9 && params.lgwin > params.lgblock) {
      params.lgblock = std::min(21, params.lgwin);
    }
  } else if (params.lgblock < kMinInputBlockBits) {
    params.lgblock = kMinInputBlockBits;
  } else if (params.lgblock > kMaxInputBlockBits) {
    params.lgblock = kMaxInputBlockBits;
  }

  // Calc number of blocks which will be distributed to threads
  size_t max_input_block_size = 1 << params.lgblock;
  size_t max_num_blocks       = std::max(size_t(1), (input_size / max_input_block_size));
  std::vector<std::thread> threads;
  
  // Output of compression
  std::vector<size_t> out_sizes(max_num_blocks);
  std::vector<std::vector<uint8_t>> outs(max_num_blocks);

  // Debug output
  // std::cout << "max_num_blocks: " << max_num_blocks << std::endl;
  // std::cout << "input_size: " << input_size << std::endl;
  // std::cout << "max_input_block_size: " << max_input_block_size << std::endl;      
  
  // Compress block-by-block independently.
  for(size_t thread_i = 0; thread_i < params.n_threads; thread_i++){
  
      std::thread t([&, thread_i](){

	      size_t min_block_i    = 0;
	      size_t max_block_i    = 0;
	      std::tie(min_block_i, max_block_i) = consecutiveMapping(thread_i, params.n_threads, max_num_blocks);
  
	      for(size_t block_i = min_block_i; block_i < max_block_i; ++block_i){
		  //std::cout << "[" <<  thread_i << "/"<< params.n_threads << "/" << block_i <<"] Compress block " << std::endl;
      
		  size_t input_block_size = std::min(max_input_block_size, input_size - ((block_i+1) * max_input_block_size));
		  size_t pos              = block_i * max_input_block_size;
		  out_sizes[block_i]      = input_block_size + (input_block_size >> 3) + 1024;
		  outs[block_i]           = std::vector<uint8_t>(out_sizes[block_i]);

		  WriteMetaBlockParallel(params,
		  			 input_block_size,
		  			 &input_buffer[pos],
		  			 pos,
		  			 input_buffer,
		  			 pos == 0, // is_first
		  			 pos + input_block_size == input_size, // is_last
		  			 &out_sizes[block_i],
		  			 outs[block_i].data());

		  outs[block_i].resize(out_sizes[block_i]);

	      }
		  
	  });

      threads.push_back(std::move(t));

  }

  // Wait until all threads have finished
  for(auto &t : threads){
      t.join();
  }


  // Piece together the output.
  size_t out_pos = 0;

  for (int i = 0; i < outs.size(); ++i) {
      if (out_pos + outs[i].size() > *encoded_size) {
	  // Encoded buffer is bigger than provided output buffer
  	  std::cout << "Encoded buffer is bigger than provided output buffer" << std::endl;
  	  return false;
	      
      }
	  
      memcpy(&encoded_buffer[out_pos], outs[i].data(), outs[i].size());
      out_pos += outs[i].size();
	  
  }
  
  *encoded_size = out_pos;
      
  
  return true;
}

}  // namespace brotli
