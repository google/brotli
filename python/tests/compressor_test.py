# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import filecmp
import functools
import os
import sys
import types
import unittest

import test_utils

import brotli


TEST_DATA_FILES = [
    'empty',  # Empty file
    '10x10y',  # Small text
    'alice29.txt',  # Large text
    'random_org_10k.bin',  # Small data
    'mapsdatazrh',  # Large data
]
TEST_DATA_PATHS = [os.path.join(test_utils.TESTDATA_DIR, f)
                   for f in TEST_DATA_FILES]


def get_compressed_name(filename):
    return filename + '.compressed'


def get_temp_compressed_name(filename):
    return filename + '.bro'


def get_temp_uncompressed_name(filename):
    return filename + '.unbro'


def bind_method_args(method, *args):
    return lambda self: method(self, *args)


# Do not inherit from unittest.TestCase here to ensure that test methods
# are not run automatically and instead are run as part of a specific
# configuration below.
class _TestCompressor(object):

    def _check_decompression_matches(self, test_data):
        # Write decompression to temp file and verify it matches the original.
        with open(get_temp_uncompressed_name(test_data), 'wb') as out_file:
            with open(get_temp_compressed_name(test_data), 'rb') as in_file:
                out_file.write(brotli.decompress(in_file.read()))
        self.assertTrue(
            filecmp.cmp(get_temp_uncompressed_name(test_data),
                        test_data,
                        shallow=False))

    def _test_single_process(self, test_data):
        # Write single-shot compression to temp file.
        with open(get_temp_compressed_name(test_data), 'wb') as out_file:
            with open(test_data, 'rb') as in_file:
                out_file.write(self.compressor.process(in_file.read()))
            out_file.write(self.compressor.finish())
        self._check_decompression_matches(test_data)

    def _test_multiple_process(self, test_data):
        # Write chunked compression to temp file.
        chunk_size = 2048
        with open(get_temp_compressed_name(test_data), 'wb') as out_file:
            with open(test_data, 'rb') as in_file:
                read_chunk = functools.partial(in_file.read, chunk_size)
                for data in iter(read_chunk, b''):
                    out_file.write(self.compressor.process(data))
            out_file.write(self.compressor.finish())
        self._check_decompression_matches(test_data)

    def _test_multiple_process_and_flush(self, test_data):
        # Write chunked and flushed compression to temp file.
        chunk_size = 2048
        with open(get_temp_compressed_name(test_data), 'wb') as out_file:
            with open(test_data, 'rb') as in_file:
                read_chunk = functools.partial(in_file.read, chunk_size)
                for data in iter(read_chunk, b''):
                    out_file.write(self.compressor.process(data))
                    out_file.write(self.compressor.flush())
            out_file.write(self.compressor.finish())
        self._check_decompression_matches(test_data)


# Add test methods for each test data file.  This makes identifying problems
# with specific compression scenarios easier.
for methodname in [m for m in dir(_TestCompressor) if m.startswith('_test')]:
    for test_data in TEST_DATA_PATHS:
        filename = os.path.splitext(os.path.basename(test_data))[0]
        name = 'test_{method}_{file}'.format(method=methodname, file=filename)
        func = bind_method_args(getattr(_TestCompressor, methodname), test_data)
        setattr(_TestCompressor, name, func)


class _CompressionTestCase(unittest.TestCase):

    def tearDown(self):
        for f in TEST_DATA_PATHS:
            try:
                os.unlink(get_temp_compressed_name(f))
            except OSError:
                pass
            try:
                os.unlink(get_temp_uncompressed_name(f))
            except OSError:
                pass


class TestCompressorQuality1(_TestCompressor, _CompressionTestCase):

    def setUp(self):
        self.compressor = brotli.Compressor(quality=1)


class TestCompressorQuality6(_TestCompressor, _CompressionTestCase):

    def setUp(self):
        self.compressor = brotli.Compressor(quality=6)


class TestCompressorQuality9(_TestCompressor, _CompressionTestCase):

    def setUp(self):
        self.compressor = brotli.Compressor(quality=9)


class TestCompressorQuality11(_TestCompressor, _CompressionTestCase):

    def setUp(self):
        self.compressor = brotli.Compressor(quality=11)


if __name__ == '__main__':
    unittest.main()
