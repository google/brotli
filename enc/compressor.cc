/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Brotli compressor API C++ wrapper and utilities. */

#include "./compressor.h"

#include <cstdlib>  /* exit */

namespace brotli {

static void SetParams(const BrotliParams* from, BrotliEncoderState* to) {
  BrotliEncoderMode mode = BROTLI_MODE_GENERIC;
  if (from->mode == BrotliParams::MODE_TEXT) {
    mode = BROTLI_MODE_TEXT;
  } else if (from->mode == BrotliParams::MODE_FONT) {
    mode = BROTLI_MODE_FONT;
  }
  BrotliEncoderSetParameter(to, BROTLI_PARAM_MODE, (uint32_t)mode);
  BrotliEncoderSetParameter(to, BROTLI_PARAM_QUALITY, (uint32_t)from->quality);
  BrotliEncoderSetParameter(to, BROTLI_PARAM_LGWIN, (uint32_t)from->lgwin);
  BrotliEncoderSetParameter(to, BROTLI_PARAM_LGBLOCK, (uint32_t)from->lgblock);
}

BrotliCompressor::BrotliCompressor(BrotliParams params) {
  state_ = BrotliEncoderCreateInstance(0, 0, 0);
  if (state_ == 0) std::exit(EXIT_FAILURE);  /* OOM */
  SetParams(&params, state_);
}

BrotliCompressor::~BrotliCompressor(void) {
  BrotliEncoderDestroyInstance(state_);
}

bool BrotliCompressor::WriteMetaBlock(const size_t input_size,
                                      const uint8_t* input_buffer,
                                      const bool is_last, size_t* encoded_size,
                                      uint8_t* encoded_buffer) {
  return !!BrotliEncoderWriteMetaBlock(state_, input_size, input_buffer,
                                       TO_BROTLI_BOOL(is_last), encoded_size,
                                       encoded_buffer);
}

bool BrotliCompressor::WriteMetadata(const size_t input_size,
                                     const uint8_t* input_buffer,
                                     const bool is_last, size_t* encoded_size,
                                     uint8_t* encoded_buffer) {
  return !!BrotliEncoderWriteMetadata(state_, input_size, input_buffer,
                                      TO_BROTLI_BOOL(is_last), encoded_size,
                                      encoded_buffer);
}

bool BrotliCompressor::FinishStream(size_t* encoded_size,
                                    uint8_t* encoded_buffer) {
  return !!BrotliEncoderFinishStream(state_, encoded_size, encoded_buffer);
}

void BrotliCompressor::CopyInputToRingBuffer(const size_t input_size,
                                             const uint8_t* input_buffer) {
  BrotliEncoderCopyInputToRingBuffer(state_, input_size, input_buffer);
}

bool BrotliCompressor::WriteBrotliData(const bool is_last,
                                       const bool force_flush, size_t* out_size,
                                       uint8_t** output) {
  return !!BrotliEncoderWriteData(state_, TO_BROTLI_BOOL(is_last),
      TO_BROTLI_BOOL(force_flush), out_size, output);
}

void BrotliCompressor::BrotliSetCustomDictionary(size_t size,
                                                 const uint8_t* dict) {
  BrotliEncoderSetCustomDictionary(state_, size, dict);
}

int BrotliCompressBuffer(BrotliParams params, size_t input_size,
                         const uint8_t* input_buffer, size_t* encoded_size,
                         uint8_t* encoded_buffer) {
  return BrotliEncoderCompress(params.quality, params.lgwin,
      (BrotliEncoderMode)params.mode, input_size, input_buffer,
      encoded_size, encoded_buffer);
}

int BrotliCompress(BrotliParams params, BrotliIn* in, BrotliOut* out) {
  return BrotliCompressWithCustomDictionary(0, 0, params, in, out);
}

int BrotliCompressWithCustomDictionary(size_t dictsize, const uint8_t* dict,
                                       BrotliParams params, BrotliIn* in,
                                       BrotliOut* out) {
  const size_t kOutputBufferSize = 65536;
  uint8_t* output_buffer;
  bool result = true;
  size_t available_in = 0;
  const uint8_t* next_in = NULL;
  size_t total_out = 0;
  bool end_of_input = false;
  BrotliEncoderState* s;

  s = BrotliEncoderCreateInstance(0, 0, 0);
  if (!s) return 0;
  SetParams(&params, s);
  BrotliEncoderSetCustomDictionary(s, dictsize, dict);
  output_buffer = new uint8_t[kOutputBufferSize];

  while (true) {
    if (available_in == 0 && !end_of_input) {
      next_in = reinterpret_cast<const uint8_t*>(
          in->Read(BrotliEncoderInputBlockSize(s), &available_in));
      if (!next_in) {
        end_of_input = true;
        available_in = 0;
      } else if (available_in == 0) {
        continue;
      }
    }
    size_t available_out = kOutputBufferSize;
    uint8_t* next_out = output_buffer;
    result = !!BrotliEncoderCompressStream(
        s, end_of_input ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS,
        &available_in, &next_in, &available_out, &next_out, &total_out);
    if (!result) break;
    size_t used_output = kOutputBufferSize - available_out;
    if (used_output != 0) {
      result = out->Write(output_buffer, used_output);
      if (!result) break;
    }
    if (BrotliEncoderIsFinished(s)) break;
  }

  delete[] output_buffer;
  BrotliEncoderDestroyInstance(s);
  return result ? 1 : 0;
}


}  /* namespace brotli */
