/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Sliding window over the input data.

#ifndef BROTLI_ENC_RINGBUFFER_H_
#define BROTLI_ENC_RINGBUFFER_H_

#include <cstdlib>  /* free, realloc */

#include "./port.h"
#include "./types.h"

namespace brotli {

// A RingBuffer(window_bits, tail_bits) contains `1 << window_bits' bytes of
// data in a circular manner: writing a byte writes it to:
//   `position() % (1 << window_bits)'.
// For convenience, the RingBuffer array contains another copy of the
// first `1 << tail_bits' bytes:
//   buffer_[i] == buffer_[i + (1 << window_bits)], if i < (1 << tail_bits),
// and another copy of the last two bytes:
//   buffer_[-1] == buffer_[(1 << window_bits) - 1] and
//   buffer_[-2] == buffer_[(1 << window_bits) - 2].
class RingBuffer {
 public:
  RingBuffer(int window_bits, int tail_bits)
      : size_(1u << window_bits),
        mask_((1u << window_bits) - 1),
        tail_size_(1u << tail_bits),
        total_size_(size_ + tail_size_),
        cur_size_(0),
        pos_(0),
        data_(0),
        buffer_(0) {}

  ~RingBuffer(void) {
    free(data_);
  }

  // Allocates or re-allocates data_ to the given length + plus some slack
  // region before and after. Fills the slack regions with zeros.
  inline void InitBuffer(const uint32_t buflen) {
    static const size_t kSlackForEightByteHashingEverywhere = 7;
    cur_size_ = buflen;
    data_ = static_cast<uint8_t*>(realloc(
        data_, 2 + buflen + kSlackForEightByteHashingEverywhere));
    buffer_ = data_ + 2;
    buffer_[-2] = buffer_[-1] = 0;
    for (size_t i = 0; i < kSlackForEightByteHashingEverywhere; ++i) {
      buffer_[cur_size_ + i] = 0;
    }
  }

  // Push bytes into the ring buffer.
  void Write(const uint8_t *bytes, size_t n) {
    if (pos_ == 0 && n < tail_size_) {
      // Special case for the first write: to process the first block, we don't
      // need to allocate the whole ringbuffer and we don't need the tail
      // either. However, we do this memory usage optimization only if the
      // first write is less than the tail size, which is also the input block
      // size, otherwise it is likely that other blocks will follow and we
      // will need to reallocate to the full size anyway.
      pos_ = static_cast<uint32_t>(n);
      InitBuffer(pos_);
      memcpy(buffer_, bytes, n);
      return;
    }
    if (cur_size_ < total_size_) {
      // Lazily allocate the full buffer.
      InitBuffer(total_size_);
      // Initialize the last two bytes to zero, so that we don't have to worry
      // later when we copy the last two bytes to the first two positions.
      buffer_[size_ - 2] = 0;
      buffer_[size_ - 1] = 0;
    }
    const size_t masked_pos = pos_ & mask_;
    // The length of the writes is limited so that we do not need to worry
    // about a write
    WriteTail(bytes, n);
    if (PREDICT_TRUE(masked_pos + n <= size_)) {
      // A single write fits.
      memcpy(&buffer_[masked_pos], bytes, n);
    } else {
      // Split into two writes.
      // Copy into the end of the buffer, including the tail buffer.
      memcpy(&buffer_[masked_pos], bytes,
             std::min(n, total_size_ - masked_pos));
      // Copy into the beginning of the buffer
      memcpy(&buffer_[0], bytes + (size_ - masked_pos),
             n - (size_ - masked_pos));
    }
    buffer_[-2] = buffer_[size_ - 2];
    buffer_[-1] = buffer_[size_ - 1];
    pos_ += static_cast<uint32_t>(n);
    if (pos_ > (1u << 30)) {  /* Wrap, but preserve not-a-first-lap feature. */
      pos_ = (pos_ & ((1u << 30) - 1)) | (1u << 30);
    }
  }

  void Reset(void) {
    pos_ = 0;
  }

  // Logical cursor position in the ring buffer.
  uint32_t position(void) const { return pos_; }

  // Bit mask for getting the physical position for a logical position.
  uint32_t mask(void) const { return mask_; }

  uint8_t *start(void) { return &buffer_[0]; }
  const uint8_t *start(void) const { return &buffer_[0]; }

 private:
  void WriteTail(const uint8_t *bytes, size_t n) {
    const size_t masked_pos = pos_ & mask_;
    if (PREDICT_FALSE(masked_pos < tail_size_)) {
      // Just fill the tail buffer with the beginning data.
      const size_t p = size_ + masked_pos;
      memcpy(&buffer_[p], bytes, std::min(n, tail_size_ - masked_pos));
    }
  }

  // Size of the ringbuffer is (1 << window_bits) + tail_size_.
  const uint32_t size_;
  const uint32_t mask_;
  const uint32_t tail_size_;
  const uint32_t total_size_;

  uint32_t cur_size_;
  // Position to write in the ring buffer.
  uint32_t pos_;
  // The actual ring buffer containing the copy of the last two bytes, the data,
  // and the copy of the beginning as a tail.
  uint8_t *data_;
  // The start of the ringbuffer.
  uint8_t *buffer_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_RINGBUFFER_H_
