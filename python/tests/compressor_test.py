# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import brotli
import pytest

from . import _test_utils


@pytest.mark.parametrize("quality", [1, 6, 9, 11])
@pytest.mark.parametrize("text_name", _test_utils.gather_text_inputs())
def test_single_process(quality, text_name):
  original = _test_utils.take_input(text_name)
  compressor = brotli.Compressor(quality=quality)
  compressed = compressor.process(original)
  compressed += compressor.finish()
  decompressed = brotli.decompress(compressed)
  assert original == decompressed


@pytest.mark.parametrize("quality", [1, 6, 9, 11])
@pytest.mark.parametrize("text_name", _test_utils.gather_text_inputs())
def test_multiple_process(quality, text_name):
  original = _test_utils.take_input(text_name)
  chunk_size = 2048
  chunks = _test_utils.chunk_input(original, chunk_size)
  compressor = brotli.Compressor(quality=quality)
  compressed = b''
  for chunk in chunks:
    compressed += compressor.process(chunk)
  compressed += compressor.finish()
  decompressed = brotli.decompress(compressed)
  assert original == decompressed


@pytest.mark.parametrize("quality", [1, 6, 9, 11])
@pytest.mark.parametrize("text_name", _test_utils.gather_text_inputs())
def test_multiple_process_and_flush(quality, text_name):
  original = _test_utils.take_input(text_name)
  chunk_size = 2048
  chunks = _test_utils.chunk_input(original, chunk_size)
  compressor = brotli.Compressor(quality=quality)
  compressed = b''
  for chunk in chunks:
    compressed += compressor.process(chunk)
    compressed += compressor.flush()
  compressed += compressor.finish()
  decompressed = brotli.decompress(compressed)
  assert original == decompressed
