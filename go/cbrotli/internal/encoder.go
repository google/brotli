// Copyright 2016 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

// Package encoder wraps the brotli encoder C API used by package brotli.
package encoder

/*
#include <brotli/encode.h>

// Wrap BrotliEncoderCompressStream so that it doesn't take variable (in-out)
// pointers. Instead of updated pointer, deltas are saved in auxiliary struct.
struct CompressStreamResult {
  size_t bytes_consumed;
  const uint8_t* output_data;
  size_t output_data_size;
  int success;
  int has_more;
};

struct CompressStreamResult CompressStream(
    BrotliEncoderState* s, BrotliEncoderOperation op,
    const uint8_t* data, size_t data_size) {
  struct CompressStreamResult result;
  size_t available_in = data_size;
  const uint8_t* next_in = data;
  size_t available_out = 0;
  result.success = BrotliEncoderCompressStream(s, op,
      &available_in, &next_in, &available_out, 0, 0) ? 1 : 0;
  result.bytes_consumed = data_size - available_in;
  result.output_data = 0;
  result.output_data_size = 0;
  if (result.success) {
    result.output_data = BrotliEncoderTakeOutput(s, &result.output_data_size);
  }
  result.has_more = BrotliEncoderHasMoreOutput(s) ? 1 : 0;
  return result;
}
*/
import "C"
import (
	"unsafe"
)

// Operation represents type of request to CompressStream
type Operation int

const (
	// Process input
	Process Operation = iota
	// Flush input processed so far
	Flush
	// Finish stream
	Finish
)

// Status represents internal state after CompressStream invocation
type Status int

const (
	// Error happened
	Error Status = iota
	// Done means that no more output will be produced
	Done
	// Ok means that more output might be produced with no additional input
	Ok
)

// Encoder is the Brotli c-encoder handle.
type Encoder struct {
	state *C.BrotliEncoderState
}

// New returns a new Brotli c-encoder handle.
// quality and lgWin are described in third_party/Brotli/enc/encode.h.
// Close MUST be called to free resources.
func New(quality, lgWin int) Encoder {
	state := C.BrotliEncoderCreateInstance(nil, nil, nil)
	C.BrotliEncoderSetParameter(
		state, C.BROTLI_PARAM_QUALITY, (C.uint32_t)(quality))
	C.BrotliEncoderSetParameter(
		state, C.BROTLI_PARAM_LGWIN, (C.uint32_t)(lgWin))
	return Encoder{state}
}

// Close frees resources used by encoder.
func (z *Encoder) Close() {
	C.BrotliEncoderDestroyInstance(z.state)
	z.state = nil
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

func cOperation(op Operation) (cOp C.BrotliEncoderOperation) {
	switch op {
	case Flush:
		return C.BROTLI_OPERATION_FLUSH
	case Finish:
		return C.BROTLI_OPERATION_FINISH
	}
	return C.BROTLI_OPERATION_PROCESS
}

// CompressStream processes data and produces Brotli-encoded bytes. Encoder may
// consume considerable amount of input before the first output bytes come out.
// Flush and Finish operations force Encoder to produce output that corresponds
// to input consumed so far. Output contents should not be modified. Liveness of
// output is hard-limited by Encoder liveness; slice becomes invalid when any
// Encoder method is invoked.
func (z *Encoder) CompressStream(in []byte, op Operation) (
	bytesConsumed int, output []byte, status Status) {
	cin, cinSize := cBytes(in)
	result := C.CompressStream(z.state, cOperation(op), cin, cinSize)
	output = C.GoBytes(
		unsafe.Pointer(result.output_data), C.int(result.output_data_size))
	var outcome Status
	if result.success == 0 {
		outcome = Error
	} else if result.has_more != 0 {
		outcome = Ok
	} else {
		outcome = Done
	}
	return int(result.bytes_consumed), output, outcome
}
