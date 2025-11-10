# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import functools
import os
import unittest

import brotli

from . import _test_utils


def _get_original_name(test_data):
  return test_data.split('.compressed')[0]


class TestDecompressor(_test_utils.TestCase):

  CHUNK_SIZE = 1
  MIN_OUTPUT_BUFFER_SIZE = 32768  # Actually, several bytes less.

  def setUp(self):
    super().setUp()
    self.decompressor = brotli.Decompressor()

  def tearDown(self):
    self.decompressor = None
    super().tearDown()

  def _check_decompression(self, test_data):
    # Verify decompression matches the original.
    temp_uncompressed = _test_utils.get_temp_uncompressed_name(test_data)
    original = _get_original_name(test_data)
    self.assert_files_match(temp_uncompressed, original)

  def _decompress(self, test_data):
    temp_uncompressed = _test_utils.get_temp_uncompressed_name(test_data)
    with open(temp_uncompressed, 'wb') as out_file:
      with open(test_data, 'rb') as in_file:
        read_chunk = functools.partial(in_file.read, self.CHUNK_SIZE)
        for data in iter(read_chunk, b''):
          out_file.write(self.decompressor.process(data))
    self.assertTrue(self.decompressor.is_finished())

  def _decompress_with_limit(self, test_data):
    output_buffer_limit = 10922
    temp_uncompressed = _test_utils.get_temp_uncompressed_name(test_data)
    with open(temp_uncompressed, 'wb') as out_file:
      with open(test_data, 'rb') as in_file:
        chunk_iter = iter(functools.partial(in_file.read, 10 * 1024), b'')
        while not self.decompressor.is_finished():
          data = b''
          if self.decompressor.can_accept_more_data():
            data = next(chunk_iter, b'')
          decompressed_data = self.decompressor.process(
              data, output_buffer_limit=output_buffer_limit
          )
          self.assertLessEqual(
              len(decompressed_data), self.MIN_OUTPUT_BUFFER_SIZE
          )
          out_file.write(decompressed_data)
        self.assertIsNone(next(chunk_iter, None))

  def _test_decompress(self, test_data):
    self._decompress(test_data)
    self._check_decompression(test_data)

  def _test_decompress_with_limit(self, test_data):
    self._decompress_with_limit(test_data)
    self._check_decompression(test_data)

  def test_too_much_input(self):
    with open(
        os.path.join(_test_utils.TESTDATA_DIR, 'zerosukkanooa.compressed'), 'rb'
    ) as in_file:
      compressed = in_file.read()
      self.decompressor.process(compressed[:-1], output_buffer_limit=10240)
      # the following assertion checks whether the test setup is correct
      self.assertFalse(self.decompressor.can_accept_more_data())
      with self.assertRaises(brotli.error):
        self.decompressor.process(compressed[-1:])

  def test_changing_limit(self):
    test_data = os.path.join(
        _test_utils.TESTDATA_DIR, 'zerosukkanooa.compressed'
    )
    check_output = os.path.exists(test_data.replace('.compressed', ''))
    temp_uncompressed = _test_utils.get_temp_uncompressed_name(test_data)
    with open(temp_uncompressed, 'wb') as out_file:
      with open(test_data, 'rb') as in_file:
        compressed = in_file.read()
        uncompressed = self.decompressor.process(
            compressed[:-1], output_buffer_limit=10240
        )
        self.assertLessEqual(len(uncompressed), self.MIN_OUTPUT_BUFFER_SIZE)
        out_file.write(uncompressed)
        while not self.decompressor.can_accept_more_data():
          out_file.write(self.decompressor.process(b''))
        out_file.write(self.decompressor.process(compressed[-1:]))
    if check_output:
      self._check_decompression(test_data)

  def test_garbage_appended(self):
    with self.assertRaises(brotli.error):
      self.decompressor.process(brotli.compress(b'a') + b'a')

  def test_already_finished(self):
    self.decompressor.process(brotli.compress(b'a'))
    with self.assertRaises(brotli.error):
      self.decompressor.process(b'a')


_test_utils.generate_test_methods(TestDecompressor, for_decompression=True)

if __name__ == '__main__':
  unittest.main()
