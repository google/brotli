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
// Input and output classes for streaming brotli compression.

#ifndef BROTLI_ENC_STREAMS_H_
#define BROTLI_ENC_STREAMS_H_

#include <stddef.h>
#include <stdio.h>
#include <string>

namespace brotli {

// Input interface for the compression routines.
class BrotliIn {
 public:
  virtual ~BrotliIn() {}

  // Return a pointer to the next block of input of at most n bytes.
  // Return the actual length in *nread.
  // At end of data, return NULL. Don't return NULL if there is more data
  // to read, even if called with n == 0.
  // Read will only be called if some of its bytes are needed.
  virtual const void* Read(size_t n, size_t* nread) = 0;
};

// Output interface for the compression routines.
class BrotliOut {
 public:
  virtual ~BrotliOut() {}

  // Write n bytes of data from buf.
  // Return true if all written, false otherwise.
  virtual bool Write(const void *buf, size_t n) = 0;
};

// Adapter class to make BrotliIn objects from raw memory.
class BrotliMemIn : public BrotliIn {
 public:
  BrotliMemIn(const void* buf, int len);

  void Reset(const void* buf, int len);

  // returns the amount of data consumed
  int position() const { return pos_; }

  const void* Read(size_t n, size_t* OUTPUT) override;

 private:
  const void* buf_;  // start of input buffer
  int len_;  // length of input
  int pos_;  // current read position within input
};

// Adapter class to make BrotliOut objects from raw memory.
class BrotliMemOut : public BrotliOut {
 public:
  BrotliMemOut(void* buf, int len);

  void Reset(void* buf, int len);

  // returns the amount of data written
  int position() const { return pos_; }

  bool Write(const void* buf, size_t n) override;

 private:
  void* buf_;  // start of output buffer
  int len_;  // length of output
  int pos_;  // current write position within output
};

// Adapter class to make BrotliOut objects from a string.
class BrotliStringOut : public BrotliOut {
 public:
  // Create a writer that appends its data to buf.
  // buf->size() will grow to at most max_size
  // buf is expected to be empty when constructing BrotliStringOut.
  BrotliStringOut(std::string* buf, int max_size);

  void Reset(std::string* buf, int max_len);

  bool Write(const void* buf, size_t n) override;

 private:
  std::string* buf_;  // start of output buffer
  int max_size_;  // max length of output
};

// Adapter class to make BrotliIn object from a file.
class BrotliFileIn : public BrotliIn {
 public:
  BrotliFileIn(FILE* f, size_t max_read_size);
  ~BrotliFileIn();

  const void* Read(size_t n, size_t* bytes_read) override;

 private:
  FILE* f_;
  void* buf_;
  size_t buf_size_;
};

// Adapter class to make BrotliOut object from a file.
class BrotliFileOut : public BrotliOut {
 public:
  explicit BrotliFileOut(FILE* f);

  bool Write(const void* buf, size_t n) override;
 private:
  FILE* f_;
};


}  // namespace brotli

#endif  // BROTLI_ENC_STREAMS_H_
