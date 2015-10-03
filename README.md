brotli
======

Brotli is a generic-purpose lossless compression algorithm that compresses data
using a combination of a modern variant of the LZ77 algorithm, Huffman coding
and 2nd order context modeling, with a compression ratio comparable to the best
currently available general-purpose compression methods. It is similar in speed
with deflate but offers more dense compression.

The specification of the Brotli Compressed Data Format is defined in the
following internet draft:
http://www.ietf.org/id/draft-alakuijala-brotli

Brotli is open-sourced under the Apache License, Version 2.0, see the LICENSE
file.

Brotli mailing list:
https://groups.google.com/forum/#!forum/brotli

Brotli's native language is C/C++

An OCaml wrapper is at https://github.com/fxfactorial/ocaml-brotli

A Lua wrapper is at https://github.com/witchu/lua-brotli and on luarock