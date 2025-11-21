# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import queue
import threading
import time

import brotli
import pytest

from . import _test_utils

MIN_OUTPUT_BUFFER_SIZE = 32768  # Actually, several bytes less.


@pytest.mark.parametrize(
    'compressed_name, original_name', _test_utils.gather_compressed_inputs()
)
def test_decompress(compressed_name, original_name):
  decompressor = brotli.Decompressor()
  compressed = _test_utils.take_input(compressed_name)
  original = _test_utils.take_input(original_name)
  chunk_size = 1
  chunks = _test_utils.chunk_input(compressed, chunk_size)
  decompressed = b''
  for chunk in chunks:
    decompressed += decompressor.process(chunk)
  assert decompressor.is_finished()
  assert original == decompressed


@pytest.mark.parametrize(
    'compressed_name, original_name', _test_utils.gather_compressed_inputs()
)
def test_decompress_with_limit(compressed_name, original_name):
  decompressor = brotli.Decompressor()
  compressed = _test_utils.take_input(compressed_name)
  original = _test_utils.take_input(original_name)
  chunk_size = 10 * 1024
  output_buffer_limit = 10922
  chunks = _test_utils.chunk_input(compressed, chunk_size)
  decompressed = b''
  while not decompressor.is_finished():
    data = b''
    if decompressor.can_accept_more_data() and chunks:
      data = chunks.pop(0)
    decompressed_chunk = decompressor.process(
        data, output_buffer_limit=output_buffer_limit
    )
    assert len(decompressed_chunk) <= MIN_OUTPUT_BUFFER_SIZE
    decompressed += decompressed_chunk
  assert not chunks
  assert original == decompressed


def test_too_much_input():
  decompressor = brotli.Decompressor()
  compressed = _test_utils.take_input('zerosukkanooa.compressed')
  decompressor.process(compressed[:-1], output_buffer_limit=10240)
  # The following assertion checks whether the test setup is correct.
  assert not decompressor.can_accept_more_data()
  with pytest.raises(brotli.error):
    decompressor.process(compressed[-1:])


def test_changing_limit():
  decompressor = brotli.Decompressor()
  input_name = 'zerosukkanooa'
  compressed = _test_utils.take_input(input_name + '.compressed')
  check_output = _test_utils.has_input(input_name)
  decompressed = decompressor.process(
      compressed[:-1], output_buffer_limit=10240
  )
  assert len(decompressed) <= MIN_OUTPUT_BUFFER_SIZE
  while not decompressor.can_accept_more_data():
    decompressed += decompressor.process(b'')
  decompressed += decompressor.process(compressed[-1:])
  if check_output:
    original = _test_utils.take_input(input_name)
    assert original == decompressed


def test_garbage_appended():
  decompressor = brotli.Decompressor()
  with pytest.raises(brotli.error):
    decompressor.process(brotli.compress(b'a') + b'a')


def test_already_finished():
  decompressor = brotli.Decompressor()
  decompressor.process(brotli.compress(b'a'))
  with pytest.raises(brotli.error):
    decompressor.process(b'a')


def make_input(size):
  compressor = brotli.Compressor(quality=1)
  prologue = compressor.process(b'b')
  prologue += compressor.flush()
  filler = compressor.process(b'c')
  filler += compressor.flush()
  epilogue = compressor.finish()
  return b''.join([prologue] + [filler] * (size // len(filler)) + [epilogue])


def _thread_decompress(compressed, decompressor, results):
  _ = decompressor.process(compressed)
  if decompressor.is_finished():
    results.put(1)


def _thread_concurrent_process(decompressor, results):
  time.sleep(0.01)
  try:
    _ = decompressor.process(b'')
  except brotli.error:
    results.put(2)


def _thread_concurrent_can_accept_more_data(decompressor, results):
  time.sleep(0.02)
  try:
    _ = decompressor.can_accept_more_data()
  except brotli.error:
    results.put(3)


def _thread_concurrent_is_finished(decompressor, results):
  time.sleep(0.03)
  try:
    _ = decompressor.is_finished()
  except brotli.error:
    results.put(4)


def test_concurrency():
  compressed = make_input(16 * 1024 * 1024)
  decompressor = brotli.Decompressor()
  results = queue.Queue()
  threads = []
  threads.append(
      threading.Thread(
          target=_thread_decompress, args=(compressed, decompressor, results)
      )
  )
  threads.append(
      threading.Thread(
          target=_thread_concurrent_process, args=(decompressor, results)
      )
  )
  threads.append(
      threading.Thread(
          target=_thread_concurrent_can_accept_more_data,
          args=(decompressor, results),
      )
  )
  threads.append(
      threading.Thread(
          target=_thread_concurrent_is_finished, args=(decompressor, results)
      )
  )
  for thread in threads:
    thread.start()
  for thread in threads:
    thread.join()
  assert sorted(list(results.queue)) == [1, 2, 3, 4]
