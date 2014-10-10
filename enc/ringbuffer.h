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
// Sliding window over the input data.

#ifndef BROTLI_ENC_RINGBUFFER_H_
#define BROTLI_ENC_RINGBUFFER_H_

// A RingBuffer(window_bits, tail_bits) contains `1 << window_bits' bytes of
// data in a circular manner: writing a byte writes it to
// `position() % (1 << window_bits)'. For convenience, the RingBuffer array
// contains another copy of the first `1 << tail_bits' bytes:
// buffer_[i] == buffer_[i + (1 << window_bits)] if i < (1 << tail_bits).
class RingBuffer {
 public:
  RingBuffer(int window_bits, int tail_bits)
      : window_bits_(window_bits), tail_bits_(tail_bits), pos_(0) {
    static const int kSlackForThreeByteHashingEverywhere = 2;
    const int buflen = (1 << window_bits_) + (1 << tail_bits_);
    buffer_ = new uint8_t[buflen + kSlackForThreeByteHashingEverywhere];
    for (int i = 0; i < kSlackForThreeByteHashingEverywhere; ++i) {
      buffer_[buflen + i] = 0;
    }
  }
  ~RingBuffer() {
    delete [] buffer_;
  }

  // Push bytes into the ring buffer.
  void Write(const uint8_t *bytes, size_t n) {
    const size_t masked_pos = pos_ & ((1 << window_bits_) - 1);
    // The length of the writes is limited so that we do not need to worry
    // about a write
    WriteTail(bytes, n);
    if (masked_pos + n <= (1 << window_bits_)) {
      // A single write fits.
      memcpy(&buffer_[masked_pos], bytes, n);
    } else {
      // Split into two writes.
      // Copy into the end of the buffer, including the tail buffer.
      memcpy(&buffer_[masked_pos], bytes,
             std::min(n,
                      ((1 << window_bits_) + (1 << tail_bits_)) - masked_pos));
      // Copy into the begining of the buffer
      memcpy(&buffer_[0], bytes + ((1 << window_bits_) - masked_pos),
             n - ((1 << window_bits_) - masked_pos));
    }
    pos_ += n;
  }

  // Logical cursor position in the ring buffer.
  size_t position() const { return pos_; }

  uint8_t *start() { return &buffer_[0]; }
  const uint8_t *start() const { return &buffer_[0]; }

 private:
  void WriteTail(const uint8_t *bytes, size_t n) {
    const size_t masked_pos = pos_ & ((1 << window_bits_) - 1);
    if (masked_pos < (1 << tail_bits_)) {
      // Just fill the tail buffer with the beginning data.
      const size_t p = (1 << window_bits_) + masked_pos;
      memcpy(&buffer_[p], bytes, std::min(n, (1 << tail_bits_) - masked_pos));
    }
  }

  // Size of the ringbuffer is (1 << window_bits) + (1 << tail_bits).
  const int window_bits_;
  const int tail_bits_;

  // Position to write in the ring buffer.
  size_t pos_;
  // The actual ring buffer containing the data and the copy of the beginning
  // as a tail.
  uint8_t *buffer_;
};

#endif  // BROTLI_ENC_RINGBUFFER_H_
