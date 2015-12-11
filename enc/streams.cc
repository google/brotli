/* Copyright 2009 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Convience routines to make Brotli I/O classes from some memory containers and
// files.

#include "./streams.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

namespace brotli {

BrotliMemOut::BrotliMemOut(void* buf, size_t len)
    : buf_(buf),
      len_(len),
      pos_(0) {}

void BrotliMemOut::Reset(void* buf, size_t len) {
  buf_ = buf;
  len_ = len;
  pos_ = 0;
}

// Brotli output routine: copy n bytes to the output buffer.
bool BrotliMemOut::Write(const void *buf, size_t n) {
  if (n + pos_ > len_)
    return false;
  char* p = reinterpret_cast<char*>(buf_) + pos_;
  memcpy(p, buf, n);
  pos_ += n;
  return true;
}

BrotliStringOut::BrotliStringOut(std::string* buf, size_t max_size)
    : buf_(buf),
      max_size_(max_size) {
  assert(buf->empty());
}

void BrotliStringOut::Reset(std::string* buf, size_t max_size) {
  buf_ = buf;
  max_size_ = max_size;
}

// Brotli output routine: add n bytes to a string.
bool BrotliStringOut::Write(const void *buf, size_t n) {
  if (buf_->size() + n > max_size_)
    return false;
  buf_->append(static_cast<const char*>(buf), n);
  return true;
}

BrotliMemIn::BrotliMemIn(const void* buf, size_t len)
    : buf_(buf),
      len_(len),
      pos_(0) {}

void BrotliMemIn::Reset(const void* buf, size_t len) {
  buf_ = buf;
  len_ = len;
  pos_ = 0;
}

// Brotli input routine: read the next chunk of memory.
const void* BrotliMemIn::Read(size_t n, size_t* output) {
  if (pos_ == len_) {
    return NULL;
  }
  if (n > len_ - pos_)
    n = len_ - pos_;
  const char* p = reinterpret_cast<const char*>(buf_) + pos_;
  pos_ += n;
  *output = n;
  return p;
}

BrotliFileIn::BrotliFileIn(FILE* f, size_t max_read_size)
    : f_(f),
      buf_(new char[max_read_size]),
      buf_size_(max_read_size) { }

BrotliFileIn::~BrotliFileIn() {
  delete[] buf_;
}

const void* BrotliFileIn::Read(size_t n, size_t* bytes_read) {
  if (n > buf_size_) {
    n = buf_size_;
  } else if (n == 0) {
    return feof(f_) ? NULL : buf_;
  }
  *bytes_read = fread(buf_, 1, n, f_);
  if (*bytes_read == 0) {
    return NULL;
  } else {
    return buf_;
  }
}

BrotliFileOut::BrotliFileOut(FILE* f) : f_(f) {}

bool BrotliFileOut::Write(const void* buf, size_t n) {
  if (fwrite(buf, n, 1, f_) != 1) {
    return false;
  }
  return true;
}

}  // namespace brotli
