// Copyright 2016 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

// Package cbrotli compresses and decompresses data with C-Brotli library.
package cbrotli

import (
	"bytes"
	"errors"
	"io"

	"github.com/google/brotli/go/cgo/decoder"
	"github.com/google/brotli/go/cgo/encoder"
)

//------------------------------------------------------------------------------
// Encoder
//------------------------------------------------------------------------------

// WriterOptions configures Writer.
type WriterOptions struct {
	// Quality controls the compression-speed vs compression-density trade-offs.
	// The higher the quality, the slower the compression. Range is 0 to 11.
	Quality int
	// LGWin is the base 2 logarithm of the sliding window size.
	// Range is 10 to 24. 0 indicates automatic configuration based on Quality.
	LGWin int
}

// ErrEncodeFailure is returned when the encode operation fails.
var ErrEncodeFailure = errors.New("Brotli encode failure")

// ErrWriteAfterClose is returned from Write() if called after Close().
var ErrWriteAfterClose = errors.New("brotli.Writer: write after close")

// Writer implements io.WriteCloser, an io.Writer decorator that produces
// Brotli-encoded data.
type Writer struct {
	dst     io.Writer
	encoder encoder.Encoder
	closed  bool
}

// NewWriter initializes new Writer instance.
// Close MUST be called to free resources.
func NewWriter(dst io.Writer, options WriterOptions) *Writer {
	return &Writer{
		dst:     dst,
		encoder: encoder.New(options.Quality, options.LGWin),
	}
}

// Close implements io.Closer. Close() MUST be invoked to free native resources.
// Also Close() implicitly flushes remaining data to the decorated writer.
func (z *Writer) Close() error {
	if z.closed {
		return nil
	}
	defer z.encoder.Close()
	_, err := z.writeChunk(nil, encoder.Finish)
	z.closed = true
	return err
}

func (z *Writer) writeChunk(p []byte, op encoder.Operation) (int, error) {
	if z.closed {
		return 0, ErrWriteAfterClose
	}
	var totalBytesConsumed int
	var err error
	totalBytesConsumed = 0
	err = nil
	for {
		bytesConsumed, output, status := z.encoder.CompressStream(p, op)
		if status == encoder.Error {
			err = ErrEncodeFailure
			break
		}
		p = p[bytesConsumed:]
		totalBytesConsumed += bytesConsumed
		_, err = z.dst.Write(output)
		if err != nil {
			break
		}
		if len(p) == 0 && status == encoder.Done {
			break
		}
	}
	return totalBytesConsumed, err
}

// Write implements io.Writer.
func (z *Writer) Write(p []byte) (int, error) {
	return z.writeChunk(p, encoder.Process)
}

// Flush outputs encoded data for all input provided to Write(). The resulting
// output can be decoded to match all input before Flush(), but the stream is
// not yet complete until after Close().
// Flush has a negative impact on compression.
func (z *Writer) Flush() error {
	_, err := z.writeChunk(nil, encoder.Finish)
	return err
}

// Encode returns content encoded with Brotli.
func Encode(content []byte, options WriterOptions) ([]byte, error) {
	var buf bytes.Buffer
	writer := NewWriter(&buf, options)
	defer writer.Close()
	_, err := writer.Write(content)
	if err != nil {
		return nil, err
	}
	if err := writer.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

//------------------------------------------------------------------------------
// Decoder
//------------------------------------------------------------------------------

// ErrDecodeFailure is returned the decode operation fails.
var ErrDecodeFailure = errors.New("Brotli decode failure")

// ErrReadAfterClose indicates Read has been called after Close.
var ErrReadAfterClose = errors.New("Read() after Close()")

// Reader implements io.ReadCloser, an io.Reader decorator that decodes
// Brotli-encoded data.
type Reader struct {
	src     io.Reader
	decoder decoder.Decoder
	buf     []byte // intermediate read buffer pointed to by next
	eof     bool   // true if all compressed stream is decoded
	next    []byte // buffered data to be passed to decoder
	output  []byte // data produced by decoder, but not yet consumed
	srcErr  error  // last source reader error
	err     error  // reader state; nil if it is OK to read further
}

// NewReader initializes new Reader instance.
// Close MUST be called to free resources.
func NewReader(src io.Reader) *Reader {
	return &Reader{
		src:     src,
		decoder: decoder.New(),
		buf:     make([]byte, 32*1024),
		eof:     false,
	}
}

// Close implements io.Closer. Close() MUST be invoked to free native resources.
func (z *Reader) Close() error {
	if z.err != ErrReadAfterClose {
		z.decoder.Close()
		z.err = ErrReadAfterClose
	}
	return nil
}

func isEOF(src io.Reader) bool {
	var buf []byte
	buf = make([]byte, 1)
	n, err := src.Read(buf)
	return n == 0 && err == io.EOF
}

// Read implements io.Reader.
func (z *Reader) Read(p []byte) (int, error) {
	// Any error state is unrecoverable.
	if z.err != nil {
		return 0, z.err
	}
	// See io.Reader documentation.
	if len(p) == 0 {
		return 0, nil
	}

	var totalOutBytes int
	totalOutBytes = 0

	// There is no practical limit for amount of bytes being consumed by decoder
	// before producing any output. Continue feeding decoder until some data is
	// produced
	for {
		// Push already produced output first.
		var outBytes int
		outBytes = len(z.output)
		if outBytes != 0 {
			outBytes = copy(p, z.output)
			p = p[outBytes:]
			z.output = z.output[outBytes:]
			totalOutBytes += outBytes
			// Output buffer is full.
			if len(p) == 0 {
				break
			}
			continue
		}
		// No more produced output left.
		// If no more output is expected, then we are finished.
		if z.eof {
			z.err = io.EOF
			break
		}
		// Replenish buffer (might cause blocking read), only if necessary.
		if len(z.next) == 0 && totalOutBytes == 0 && z.srcErr != io.EOF {
			var n int
			n, z.srcErr = z.src.Read(z.buf)
			z.next = z.buf[:n]
			if z.srcErr != nil && z.srcErr != io.EOF {
				z.err = z.srcErr
				break
			}
		}
		// Do decoding.
		consumed, output, status := z.decoder.DecompressStream(z.next)
		z.output = output
		z.next = z.next[consumed:]
		if status == decoder.Error {
			// When error happens, the remaining output does not matter.
			z.err = ErrDecodeFailure
			break
		} else if status == decoder.Done {
			// Decoder stream is closed; not further input is expected.
			if len(z.next) != 0 || (z.srcErr != io.EOF && !isEOF(z.src)) {
				z.err = ErrDecodeFailure
				break
			}
			// No more output is expected; keep pushing output.
			z.eof = true
			continue
		} else {
			// If can not move any further...
			if consumed == 0 && len(z.output) == 0 {
				// Unexpected end of input.
				if z.srcErr == io.EOF || totalOutBytes == 0 {
					z.err = ErrDecodeFailure
				}
				// Postpone blocking reads for the next invocation.
				break
			}
			// Continue pushing output.
		}
	}
	return totalOutBytes, z.err
}

// Decode decodes Brotli encoded data.
func Decode(encodedData []byte) ([]byte, error) {
	var buf bytes.Buffer
	reader := NewReader(bytes.NewReader(encodedData))
	defer reader.Close()
	_, err := io.Copy(&buf, reader)
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
