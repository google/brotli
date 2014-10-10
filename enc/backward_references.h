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

void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              const uint8_t* ringbuffer,
                              const float* literal_cost,
                              size_t ringbuffer_mask,
                              const size_t max_backward_limit,
                              Hashers* hashers,
                              Hashers::Type hash_type,
                              std::vector<Command>* commands);

}  // namespace brotli

#endif  // BROTLI_ENC_BACKWARD_REFERENCES_H_
