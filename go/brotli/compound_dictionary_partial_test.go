// Copyright 2026 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

package brotli

import (
	"bytes"
	"testing"
)

func TestCompoundDictionaryPartialRange(t *testing.T) {
	encoded := []byte{0xa1, 0xa0, 0x01, 0xc0, 0x2f, 0x01, 0x10, 0xc6, 0x84, 0x18, 0x0f}
	dictionary := []byte("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\nUNREFERENCED-TRAILER-9f4c7a2e\n")
	want := []byte("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\n")

	got, err := DecodeWithRawDictionary(encoded, dictionary)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("decoded mismatch: got %q, want %q", got, want)
	}
}
