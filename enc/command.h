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
#include "./fast_log.h"

namespace brotli {

static inline void GetDistCode(int distance_code,
                               uint16_t* code, uint32_t* extra) {
  distance_code -= 1;
  if (distance_code < 16) {
    *code = distance_code;
    *extra = 0;
  } else {
    distance_code -= 12;
    int numextra = Log2FloorNonZero(distance_code) - 1;
    int prefix = distance_code >> numextra;
    *code = 12 + 2 * numextra + prefix;
    *extra = (numextra << 24) | (distance_code - (prefix << numextra));
  }
}

static int insbase[] =   { 0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66,
    98, 130, 194, 322, 578, 1090, 2114, 6210, 22594 };
static int insextra[] =  { 0, 0, 0, 0, 0, 0, 1, 1,  2,  2,  3,  3,  4,  4,  5,
    5,   6,   7,   8,   9,   10,   12,   14,    24 };
static int copybase[] =  { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38,
    54,  70, 102, 134, 198, 326,   582, 1094,  2118 };
static int copyextra[] = { 0, 0, 0, 0, 0, 0, 0, 0,  1,  1,  2,  2,  3,  3,  4,
    4,   5,   5,   6,   7,   8,     9,   10,    24 };

static inline int GetInsertLengthCode(int insertlen) {
  if (insertlen < 6) {
    return insertlen;
  } else if (insertlen < 130) {
    insertlen -= 2;
    int nbits = Log2FloorNonZero(insertlen) - 1;
    return (nbits << 1) + (insertlen >> nbits) + 2;
  } else if (insertlen < 2114) {
    return Log2FloorNonZero(insertlen - 66) + 10;
  } else if (insertlen < 6210) {
    return 21;
  } else if (insertlen < 22594) {
    return 22;
  } else {
    return 23;
  }
}

static inline int GetCopyLengthCode(int copylen) {
  if (copylen < 10) {
    return copylen - 2;
  } else if (copylen < 134) {
    copylen -= 6;
    int nbits = Log2FloorNonZero(copylen) - 1;
    return (nbits << 1) + (copylen >> nbits) + 4;
  } else if (copylen < 2118) {
    return Log2FloorNonZero(copylen - 70) + 12;
  } else {
    return 23;
  }
}

static inline int CombineLengthCodes(
    int inscode, int copycode, int distancecode) {
  int bits64 = (copycode & 0x7u) | ((inscode & 0x7u) << 3);
  if (distancecode == 0 && inscode < 8 && copycode < 16) {
    return (copycode < 8) ? bits64 : (bits64 | 64);
  } else {
    // "To convert an insert-and-copy length code to an insert length code and
    // a copy length code, the following table can be used"
    static const int cells[9] = { 2, 3, 6, 4, 5, 8, 7, 9, 10 };
    return (cells[(copycode >> 3) + 3 * (inscode >> 3)] << 6) | bits64;
  }
}

static inline void GetLengthCode(int insertlen, int copylen, int distancecode,
                                 uint16_t* code, uint64_t* extra) {
  int inscode = GetInsertLengthCode(insertlen);
  int copycode = GetCopyLengthCode(copylen);
  uint64_t insnumextra = insextra[inscode];
  uint64_t numextra = insnumextra + copyextra[copycode];
  uint64_t insextraval = insertlen - insbase[inscode];
  uint64_t copyextraval = copylen - copybase[copycode];
  *code = CombineLengthCodes(inscode, copycode, distancecode);
  *extra = (numextra << 48) | (copyextraval << insnumextra) | insextraval;
}

struct Command {
  Command() {}

  Command(int insertlen, int copylen, int copylen_code, int distance_code)
      : insert_len_(insertlen), copy_len_(copylen) {
    GetDistCode(distance_code, &dist_prefix_, &dist_extra_);
    GetLengthCode(insertlen, copylen_code, dist_prefix_,
                  &cmd_prefix_, &cmd_extra_);
  }

  Command(int insertlen)
      : insert_len_(insertlen), copy_len_(0), dist_prefix_(16), dist_extra_(0) {
    GetLengthCode(insertlen, 4, dist_prefix_, &cmd_prefix_, &cmd_extra_);
  }

  int DistanceCode() const {
    if (dist_prefix_ < 16) {
      return dist_prefix_ + 1;
    }
    int nbits = dist_extra_ >> 24;
    int extra = dist_extra_ & 0xffffff;
    int prefix = dist_prefix_ - 12 - 2 * nbits;
    return (prefix << nbits) + extra + 13;
  }

  int DistanceContext() const {
    int r = cmd_prefix_ >> 6;
    int c = cmd_prefix_ & 7;
    if ((r == 0 || r == 2 || r == 4 || r == 7) && (c <= 2)) {
      return c;
    }
    return 3;
  }

  int insert_len_;
  int copy_len_;
  uint16_t cmd_prefix_;
  uint16_t dist_prefix_;
  uint64_t cmd_extra_;
  uint32_t dist_extra_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_COMMAND_H_
