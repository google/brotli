/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function to find backward reference copies. */

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_H_

#include "../common/types.h"
#include "./command.h"
#include "./hash.h"
#include "./memory.h"
#include "./port.h"
#include "./quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* "commands" points to the next output command to write to, "*num_commands" is
   initially the total amount of commands output by previous
   CreateBackwardReferences calls, and must be incremented by the amount written
   by this call. */
BROTLI_INTERNAL void BrotliCreateBackwardReferences(
    MemoryManager* m, size_t num_bytes, size_t position, BROTLI_BOOL is_last,
    const uint8_t* ringbuffer, size_t ringbuffer_mask,
    const BrotliEncoderParams* params, Hashers* hashers, int* dist_cache,
    size_t* last_insert_len, Command* commands, size_t* num_commands,
    size_t* num_literals);

typedef struct ZopfliNode {
  /* best length to get up to this byte (not including this byte itself)
     highest 8 bit is used to reconstruct the length code */
  uint32_t length;
  /* distance associated with the length
     highest 7 bit contains distance short code + 1 (or zero if no short code)
  */
  uint32_t distance;
  /* number of literal inserts before this copy */
  uint32_t insert_length;

  /* This union holds information used by dynamic-programming. During forward
     pass |cost| it used to store the goal function. When node is processed its
     |cost| is invalidated in favor of |shortcut|. On path backtracing pass
     |next| is assigned the offset to next node on the path. */
  union {
    /* Smallest cost to get to this byte from the beginning, as found so far. */
    float cost;
    /* Offset to the next node on the path. Equals to command_length() of the
       next node on the path. For last node equals to BROTLI_UINT32_MAX */
    uint32_t next;
    /* Node position that provides next distance for distance cache. */
    uint32_t shortcut;
  } u;
} ZopfliNode;

BROTLI_INTERNAL void BrotliInitZopfliNodes(ZopfliNode* array, size_t length);

/* Computes the shortest path of commands from position to at most
   position + num_bytes.

   On return, path->size() is the number of commands found and path[i] is the
   length of the ith command (copy length plus insert length).
   Note that the sum of the lengths of all commands can be less than num_bytes.

   On return, the nodes[0..num_bytes] array will have the following
   "ZopfliNode array invariant":
   For each i in [1..num_bytes], if nodes[i].cost < kInfinity, then
     (1) nodes[i].copy_length() >= 2
     (2) nodes[i].command_length() <= i and
     (3) nodes[i - nodes[i].command_length()].cost < kInfinity */
BROTLI_INTERNAL size_t BrotliZopfliComputeShortestPath(
    MemoryManager* m, size_t num_bytes, size_t position,
    const uint8_t* ringbuffer, size_t ringbuffer_mask,
    const BrotliEncoderParams* params, const size_t max_backward_limit,
    const int* dist_cache, H10* hasher, ZopfliNode* nodes);

BROTLI_INTERNAL void BrotliZopfliCreateCommands(const size_t num_bytes,
                                                const size_t block_start,
                                                const size_t max_backward_limit,
                                                const ZopfliNode* nodes,
                                                int* dist_cache,
                                                size_t* last_insert_len,
                                                Command* commands,
                                                size_t* num_literals);

/* Maximum distance, see section 9.1. of the spec. */
static BROTLI_INLINE size_t MaxBackwardLimit(int lgwin) {
  return (1u << lgwin) - 16;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_BACKWARD_REFERENCES_H_ */
