/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Function to find backward reference copies.

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_H_

#include <vector>

#include "./hash.h"
#include "./command.h"
#include "./types.h"

namespace brotli {

// "commands" points to the next output command to write to, "*num_commands" is
// initially the total amount of commands output by previous
// CreateBackwardReferences calls, and must be incremented by the amount written
// by this call.
void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              bool is_last,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const int quality,
                              const int lgwin,
                              Hashers* hashers,
                              int hash_type,
                              int* dist_cache,
                              size_t* last_insert_len,
                              Command* commands,
                              size_t* num_commands,
                              size_t* num_literals);

static const float kInfinity = std::numeric_limits<float>::infinity();

struct ZopfliNode {
  ZopfliNode(void) : length(1),
                     distance(0),
                     insert_length(0),
                     cost(kInfinity) {}

  inline uint32_t copy_length() const {
    return length & 0xffffff;
  }

  inline uint32_t length_code() const {
    const uint32_t modifier = length >> 24;
    return copy_length() + 9u - modifier;
  }

  inline uint32_t copy_distance() const {
    return distance & 0x1ffffff;
  }

  inline uint32_t distance_code() const {
    const uint32_t short_code = distance >> 25;
    return short_code == 0 ? copy_distance() + 15 : short_code - 1;
  }

  inline uint32_t command_length() const {
    return copy_length() + insert_length;
  }

  // best length to get up to this byte (not including this byte itself)
  // highest 8 bit is used to reconstruct the length code
  uint32_t length;
  // distance associated with the length
  // highest 7 bit contains distance short code + 1 (or zero if no short code)
  uint32_t distance;
  // number of literal inserts before this copy
  uint32_t insert_length;
  // smallest cost to get to this byte from the beginning, as found so far
  float cost;
};

// Computes the shortest path of commands from position to at most
// position + num_bytes.
//
// On return, path->size() is the number of commands found and path[i] is the
// length of the ith command (copy length plus insert length).
// Note that the sum of the lengths of all commands can be less than num_bytes.
//
// On return, the nodes[0..num_bytes] array will have the following
// "ZopfliNode array invariant":
// For each i in [1..num_bytes], if nodes[i].cost < kInfinity, then
//   (1) nodes[i].copy_length() >= 2
//   (2) nodes[i].command_length() <= i and
//   (3) nodes[i - nodes[i].command_length()].cost < kInfinity
void ZopfliComputeShortestPath(size_t num_bytes,
                               size_t position,
                               const uint8_t* ringbuffer,
                               size_t ringbuffer_mask,
                               const size_t max_backward_limit,
                               const int* dist_cache,
                               Hashers::H10* hasher,
                               ZopfliNode* nodes,
                               std::vector<uint32_t>* path);

void ZopfliCreateCommands(const size_t num_bytes,
                          const size_t block_start,
                          const size_t max_backward_limit,
                          const std::vector<uint32_t>& path,
                          const ZopfliNode* nodes,
                          int* dist_cache,
                          size_t* last_insert_len,
                          Command* commands,
                          size_t* num_literals);

}  // namespace brotli

#endif  // BROTLI_ENC_BACKWARD_REFERENCES_H_
