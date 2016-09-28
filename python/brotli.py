# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""Functions to compress and decompress data using the Brotli library."""

import _brotli


# The library version.
__version__ = _brotli.__version__

# The compression mode.
MODE_GENERIC = _brotli.MODE_GENERIC
MODE_TEXT = _brotli.MODE_TEXT
MODE_FONT = _brotli.MODE_FONT

# Compress a byte string.
compress = _brotli.compress

# Decompress a compressed byte string.
decompress = _brotli.decompress

# Raised if compression or decompression fails.
error = _brotli.error
