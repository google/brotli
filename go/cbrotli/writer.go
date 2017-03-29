// Copyright 2016 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

package cbrotli

/*
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <brotli/encode.h>

static bool CompressStream(BrotliEncoderState* s, BrotliEncoderOperation op,
                           uint8_t* out, size_t out_len,
                           const uint8_t* in, size_t in_len,
                           size_t* out_written, size_t* in_consumed) {
	size_t in_remaining = in_len;
	size_t out_remaining = out_len;
	bool ok = !!BrotliEncoderCompressStream(s, op, &in_remaining, &in,
	                                        &out_remaining, &out, NULL);
	*out_written = out_len - out_remaining;
	if (in_consumed != NULL) {
		*in_consumed = in_len - in_remaining;
	}
	return ok;
}
*/
import "C"

import (
	"bytes"
	"errors"
	"io"
)

// WriterOptions configures Writer.
type WriterOptions struct {
	// Quality controls the compression-speed vs compression-density trade-offs.
	// The higher the quality, the slower the compression. Range is 0 to 11.
	Quality int
	// LGWin is the base 2 logarithm of the sliding window size.
	// Range is 10 to 24. 0 indicates automatic configuration based on Quality.
	LGWin int
	// BufferSize is the number of bytes to use to buffer encoded output.
	// 0 indicates an implementation-defined default.
	BufferSize int
}

// Writer implements io.WriteCloser by writing Brotli-encoded data to an
// underlying Writer.
type Writer struct {
	dst          io.Writer
	state        *C.BrotliEncoderState
	buf, encoded []byte
}

var (
	errEncode       = errors.New("cbrotli: encode error")
	errWriterClosed = errors.New("cbrotli: Writer is closed")
)

// NewWriter initializes new Writer instance.
// Close MUST be called to free resources.
func NewWriter(dst io.Writer, options WriterOptions) *Writer {
	state := C.BrotliEncoderCreateInstance(nil, nil, nil)
	// TODO(b/18187008): Check if LGBLOCK or MODE are useful to Flywheel.
	C.BrotliEncoderSetParameter(
		state, C.BROTLI_PARAM_QUALITY, (C.uint32_t)(options.Quality))
	C.BrotliEncoderSetParameter(
		state, C.BROTLI_PARAM_LGWIN, (C.uint32_t)(options.LGWin))

	// TODO(bcmills): If the underlying io.Writer implements io.ReaderFrom, use
	// that instead of copying through a secondary buffer.
	bufSize := options.BufferSize
	if bufSize <= 0 {
		// TODO(bcmills): Are there better default buffer sizes to use here?
		//
		// Ideally it would be nice to dynamically resize the buffer based on
		// feedback from the encoder.
		if options.LGWin > 0 {
			bufSize = 1 << uint(options.LGWin)
		} else {
			bufSize = initialReadBufSize
		}
	}
	return &Writer{
		dst:   dst,
		state: state,
		buf:   make([]byte, bufSize),
	}
}

func (w *Writer) flushBuf() error {
	if len(w.encoded) == 0 {
		return nil
	}
	n, err := w.dst.Write(w.encoded)
	w.encoded = w.encoded[n:]
	return err
}

func (w *Writer) untilEmpty(op C.BrotliEncoderOperation) error {
	if w.state == nil {
		return errWriterClosed
	}
	if err := w.flushBuf(); err != nil {
		return err
	}
	for {
		var written C.size_t
		ok := C.CompressStream(w.state, op,
			(*C.uint8_t)(&w.buf[0]), C.size_t(len(w.buf)), nil, 0, &written, nil)
		w.encoded = w.buf[:int(written)]
		if err := w.flushBuf(); err != nil {
			return err
		}
		if written == 0 {
			if !ok {
				return errEncode
			}
			return nil
		}
	}
}

// Flush outputs encoded data for all input provided to Write. The resulting
// output can be decoded to match all input before Flush, but the stream is
// not yet complete until after Close.
// Flush has a negative impact on compression.
func (w *Writer) Flush() error {
	return w.untilEmpty(C.BROTLI_OPERATION_FLUSH)
}

// Close flushes remaining data to the decorated writer
// and frees C resources.
func (w *Writer) Close() error {
	err := w.Flush()
	if err == nil {
		err = w.untilEmpty(C.BROTLI_OPERATION_FINISH)
	}
	C.BrotliEncoderDestroyInstance(w.state)
	w.state = nil
	return err
}

// Write implements io.Writer. Flush or Close must be called to ensure that the
// encoded bytes are actually flushed to the underlying Writer.
func (w *Writer) Write(p []byte) (n int, err error) {
	if w.state == nil {
		return 0, errWriterClosed
	}

	for len(p) > 0 {
		if err := w.flushBuf(); err != nil {
			return n, err
		}

		var written, consumed C.size_t
		ok := C.CompressStream(w.state, C.BROTLI_OPERATION_PROCESS,
			(*C.uint8_t)(&w.buf[0]), C.size_t(len(w.buf)),
			(*C.uint8_t)(&p[0]), C.size_t(len(p)),
			&written, &consumed)
		w.encoded = w.buf[:int(written)]
		n += int(consumed)
		p = p[int(consumed):]

		if !ok {
			return n, errEncode
		}
	}
	return n, nil
}

// Encode returns content encoded with Brotli.
func Encode(content []byte, options WriterOptions) ([]byte, error) {
	var buf bytes.Buffer
	writer := NewWriter(&buf, options)
	_, err := writer.Write(content)
	if closeErr := writer.Close(); err == nil && closeErr != nil {
		err = closeErr
	}
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
