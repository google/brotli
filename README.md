<p align="center"><img src="https://brotli.org/brotli.svg" alt="Brotli" width="64"></p>

### Introduction

Brotli is a generic-purpose lossless compression algorithm that compresses data
using a combination of a modern variant of the LZ77 algorithm, Huffman coding
and 2nd order context modeling, with a compression ratio comparable to the best
currently available general-purpose compression methods. It is similar in speed
with deflate but offers more dense compression.

The specification of the Brotli Compressed Data Format is defined in [RFC 7932](https://tools.ietf.org/html/rfc7932).

Brotli is open-sourced under the MIT License, see the LICENSE file.

Brotli mailing list:
https://groups.google.com/forum/#!forum/brotli

[![TravisCI Build Status](https://travis-ci.org/google/brotli.svg?branch=master)](https://travis-ci.org/google/brotli)
[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/google/brotli?branch=master&svg=true)](https://ci.appveyor.com/project/szabadka/brotli)

### Build instructions

#### Autotools-style CMake

[configure-cmake](https://github.com/nemequ/configure-cmake) is an
autotools-style configure script for CMake-based projects.

The basic commands to build, test and install brotli are:

    $ mkdir out && cd out
    $ ../configure-cmake
    $ make
    $ make test
    $ make install

To build static libraries use `--disable-shared-libs` argument:

    $ mkdir out-static && cd out-static
    $ ../configure-cmake --disable-shared-libs
    $ make install

#### Bazel

See [Bazel](http://www.bazel.build/)

#### CMake

The basic commands to build, test and install brotli are:

    $ mkdir out && cd out
    $ cmake ..
    $ make
    $ make test
    $ make install

You can use other [CMake](https://cmake.org/) configuration. For example, to
build static libraries:

    $ mkdir out-static && cd out-static
    $ cmake .. -DBUILD_SHARED_LIBS=OFF
    $ make

#### Premake5

See [Premake5](https://premake.github.io/)

#### Python

To install the Python module from source, run the following:

    $ python setup.py install

See the [Python readme](python/README.md) for more details on testing
and development.

### Benchmarks
* [Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/) / [Unstable Squash Compression Benchmark](https://quixdb.github.io/squash-benchmark/unstable/)
* [Large Text Compression Benchmark](http://mattmahoney.net/dc/text.html)
* [Lzturbo Benchmark](https://sites.google.com/site/powturbo/home/benchmark)

### Related projects
Independent [decoder](https://github.com/madler/brotli) implementation by Mark Adler, based entirely on format specification.

JavaScript port of brotli [decoder](https://github.com/devongovett/brotli.js). Could be used directly via `npm install brotli`
