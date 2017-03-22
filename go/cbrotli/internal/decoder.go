// Copyright 2016 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

// Package decoder wraps the brotli decoder C API used by package brotli.
package decoder

/*
#include <brotli/decode.h>

// Wrap BrotliDecoderDecompressStream so that it doesn't take variable (in-out)
// pointers. Instead of updated pointer, deltas are saved in auxiliary struct.

struct DecompressStreamResult {
  size_t bytes_consumed;
  const uint8_t* output_data;
  size_t output_data_size;
  BrotliDecoderResult status;
};

struct DecompressStreamResult DecompressStream(BrotliDecoderState* s,
    const uint8_t* encoded_data, size_t encoded_data_size) {
  struct DecompressStreamResult result;
  size_t available_in = encoded_data_size;
  const uint8_t* next_in = encoded_data;
  size_t available_out = 0;
  result.status = BrotliDecoderDecompressStream(s,
      &available_in, &next_in, &available_out, 0, 0);
  result.bytes_consumed = encoded_data_size - available_in;
  result.output_data = 0;
  result.output_data_size = 0;
  if (result.status != BROTLI_DECODER_RESULT_ERROR) {
    result.output_data = BrotliDecoderTakeOutput(s, &result.output_data_size);
    if (BrotliDecoderIsFinished(s)) {
      result.status = BROTLI_DECODER_RESULT_SUCCESS;
    }
  }
  return result;
}

*/
import "C"
import (
	"unsafe"
)

// Status represents internal state after DecompressStream invokation
type Status int

const (
	// Error happened
	Error Status = iota
	// Done means that no more output will be produced
	Done
	// Ok means that more output might be produced with no additional input
	Ok
)

// Decoder is the Brotli c-decoder handle.
type Decoder struct {
	state *C.BrotliDecoderState
}

// New returns a new Brotli c-decoder handle.
// Close MUST be called to free resources.
func New() Decoder {
	return Decoder{state: C.BrotliDecoderCreateInstance(nil, nil, nil)}
}

// Close frees resources used by decoder.
func (z *Decoder) Close() {
	C.BrotliDecoderDestroyInstance(z.state)
	z.state = nil
}

func goStatus(cStatus C.BrotliDecoderResult) (status Status) {
	switch cStatus {
	case C.BROTLI_DECODER_RESULT_SUCCESS:
		return Done
	case C.BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
		return Ok
	case C.BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
		return Ok
	}
	return Error
}

// cBytes casts a Go []byte into a C uint8_t*. We pass &buf[0] directly to C,
// which is legal because C doesn't save the pointer longer than the call and
// the byte array itself doesn't contain any pointers.
func cBytes(buf []byte) (*C.uint8_t, C.size_t) {
	if len(buf) == 0 {
		return (*C.uint8_t)(nil), 0
	}
	return (*C.uint8_t)(unsafe.Pointer(&buf[0])), C.size_t(len(buf))
}

// DecompressStream reads Brotli-encoded bytes from in, and returns produced
// bytes. Output contents should not be modified. Liveness of output is
// hard-limited by Decoder liveness; slice becomes invalid when any Decoder
// method is invoked.
func (z *Decoder) DecompressStream(in []byte) (
	bytesConsumed int, output []byte, status Status) {
	cin, cinSize := cBytes(in)
	result := C.DecompressStream(z.state, cin, cinSize)
	output = C.GoBytes(
		unsafe.Pointer(result.output_data), C.int(result.output_data_size))
	return int(result.bytes_consumed), output, goStatus(result.status)
}
