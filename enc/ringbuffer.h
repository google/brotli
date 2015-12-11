/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Sliding window over the input data.

#ifndef BROTLI_ENC_RINGBUFFER_H_
#define BROTLI_ENC_RINGBUFFER_H_


#include "./port.h"
#include "./types.h"

namespace brotli {

// A RingBuffer(window_bits, tail_bits) contains `1 << window_bits' bytes of
// data in a circular manner: writing a byte writes it to
// `position() % (1 << window_bits)'. For convenience, the RingBuffer array
// contains another copy of the first `1 << tail_bits' bytes:
// buffer_[i] == buffer_[i + (1 << window_bits)] if i < (1 << tail_bits).
class RingBuffer {
 public:
  RingBuffer(int window_bits, int tail_bits)
      : size_((size_t(1) << window_bits)),
        mask_((size_t(1) << window_bits) - 1),
        tail_size_(size_t(1) << tail_bits),
        pos_(0) {
    static const int kSlackForEightByteHashingEverywhere = 7;
    const size_t buflen = size_ + tail_size_;
    buffer_ = new uint8_t[buflen + kSlackForEightByteHashingEverywhere];
    for (int i = 0; i < kSlackForEightByteHashingEverywhere; ++i) {
      buffer_[buflen + i] = 0;
    }
  }
  ~RingBuffer() {
    delete [] buffer_;
  }

  // Push bytes into the ring buffer.
  void Write(const uint8_t *bytes, size_t n) {
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
             std::min(n, (size_ + tail_size_) - masked_pos));
      // Copy into the beginning of the buffer
      memcpy(&buffer_[0], bytes + (size_ - masked_pos),
             n - (size_ - masked_pos));
    }
    pos_ += n;
  }

  void Reset() {
    pos_ = 0;
  }

  // Logical cursor position in the ring buffer.
  size_t position() const { return pos_; }

  // Bit mask for getting the physical position for a logical position.
  size_t mask() const { return mask_; }

  uint8_t *start() { return &buffer_[0]; }
  const uint8_t *start() const { return &buffer_[0]; }

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
  const size_t size_;
  const size_t mask_;
  const size_t tail_size_;

  // Position to write in the ring buffer.
  size_t pos_;
  // The actual ring buffer containing the data and the copy of the beginning
  // as a tail.
  uint8_t *buffer_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_RINGBUFFER_H_
