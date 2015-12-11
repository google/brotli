/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions for streaming input and output. */

#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "./streams.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int BrotliMemInputFunction(void* data, uint8_t* buf, size_t count) {
  BrotliMemInput* input = (BrotliMemInput*)data;
  if (input->pos > input->length) {
    return -1;
  }
  if (input->pos + count > input->length) {
    count = input->length - input->pos;
  }
  memcpy(buf, input->buffer + input->pos, count);
  input->pos += count;
  return (int)count;
}

BrotliInput BrotliInitMemInput(const uint8_t* buffer, size_t length,
                               BrotliMemInput* mem_input) {
  BrotliInput input;
  mem_input->buffer = buffer;
  mem_input->length = length;
  mem_input->pos = 0;
  input.cb_ = &BrotliMemInputFunction;
  input.data_ = mem_input;
  return input;
}

int BrotliMemOutputFunction(void* data, const uint8_t* buf, size_t count) {
  BrotliMemOutput* output = (BrotliMemOutput*)data;
  size_t limit = output->length - output->pos;
  if (count > limit) {
    count = limit;
  }
  memcpy(output->buffer + output->pos, buf, count);
  output->pos += count;
  return (int)count;
}

BrotliOutput BrotliInitMemOutput(uint8_t* buffer, size_t length,
                                 BrotliMemOutput* mem_output) {
  BrotliOutput output;
  mem_output->buffer = buffer;
  mem_output->length = length;
  mem_output->pos = 0;
  output.cb_ = &BrotliMemOutputFunction;
  output.data_ = mem_output;
  return output;
}

int BrotliFileInputFunction(void* data, uint8_t* buf, size_t count) {
  return (int)fread(buf, 1, count, (FILE*)data);
}

BrotliInput BrotliFileInput(FILE* f) {
  BrotliInput in;
  in.cb_ = BrotliFileInputFunction;
  in.data_ = f;
  return in;
}

int BrotliFileOutputFunction(void* data, const uint8_t* buf, size_t count) {
  return (int)fwrite(buf, 1, count, (FILE*)data);
}

BrotliOutput BrotliFileOutput(FILE* f) {
  BrotliOutput out;
  out.cb_ = BrotliFileOutputFunction;
  out.data_ = f;
  return out;
}

int BrotliNullOutputFunction(void* data , const uint8_t* buf, size_t count) {
  BROTLI_UNUSED(data);
  BROTLI_UNUSED(buf);
  return (int)count;
}

BrotliOutput BrotliNullOutput(void) {
  BrotliOutput out;
  out.cb_ = BrotliNullOutputFunction;
  out.data_ = NULL;
  return out;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    /* extern "C" */
#endif
