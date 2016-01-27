/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Function to find backward reference copies.

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_H_

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

}  // namespace brotli

#endif  // BROTLI_ENC_BACKWARD_REFERENCES_H_
