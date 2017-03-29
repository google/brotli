// Copyright 2016 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

// Package cbrotli compresses and decompresses data with C-Brotli library.
package cbrotli

/*
#include <stddef.h>
#include <stdint.h>

#include <brotli/decode.h>

static BrotliDecoderResult DecompressStream(BrotliDecoderState* s,
                                            uint8_t* out, size_t out_len,
                                            const uint8_t* in, size_t in_len,
                                            size_t* out_written,
                                            size_t* in_consumed) {
	size_t in_remaining = in_len;
	size_t out_remaining = out_len;
	BrotliDecoderResult result = BrotliDecoderDecompressStream(
	    s, &in_remaining, &in, &out_remaining, &out, NULL);
	*out_written = out_len - out_remaining;
	*in_consumed = in_len - in_remaining;
	return result;
}

*/
import "C"

import (
	"bytes"
	"errors"
	"io"
)

type decodeError C.BrotliDecoderErrorCode

func (err decodeError) Error() string {
	return "cbrotli: " + C.GoString(C.BrotliDecoderErrorString(C.BrotliDecoderErrorCode(err)))
}

var errExcessiveInput = errors.New("cbrotli: excessive input")

// Reader implements io.ReadCloser by reading Brotli-encoded data from an
// underlying Reader.
type Reader struct {
	src   io.Reader
	state *C.BrotliDecoderState
	buf   []byte // scratch space for reading from src
	in    []byte // current chunk to decode; usually aliases buf
}

// initialBufSize is a "good" buffer size that avoids excessive round-trips
// between C and Go but doesn't waste too much memory on buffering.
// It is arbitrarily chosen to be equal to the constant used in io.Copy.
//
// TODO(bcmills): This constant should be based on empirical measurements.
const initialReadBufSize = 32 * 1024

// NewReader initializes new Reader instance.
// Close MUST be called to free resources.
func NewReader(src io.Reader) *Reader {
	return &Reader{
		src:   src,
		state: C.BrotliDecoderCreateInstance(nil, nil, nil),
		buf:   make([]byte, initialReadBufSize),
	}
}

func (r *Reader) Close() error {
	if r.state != nil {
		C.BrotliDecoderDestroyInstance(r.state)
		r.state = nil
	}
	return nil
}

func (r *Reader) Read(p []byte) (n int, err error) {
	if len(r.in) == 0 {
		n, err := r.src.Read(r.buf)
		if n == 0 && err != nil {
			return 0, err
		}
		r.in = r.buf[:n]
	}

	if len(p) == 0 {
		return 0, nil
	}

	for n == 0 {
		var written, consumed C.size_t
		result := C.DecompressStream(r.state,
			(*C.uint8_t)(&p[0]), C.size_t(len(p)),
			(*C.uint8_t)(&r.in[0]), C.size_t(len(r.in)),
			&written, &consumed)
		r.in = r.in[int(consumed):]
		n = int(written)

		switch result {
		case C.BROTLI_DECODER_RESULT_SUCCESS:
			if len(r.in) > 0 {
				return n, errExcessiveInput
			}
			return n, nil
		case C.BROTLI_DECODER_RESULT_ERROR:
			return n, decodeError(C.BrotliDecoderGetErrorCode(r.state))
		case C.BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
			if n == 0 {
				return 0, io.ErrShortBuffer
			}
			return n, nil
		case C.BROTLI_DECODER_NEEDS_MORE_INPUT:
		}

		if len(r.in) >= len(r.buf)/2 {
			// Too much buffer was left over: expand it to make faster progress.
			//
			// TODO(bcmills): Is there some way to get better feedback about buffer
			// sizes from the C API?
			r.buf = make([]byte, len(r.in)*2)
		}

		// Move unread bytes to the start of the buffer.
		r.in = r.buf[:copy(r.buf, r.in)]

		// Top off the buffer.
		encN, err := r.src.Read(r.in[len(r.in):cap(r.in)])
		if encN == 0 && n == 0 {
			// We need more input to make progress, but there isn't any available.
			if err == io.EOF {
				return 0, io.ErrUnexpectedEOF
			}
			return 0, err
		}
		r.in = r.in[:len(r.in)+encN]
	}

	return n, nil
}

// Decode decodes Brotli encoded data.
func Decode(encodedData []byte) ([]byte, error) {
	r := NewReader(bytes.NewReader(encodedData))
	defer r.Close()
	var buf bytes.Buffer
	_, err := io.Copy(&buf, r)
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
