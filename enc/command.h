/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// This class models a sequence of literals and a backward reference copy.

#ifndef BROTLI_ENC_COMMAND_H_
#define BROTLI_ENC_COMMAND_H_

#include "./fast_log.h"
#include "./prefix.h"
#include "./types.h"

namespace brotli {

static int insbase[] =   { 0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66,
    98, 130, 194, 322, 578, 1090, 2114, 6210, 22594 };
static int insextra[] =  { 0, 0, 0, 0, 0, 0, 1, 1,  2,  2,  3,  3,  4,  4,  5,
    5,   6,   7,   8,   9,   10,   12,   14,    24 };
static int copybase[] =  { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38,
    54,  70, 102, 134, 198, 326,   582, 1094,  2118 };
static int copyextra[] = { 0, 0, 0, 0, 0, 0, 0, 0,  1,  1,  2,  2,  3,  3,  4,
    4,   5,   5,   6,   7,   8,     9,   10,    24 };

static inline uint16_t GetInsertLengthCode(int insertlen) {
  if (insertlen < 6) {
    return static_cast<uint16_t>(insertlen);
  } else if (insertlen < 130) {
    insertlen -= 2;
    int nbits = Log2FloorNonZero(insertlen) - 1;
    return static_cast<uint16_t>((nbits << 1) + (insertlen >> nbits) + 2);
  } else if (insertlen < 2114) {
    return static_cast<uint16_t>(Log2FloorNonZero(insertlen - 66) + 10);
  } else if (insertlen < 6210) {
    return 21u;
  } else if (insertlen < 22594) {
    return 22u;
  } else {
    return 23u;
  }
}

static inline uint16_t GetCopyLengthCode(int copylen) {
  if (copylen < 10) {
    return static_cast<uint16_t>(copylen - 2);
  } else if (copylen < 134) {
    copylen -= 6;
    int nbits = Log2FloorNonZero(copylen) - 1;
    return static_cast<uint16_t>((nbits << 1) + (copylen >> nbits) + 4);
  } else if (copylen < 2118) {
    return static_cast<uint16_t>(Log2FloorNonZero(copylen - 70) + 12);
  } else {
    return 23u;
  }
}

static inline uint16_t CombineLengthCodes(
    uint16_t inscode, uint16_t copycode, bool use_last_distance) {
  uint16_t bits64 =
      static_cast<uint16_t>((copycode & 0x7u) | ((inscode & 0x7u) << 3));
  if (use_last_distance && inscode < 8 && copycode < 16) {
    return (copycode < 8) ? bits64 : (bits64 | 64);
  } else {
    // "To convert an insert-and-copy length code to an insert length code and
    // a copy length code, the following table can be used"
    static const uint16_t cells[9] = { 128u, 192u, 384u, 256u, 320u, 512u,
                                       448u, 576u, 640u };
    return cells[(copycode >> 3) + 3 * (inscode >> 3)] | bits64;
  }
}

static inline void GetLengthCode(int insertlen, int copylen,
                                 bool use_last_distance,
                                 uint16_t* code, uint64_t* extra) {
  uint16_t inscode = GetInsertLengthCode(insertlen);
  uint16_t copycode = GetCopyLengthCode(copylen);
  uint64_t insnumextra = insextra[inscode];
  uint64_t numextra = insnumextra + copyextra[copycode];
  uint64_t insextraval = insertlen - insbase[inscode];
  uint64_t copyextraval = copylen - copybase[copycode];
  *code = CombineLengthCodes(inscode, copycode, use_last_distance);
  *extra = (numextra << 48) | (copyextraval << insnumextra) | insextraval;
}

struct Command {
  // distance_code is e.g. 0 for same-as-last short code, or 16 for offset 1.
  Command(int insertlen, int copylen, int copylen_code, int distance_code)
      : insert_len_(insertlen), copy_len_(copylen) {
    // The distance prefix and extra bits are stored in this Command as if
    // npostfix and ndirect were 0, they are only recomputed later after the
    // clustering if needed.
    PrefixEncodeCopyDistance(distance_code, 0, 0, &dist_prefix_, &dist_extra_);
    GetLengthCode(insertlen, copylen_code, dist_prefix_ == 0,
                  &cmd_prefix_, &cmd_extra_);
  }

  Command(int insertlen)
      : insert_len_(insertlen), copy_len_(0), dist_prefix_(16), dist_extra_(0) {
    GetLengthCode(insertlen, 4, dist_prefix_ == 0, &cmd_prefix_, &cmd_extra_);
  }

  int DistanceCode() const {
    if (dist_prefix_ < 16) {
      return dist_prefix_;
    }
    int nbits = dist_extra_ >> 24;
    int extra = dist_extra_ & 0xffffff;
    int prefix = dist_prefix_ - 12 - 2 * nbits;
    return (prefix << nbits) + extra + 12;
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
