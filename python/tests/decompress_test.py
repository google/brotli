# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import brotli
import pytest

from . import _test_utils


@pytest.mark.parametrize(
    'compressed_name, original_name', _test_utils.gather_compressed_inputs()
)
def test_decompress(compressed_name, original_name):
  compressed = _test_utils.take_input(compressed_name)
  original = _test_utils.take_input(original_name)
  decompressed = brotli.decompress(compressed)
  assert decompressed == original


def test_garbage_appended():
  with pytest.raises(brotli.error):
    brotli.decompress(brotli.compress(b'a') + b'a')
