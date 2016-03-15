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

static uint32_t kInsBase[] =   { 0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50,
    66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594 };
static uint32_t kInsExtra[] =  { 0, 0, 0, 0, 0, 0, 1, 1,  2,  2,  3,  3,  4,  4,
    5,   5,   6,   7,   8,   9,   10,   12,   14,    24 };
static uint32_t kCopyBase[] =  { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30,
    38, 54,  70, 102, 134, 198, 326,   582, 1094,  2118 };
static uint32_t kCopyExtra[] = { 0, 0, 0, 0, 0, 0, 0, 0,  1,  1,  2,  2,  3,  3,
     4,  4,   5,   5,   6,   7,   8,     9,   10,    24 };

static inline uint16_t GetInsertLengthCode(size_t insertlen) {
  if (insertlen < 6) {
    return static_cast<uint16_t>(insertlen);
  } else if (insertlen < 130) {
    insertlen -= 2;
    uint32_t nbits = Log2FloorNonZero(insertlen) - 1u;
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

static inline uint16_t GetCopyLengthCode(size_t copylen) {
  if (copylen < 10) {
    return static_cast<uint16_t>(copylen - 2);
  } else if (copylen < 134) {
    copylen -= 6;
    uint32_t nbits = Log2FloorNonZero(copylen) - 1u;
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

static inline void GetLengthCode(size_t insertlen, size_t copylen,
                                 bool use_last_distance,
                                 uint16_t* code) {
  uint16_t inscode = GetInsertLengthCode(insertlen);
  uint16_t copycode = GetCopyLengthCode(copylen);
  *code = CombineLengthCodes(inscode, copycode, use_last_distance);
}

static inline uint32_t GetInsertBase(uint16_t inscode) {
  return kInsBase[inscode];
}

static inline uint32_t GetInsertExtra(uint16_t inscode) {
  return kInsExtra[inscode];
}

static inline uint32_t GetCopyBase(uint16_t copycode) {
  return kCopyBase[copycode];
}

static inline uint32_t GetCopyExtra(uint16_t copycode) {
  return kCopyExtra[copycode];
}

struct Command {
  // distance_code is e.g. 0 for same-as-last short code, or 16 for offset 1.
  Command(size_t insertlen, size_t copylen, size_t copylen_code,
          size_t distance_code)
      : insert_len_(static_cast<uint32_t>(insertlen)) {
    copy_len_ = static_cast<uint32_t>(
        copylen | ((copylen_code ^ copylen) << 24));
    // The distance prefix and extra bits are stored in this Command as if
    // npostfix and ndirect were 0, they are only recomputed later after the
    // clustering if needed.
    PrefixEncodeCopyDistance(distance_code, 0, 0, &dist_prefix_, &dist_extra_);
    GetLengthCode(insertlen, copylen_code, dist_prefix_ == 0,
                  &cmd_prefix_);
  }

  explicit Command(size_t insertlen)
      : insert_len_(static_cast<uint32_t>(insertlen))
      , copy_len_(4 << 24), dist_extra_(0), dist_prefix_(16) {
    GetLengthCode(insertlen, 4, dist_prefix_ == 0, &cmd_prefix_);
  }

  uint32_t DistanceCode(void) const {
    if (dist_prefix_ < 16) {
      return dist_prefix_;
    }
    uint32_t nbits = dist_extra_ >> 24;
    uint32_t extra = dist_extra_ & 0xffffff;
    uint32_t prefix = dist_prefix_ - 12 - 2 * nbits;
    return (prefix << nbits) + extra + 12;
  }

  uint32_t DistanceContext(void) const {
    uint32_t r = cmd_prefix_ >> 6;
    uint32_t c = cmd_prefix_ & 7;
    if ((r == 0 || r == 2 || r == 4 || r == 7) && (c <= 2)) {
      return c;
    }
    return 3;
  }

  inline uint32_t copy_len(void) const {
    return copy_len_ & 0xFFFFFF;
  }

  inline uint32_t copy_len_code(void) const {
    return (copy_len_ & 0xFFFFFF) ^ (copy_len_ >> 24);
  }

  uint32_t insert_len_;
  /* Stores copy_len in low 24 bits and copy_len XOR copy_code in high 8 bit. */
  uint32_t copy_len_;
  uint32_t dist_extra_;
  uint16_t cmd_prefix_;
  uint16_t dist_prefix_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_COMMAND_H_
