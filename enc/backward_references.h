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
// Function to find backward reference copies.

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_H_

#include <stdint.h>
#include <vector>

#include "./hash.h"
#include "./command.h"

namespace brotli {

// "commands" points to the next output command to write to, "*num_commands" is
// initially the total amount of commands output by previous
// CreateBackwardReferences calls, and must be incremented by the amount written
// by this call.
void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const float* literal_cost,
                              size_t literal_cost_mask,
                              const size_t max_backward_limit,
                              const int quality,
                              Hashers* hashers,
                              int hash_type,
                              int* dist_cache,
                              int* last_insert_len,
                              Command* commands,
                              int* num_commands,
                              int* num_literals);

}  // namespace brotli

#endif  // BROTLI_ENC_BACKWARD_REFERENCES_H_
