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
// API for Brotli compression

#ifndef BROTLI_ENC_ENCODE_H_
#define BROTLI_ENC_ENCODE_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace brotli {

// Compresses the data in input_buffer into encoded_buffer, and sets
// *encoded_size to the compressed length.
// Returns 0 if there was an error and 1 otherwise.
int BrotliCompressBuffer(size_t input_size,
                         const uint8_t* input_buffer,
                         size_t* encoded_size,
                         uint8_t* encoded_buffer);


}  // namespace brotli

#endif  // BROTLI_ENC_ENCODE_H_
