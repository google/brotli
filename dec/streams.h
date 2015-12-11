/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions for streaming input and output. */

#ifndef BROTLI_DEC_STREAMS_H_
#define BROTLI_DEC_STREAMS_H_

#include <stdio.h>
#include "./port.h"
#include "./types.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Function pointer type used to read len bytes into buf. Returns the */
/* number of bytes read or -1 on error. */
typedef int (*BrotliInputFunction)(void* data, uint8_t* buf, size_t len);

/* Input callback function with associated data. */
typedef struct {
  BrotliInputFunction cb_;
  void* data_;
} BrotliInput;

/* Reads len bytes into buf, using the in callback. */
static BROTLI_INLINE int BrotliRead(BrotliInput in, uint8_t* buf, size_t len) {
  return in.cb_(in.data_, buf, len);
}

/* Function pointer type used to write len bytes into buf. Returns the */
/* number of bytes written or -1 on error. */
typedef int (*BrotliOutputFunction)(void* data, const uint8_t* buf, size_t len);

/* Output callback function with associated data. */
typedef struct {
  BrotliOutputFunction cb_;
  void* data_;
} BrotliOutput;

/* Writes len bytes into buf, using the out callback. */
static BROTLI_INLINE int BrotliWrite(BrotliOutput out,
                                     const uint8_t* buf, size_t len) {
  return out.cb_(out.data_, buf, len);
}

/* Memory region with position. */
typedef struct {
  const uint8_t* buffer;
  size_t length;
  size_t pos;
} BrotliMemInput;

/* Input callback where *data is a BrotliMemInput struct. */
int BrotliMemInputFunction(void* data, uint8_t* buf, size_t count);

/* Returns an input callback that wraps the given memory region. */
BrotliInput BrotliInitMemInput(const uint8_t* buffer, size_t length,
                               BrotliMemInput* mem_input);

/* Output buffer with position. */
typedef struct {
  uint8_t* buffer;
  size_t length;
  size_t pos;
} BrotliMemOutput;

/* Output callback where *data is a BrotliMemOutput struct. */
int BrotliMemOutputFunction(void* data, const uint8_t* buf, size_t count);

/* Returns an output callback that wraps the given memory region. */
BrotliOutput BrotliInitMemOutput(uint8_t* buffer, size_t length,
                                 BrotliMemOutput* mem_output);

/* Input callback that reads from a file. */
int BrotliFileInputFunction(void* data, uint8_t* buf, size_t count);
BrotliInput BrotliFileInput(FILE* f);

/* Output callback that writes to a file. */
int BrotliFileOutputFunction(void* data, const uint8_t* buf, size_t count);
BrotliOutput BrotliFileOutput(FILE* f);

/* Output callback that does nothing, always consumes the whole input. */
int BrotliNullOutputFunction(void* data, const uint8_t* buf, size_t count);
BrotliOutput BrotliNullOutput(void);

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif

#endif  /* BROTLI_DEC_STREAMS_H_ */
