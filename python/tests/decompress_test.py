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


# --- output_buffer_limit tests ---

def test_decompress_with_limit_succeeds_within_limit():
  """Decompression succeeds when output fits within the limit."""
  original = b'Hello, brotli!' * 100
  compressed = brotli.compress(original)
  # Set limit well above actual output size
  result = brotli.decompress(compressed, output_buffer_limit=len(original) * 2)
  assert result == original


def test_decompress_with_limit_raises_when_exceeded():
  """Decompression raises brotli.error when output exceeds the limit."""
  original = b'\x00' * (1024 * 1024)  # 1 MB of zeros
  compressed = brotli.compress(original, quality=1)
  with pytest.raises(brotli.error):
    brotli.decompress(compressed, output_buffer_limit=1024)


def test_decompress_with_zero_limit_is_unlimited():
  """output_buffer_limit=0 means no limit (backward compatibility)."""
  original = b'A' * (256 * 1024)
  compressed = brotli.compress(original)
  result = brotli.decompress(compressed, output_buffer_limit=0)
  assert result == original


def test_decompress_without_limit_is_unlimited():
  """Omitting output_buffer_limit is equivalent to 0 (unlimited)."""
  original = b'B' * (256 * 1024)
  compressed = brotli.compress(original)
  result = brotli.decompress(compressed)
  assert result == original


def test_decompress_with_limit_at_exact_boundary():
  """Decompression succeeds when limit equals the decompressed size."""
  original = b'C' * (64 * 1024)
  compressed = brotli.compress(original)
  # The internal buffer allocates in power-of-2 blocks, so the total allocated
  # will be >= len(original). Setting limit to len(original) should succeed
  # since total_allocated is checked against the limit only when the buffer
  # needs to grow beyond the current allocation.
  result = brotli.decompress(compressed, output_buffer_limit=len(original))
  assert result == original


def test_decompress_with_limit_just_below_output():
  """Decompression raises when limit is below the required output size."""
  original = b'\x00' * (512 * 1024)  # 512 KB
  compressed = brotli.compress(original, quality=1)
  # Use half the output size as limit; the buffer grows in power-of-2 blocks,
  # so the limit must be below a block boundary to trigger.
  with pytest.raises(brotli.error):
    brotli.decompress(compressed, output_buffer_limit=len(original) // 2)


@pytest.mark.parametrize(
    'compressed_name, original_name', _test_utils.gather_compressed_inputs()
)
def test_decompress_with_large_limit(compressed_name, original_name):
  """All test vectors decompress successfully with a generous limit."""
  compressed = _test_utils.take_input(compressed_name)
  original = _test_utils.take_input(original_name)
  # Use a limit of at least 1 MB or 2x the original size, whichever is larger.
  generous_limit = max(len(original) * 2, 1024 * 1024)
  result = brotli.decompress(compressed, output_buffer_limit=generous_limit)
  assert result == original


def test_decompress_limit_error_message():
  """The error message for limit exceeded is specific and informative."""
  original = b'\x00' * (1024 * 1024)
  compressed = brotli.compress(original, quality=1)
  with pytest.raises(brotli.error, match='output_buffer_limit'):
    brotli.decompress(compressed, output_buffer_limit=1024)


def test_decompress_limit_keyword_only():
  """output_buffer_limit can be passed as a keyword argument."""
  original = b'test data for keyword arg'
  compressed = brotli.compress(original)
  result = brotli.decompress(compressed, output_buffer_limit=1024)
  assert result == original


def test_decompress_with_negative_limit_is_unlimited():
  """Negative output_buffer_limit is treated as unlimited (no limit)."""
  original = b'D' * (256 * 1024)
  compressed = brotli.compress(original)
  result = brotli.decompress(compressed, output_buffer_limit=-1)
  assert result == original
