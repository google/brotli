### Introduction

Brotli is a generic-purpose lossless compression algorithm that compresses data
using a combination of a modern variant of the LZ77 algorithm, Huffman coding
and 2nd order context modeling, with a compression ratio comparable to the best
currently available general-purpose compression methods. It is similar in speed
with deflate but offers more dense compression.

The specification of the Brotli Compressed Data Format is defined in [RFC 7932](https://www.ietf.org/rfc/rfc7932.txt).

Brotli is open-sourced under the MIT License, see the LICENSE file.

Brotli mailing list:
https://groups.google.com/forum/#!forum/brotli

[![Build Status](https://travis-ci.org/google/brotli.svg?branch=master)](https://travis-ci.org/google/brotli)

### Benchmarks
* [Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/) / [Unstable Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/unstable/)
* [Large Text Compression Benchmark](http://mattmahoney.net/dc/text.html)
* [Lzturbo Benchmark](https://sites.google.com/site/powturbo/home/benchmark)

### Related projects
Independent [decoder](https://github.com/madler/brotli) implementation by Mark Adler, based entirely on format specification.

JavaScript port of brotli [decoder](https://github.com/devongovett/brotli.js). Could be used directly via `npm install brotli`
