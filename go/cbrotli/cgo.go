// Copyright 2017 Google Inc. All Rights Reserved.
//
// Distributed under MIT license.
// See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

package cbrotli

// Inform golang build system that it should link brotli libraries.
// libbrotlicommon goes last, after the libraries that depend on it, so that
// linking against static libraries works.

// #cgo pkg-config: libbrotlienc libbrotlidec libbrotlicommon
import "C"
