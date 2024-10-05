# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""
Functions to compress and decompress data using the Brotli library.
"""

from typing import Union

ByteString = Union[bytes, bytearray, memoryview]

version: str
__version__: str

MODE_GENERIC: int = 0
MODE_TEXT: int = 1
MODE_FONT: int = 2

class Compressor:
    """An object to compress a byte string."""

    def __init__(
        self, mode=MODE_GENERIC, quality: int = 11, lgwin: int = 22, lgblock: int = 0
    ):
        """
        Args:
            mode (int, optional): The compression mode can be
                - MODE_GENERIC (default),
                - MODE_TEXT (for UTF-8 format text input)
                - MODE_FONT (for WOFF 2.0)
            quality (int, optional): Controls the compression-speed vs compression-density tradeoff.
                The higher the quality, the slower the compression.
                Range is 0 to 11.
                Defaults to 11.
            lgwin (int, optional): Base 2 logarithm of the sliding window size.
                Range is 10 to 24.
                Defaults to 22.
            lgblock (int, optional): Base 2 logarithm of the maximum input block size.
                Range is 16 to 24.
                If set to 0, the value will be set based on the quality.
                Defaults to 0.

        Raises:
            brotli.error: If arguments are invalid.
        """

    def process(self, string: ByteString) -> bytes:
        """Process "string" for compression, returning a string that contains
        compressed output data.  This data should be concatenated to the output
        produced by any preceding calls to the "process()" or "flush()" methods.
        Some or all of the input may be kept in internal buffers for later
        processing, and the compressed output data may be empty until enough input
        has been accumulated.

        Args:
          string (bytes): The input data

        Returns:
          The compressed output data (bytes)

        Raises:
          brotli.error: If compression fails
        """
        ...

    def flush(self) -> bytes:
        """Process all pending input, returning a string containing the remaining
        compressed data. This data should be concatenated to the output produced by
        any preceding calls to the "process()" or "flush()" methods.

        Returns:
          The compressed output data (bytes)

        Raises:
          brotli.error: If compression fails
        """
        ...

    def finish(self) -> bytes:
        """Process all pending input and complete all compression, returning a string
        containing the remaining compressed data. This data should be concatenated
        to the output produced by any preceding calls to the "process()" or "flush()" methods.
        After calling "finish()", the "process()" and "flush()" methods
        cannot be called again, and a new "Compressor" object should be created.

        Returns:
          The compressed output data (bytes)

        Raises:
          brotli.error: If compression fails
        """
        ...

class Decompressor:
    """An object to decompress a byte string."""

    def __init__(self): ...
    def is_finished(self) -> bool:
        """Checks if decoder instance reached the final state.

        Returns:
          True if the decoder is in a state where it reached the end of the input
            and produced all of the output
          False otherwise

        Raises:
          brotli.error: If decompression fails
        """
        ...

    def process(self, string: ByteString) -> bytes:
        """Process "string" for decompression, returning a string that contains
        decompressed output data.  This data should be concatenated to the output
        produced by any preceding calls to the "process()" method.
        Some or all of the input may be kept in internal buffers for later
        processing, and the decompressed output data may be empty until enough input
        has been accumulated.

        Args:
          string (bytes): The input data

        Returns:
          The decompressed output data (bytes)

        Raises:
          brotli.error: If decompression fails
        """
        ...

def compress(
    string: ByteString,
    mode: int = MODE_GENERIC,
    quality: int = 11,
    lgwin: int = 22,
    lgblock: int = 0,
):
    """Compress a byte string.

    Args:
      string (bytes): The input data.
      mode (int, optional): The compression mode can be
        - MODE_GENERIC (default),
        - MODE_TEXT (for UTF-8 format text input)
        - MODE_FONT (for WOFF 2.0)
      quality (int, optional): Controls the compression-speed vs compression-density tradeoff.
        The higher the quality, the slower the compression.
        Range is 0 to 11.
        Defaults to 11.
      lgwin (int, optional): Base 2 logarithm of the sliding window size.
        Range is 10 to 24.
        Defaults to 22.
      lgblock (int, optional): Base 2 logarithm of the maximum input block size.
        Range is 16 to 24.
        If set to 0, the value will be set based on the quality.
        Defaults to 0.

    Returns:
      The compressed byte string.

    Raises:
      brotli.error: If arguments are invalid, or compressor fails.
    """
    ...

def decompress(string: bytes) -> bytes:
    """
    Decompress a compressed byte string.

    Args:
      string (bytes): The compressed input data.

    Returns:
      The decompressed byte string.

    Raises:
      brotli.error: If decompressor fails.
    """
    ...

class error(Exception): ...
