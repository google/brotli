// Copyright 2025 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

package brotli_test

import (
	"bytes"
	"fmt"
	"io"
	"math/rand"
	"testing"

	"github.com/google/brotli/go/brotli"
	"github.com/google/brotli/go/cbrotli"
)

func TestReader(t *testing.T) {
	content := bytes.Repeat([]byte("hello world!"), 10000)
	encoded, _ := cbrotli.Encode(content, cbrotli.WriterOptions{Quality: 5})
	r := brotli.NewReader(bytes.NewReader(encoded))
	var decodedOutput bytes.Buffer
	n, err := io.Copy(&decodedOutput, r)
	if err != nil {
		t.Fatalf("Copy(): n=%v, err=%v", n, err)
	}
	if err := r.Close(); err != nil {
		t.Errorf("Close(): %v", err)
	}
	if got := decodedOutput.Bytes(); !bytes.Equal(got, content) {
		t.Errorf(""+
			"Reader output:\n"+
			"%q\n"+
			"want:\n"+
			"<%d bytes>",
			got, len(content))
	}
	buf := make([]byte, 4)
	if _, err := r.Read(buf); err == nil {
		t.Errorf("Read-after-Close expected to return error")
	}
}

func TestDecode(t *testing.T) {
	content := bytes.Repeat([]byte("hello world!"), 10000)
	encoded, _ := cbrotli.Encode(content, cbrotli.WriterOptions{Quality: 5})
	decoded, err := brotli.Decode(encoded)
	if err != nil {
		t.Errorf("Decode: %v", err)
	}
	if !bytes.Equal(decoded, content) {
		t.Errorf(""+
			"Decode content:\n"+
			"%q\n"+
			"want:\n"+
			"<%d bytes>",
			decoded, len(content))
	}
}

func TestDecodeFuzz(t *testing.T) {
	// Test that the decoder terminates with corrupted input.
	content := bytes.Repeat([]byte("hello world!"), 100)
	src := rand.NewSource(0)
	encoded, err := cbrotli.Encode(content, cbrotli.WriterOptions{Quality: 5})
	if err != nil {
		t.Fatalf("Encode(<%d bytes>, _) = _, %s", len(content), err)
	}
	if len(encoded) == 0 {
		t.Fatalf("Encode(<%d bytes>, _) produced empty output", len(content))
	}
	for i := 0; i < 100; i++ {
		enc := append([]byte{}, encoded...)
		for j := 0; j < 5; j++ {
			enc[int(src.Int63())%len(enc)] = byte(src.Int63() % 256)
		}
		brotli.Decode(enc)
	}
}

func TestDecodeTrailingData(t *testing.T) {
	content := bytes.Repeat([]byte("hello world!"), 100)
	encoded, _ := cbrotli.Encode(content, cbrotli.WriterOptions{Quality: 5})
	_, err := brotli.Decode(append(encoded, 0))
	if err == nil {
		t.Errorf("Expected 'excessive input' error")
	}
}

func TestEncodeDecode(t *testing.T) {
	for _, test := range []struct {
		data    []byte
		repeats int
	}{
		{nil, 0},
		{[]byte("A"), 1},
		{[]byte("<html><body><H1>Hello world</H1></body></html>"), 10},
		{[]byte("<html><body><H1>Hello world</H1></body></html>"), 1000},
	} {
		t.Logf("case %q x %d", test.data, test.repeats)
		input := bytes.Repeat(test.data, test.repeats)
		encoded, err := cbrotli.Encode(input, cbrotli.WriterOptions{Quality: 5})
		if err != nil {
			t.Errorf("Encode: %v", err)
		}
		// Inputs are compressible, but may be too small to compress.
		if maxSize := len(input)/2 + 20; len(encoded) >= maxSize {
			t.Errorf(""+
				"Encode returned %d bytes, want <%d\n"+
				"Encoded=%q",
				len(encoded), maxSize, encoded)
		}
		decoded, err := brotli.Decode(encoded)
		if err != nil {
			t.Errorf("Decode: %v", err)
		}
		if !bytes.Equal(decoded, input) {
			var want string
			if len(input) > 320 {
				want = fmt.Sprintf("<%d bytes>", len(input))
			} else {
				want = fmt.Sprintf("%q", input)
			}
			t.Errorf(""+
				"Decode content:\n"+
				"%q\n"+
				"want:\n"+
				"%s",
				decoded, want)
		}
	}
}

func TestEncodeDecodeWithDictionary(t *testing.T) {
	q := 5
	l := 4096

	input := make([]byte, l)
	for i := 0; i < l; i++ {
		input[i] = byte(i*7 + i*i*5)
	}
	// use dictionary same as input
	pd := cbrotli.NewPreparedDictionary(input, cbrotli.DtRaw, q)
	defer pd.Close()

	encoded, err := cbrotli.Encode(input, cbrotli.WriterOptions{Quality: q, Dictionary: pd})
	if err != nil {
		t.Errorf("Encode: %v", err)
	}
	limit := 20
	if len(encoded) > limit {
		t.Errorf("Output length exceeds expectations: %d > %d", len(encoded), limit)
	}

	decoded, err := brotli.DecodeWithRawDictionary(encoded, input)
	if err != nil {
		t.Errorf("Decode: %v", err)
	}
	if !bytes.Equal(decoded, input) {
		var want string
		if len(input) > 320 {
			want = fmt.Sprintf("<%d bytes>", len(input))
		} else {
			want = fmt.Sprintf("%q", input)
		}
		t.Errorf(""+
			"Decode content:\n"+
			"%q\n"+
			"want:\n"+
			"%s",
			decoded, want)
	}
}
