// Copyright 2025 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

package brotli

import (
	"bytes"
	"errors"
	"io"
	"strconv"
	"unsafe"
)

// ReaderOptions optional parameters for constructing a Reader.
type ReaderOptions struct {
	RawDictionary []byte
	EagerOutput   bool
	LargeWindow   bool
}

// Reader implements io.Reader by decoding Brotli stream from an underlying Reader.
type Reader struct {
	initialized bool
	closed      bool
	options     ReaderOptions
	state       *_State
}

var errDecodeError = errors.New("brotli: decode error")
var errReaderClosed = errors.New("brotli: Reader is closed")

func toErr(errorCode int32) error {
	if errorCode >= 0 {
		panic("brotli: non-negative error code")
	}
	if errorCode <= -21 {
		panic("brotli: panic error code: " + strconv.Itoa(int(errorCode)))
	}
	return errDecodeError
}

// NewReader initializes new Reader instance.
func NewReader(src io.Reader) io.ReadCloser {
	return NewReaderWithOptions(src, ReaderOptions{})
}

// NewReaderWithOptions initializes new Reader instance with given options.
func NewReaderWithOptions(src io.Reader, options ReaderOptions) io.ReadCloser {
	this := &Reader{
		options:     options,
		state:       makeState(),
		initialized: false,
		closed:      false,
	}
	this.state.input = &src
	return this
}

// Read implements io.Reader.
func (r *Reader) Read(p []byte) (n int, err error) {
	if r.closed {
		return 0, errReaderClosed
	}
	if !r.initialized {
		if errorCode := initState(r.state); errorCode < 0 {
			return 0, toErr(errorCode)
		}
		if r.options.EagerOutput {
			if errorCode := enableEagerOutput(r.state); errorCode < 0 {
				return 0, toErr(errorCode)
			}
		}
		if r.options.LargeWindow {
			if errorCode := enableLargeWindow(r.state); errorCode < 0 {
				return 0, toErr(errorCode)
			}
		}
		dictionaryLen := len(r.options.RawDictionary)
		if dictionaryLen > 0 {
			dictionatyInt8 := unsafe.Slice((*int8)(unsafe.Pointer(&r.options.RawDictionary[0])), dictionaryLen)
			if errorCode := attachDictionaryChunk(r.state, dictionatyInt8); errorCode < 0 {
				return 0, toErr(errorCode)
			}
		}
		r.initialized = true
	}

	length := len(p)
	buf := unsafe.Slice((*int8)(unsafe.Pointer(&p[0])), length)
	r.state.output = buf
	r.state.outputOffset = 0
	r.state.outputLength = int32(length)
	r.state.outputUsed = 0
	if errorCode := decompress(r.state); errorCode < 0 {
		return 0, toErr(errorCode)
	}
	length = int(r.state.outputUsed)
	if length == 0 {
		return 0, io.EOF
	}
	return length, nil
}

// Close implements io.Closer.
func (r *Reader) Close() error {
	r.closed = true
	// If close is asynchronous, then initialized could be still false,
	// even if decoder is (paritally) initialized.
	if r.initialized {
		if errorCode := close(r.state); errorCode < 0 {
			return toErr(errorCode)
		}
	}
	closeInput(r.state)
	return nil
}

// Decode decodes Brotli encoded data.
func Decode(encodedData []byte) ([]byte, error) {
	return DecodeWithRawDictionary(encodedData, nil)
}

// DecodeWithRawDictionary decodes Brotli encoded data with shared dictionary.
func DecodeWithRawDictionary(encodedData []byte, dictionary []byte) ([]byte, error) {
	r := NewReaderWithOptions(bytes.NewReader(encodedData), ReaderOptions{RawDictionary: dictionary})
	defer r.Close()
	return io.ReadAll(r)
}
