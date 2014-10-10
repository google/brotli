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
// This class models a sequence of literals and a backward reference copy.

#ifndef BROTLI_ENC_COMMAND_H_
#define BROTLI_ENC_COMMAND_H_

#include <stdint.h>

namespace brotli {

// Command holds a sequence of literals and a backward reference copy.
class Command {
 public:
  // distance_code_ is initialized to 17 because it refers to the distance
  // code of a backward distance of 1, this way the last insert-only command
  // won't use the last-distance short code, and accordingly distance_prefix_ is
  // set to 16
  Command() : insert_length_(0), copy_length_(0), copy_length_code_(0),
              copy_distance_(0), distance_code_(17),
              distance_prefix_(16), command_prefix_(0),
              distance_extra_bits_(0), distance_extra_bits_value_(0) {}

  uint32_t insert_length_;
  uint32_t copy_length_;
  uint32_t copy_length_code_;
  uint32_t copy_distance_;
  // Values <= 16 are short codes, values > 16 are distances shifted by 16.
  uint32_t distance_code_;
  uint16_t distance_prefix_;
  uint16_t command_prefix_;
  int distance_extra_bits_;
  uint32_t distance_extra_bits_value_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_COMMAND_H_
