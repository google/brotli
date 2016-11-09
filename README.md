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

### Build instructions

#### Make

To build and run tests, simply do:

    $ ./configure && make

If you want to install brotli, use one of the more advanced build systems below.

#### Bazel

See [Bazel](http://www.bazel.build/)

#### CMake

The basic commands to build, test and install brotli are:

    $ mkdir out && cd out && cmake .. && make
    $ make test
    $ make install

You can use other [CMake](https://cmake.org/) configuration. For example, to
build static libraries and use a custom installation directory:

    $ mkdir out-static && \
      cd out-static && \
      cmake .. -DBUILD_SHARED_LIBS=0 -DCMAKE_INSTALL_PREFIX='/my/install/dir/'
    $ make install

#### Premake5

See [Premake5](https://premake.github.io/)

#### Python

The basic commands to build, test, and install the Python module are:

    $ python setup.py build test
    $ python setup.py install

See the [Python readme](python/README.md) for more details.

### Benchmarks
* [Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/) / [Unstable Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/unstable/)
* [Large Text Compression Benchmark](http://mattmahoney.net/dc/text.html)
* [Lzturbo Benchmark](https://sites.google.com/site/powturbo/home/benchmark)

### Related projects
Independent [decoder](https://github.com/madler/brotli) implementation by Mark Adler, based entirely on format specification.

JavaScript port of brotli [decoder](https://github.com/devongovett/brotli.js). Could be used directly via `npm install brotli`
