# Copyright 2025 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""Functions to compress and decompress data using the Brotli library."""

from ._brotli import (
    # The library version.
    __version__,
    # The compression mode.
    MODE_GENERIC,
    MODE_TEXT,
    MODE_FONT,
    # The Compressor object.
    Compressor,
    # The Decompressor object.
    Decompressor,
    # Decompress a compressed byte string.
    decompress,
    # Raised if compression or decompression fails.
    error,
)


version = __version__


# Compress a byte string.
def compress(string, mode=MODE_GENERIC, quality=11, lgwin=22, lgblock=0):
    compressor = Compressor(mode=mode, quality=quality, lgwin=lgwin, lgblock=lgblock)
    return compressor.process(string) + compressor.finish()
