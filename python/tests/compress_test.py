# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import brotli
import pytest

from . import _test_utils


@pytest.mark.parametrize("quality", [1, 6, 9, 11])
@pytest.mark.parametrize("lgwin", [10, 15, 20, 24, 25, 30])
@pytest.mark.parametrize("text_name", _test_utils.gather_text_inputs())
def test_compress(quality, lgwin, text_name):
  original = _test_utils.take_input(text_name)
  compressed = brotli.compress(original, quality=quality, lgwin=lgwin)
  decompressed = brotli.decompress(compressed)
  assert original == decompressed

def test_invalid_lgwin():
  with pytest.raises(brotli.error):
    brotli.compress(b"test", lgwin=9)
  with pytest.raises(brotli.error):
    brotli.compress(b"test", lgwin=31)

