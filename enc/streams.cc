// Copyright 2009 Google Inc. All Rights Reserved.
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
// Convience routines to make Brotli I/O classes from some memory containers and
// files.

#include "./streams.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

namespace brotli {

BrotliMemOut::BrotliMemOut(void* buf, int len)
    : buf_(buf),
      len_(len),
      pos_(0) {}

void BrotliMemOut::Reset(void* buf, int len) {
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

BrotliStringOut::BrotliStringOut(std::string* buf, int max_size)
    : buf_(buf),
      max_size_(max_size) {
  assert(buf->empty());
}

void BrotliStringOut::Reset(std::string* buf, int max_size) {
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

BrotliMemIn::BrotliMemIn(const void* buf, int len)
    : buf_(buf),
      len_(len),
      pos_(0) {}

void BrotliMemIn::Reset(const void* buf, int len) {
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
      buf_(malloc(max_read_size)),
      buf_size_(max_read_size) {}

BrotliFileIn::~BrotliFileIn() {
  if (buf_) free(buf_);
}

const void* BrotliFileIn::Read(size_t n, size_t* bytes_read) {
  if (buf_ == NULL) {
    *bytes_read = 0;
    return NULL;
  }
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
