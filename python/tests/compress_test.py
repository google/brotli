# Copyright 2016 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import unittest

import _test_utils
import brotli


class TestCompress(_test_utils.TestCase):

    def _check_decompression(self, test_data, **kwargs):
        # Write decompression to temp file and verify it matches the original.
        temp_uncompressed = _test_utils.get_temp_uncompressed_name(test_data)
        temp_compressed = _test_utils.get_temp_compressed_name(test_data)
        original = test_data
        with open(temp_uncompressed, 'wb') as out_file:
            with open(temp_compressed, 'rb') as in_file:
                out_file.write(brotli.decompress(in_file.read(), **kwargs))
        self.assertFilesMatch(temp_uncompressed, original)

    def _compress(self, test_data, **kwargs):
        temp_compressed = _test_utils.get_temp_compressed_name(test_data)
        with open(temp_compressed, 'wb') as out_file:
            with open(test_data, 'rb') as in_file:
                out_file.write(brotli.compress(in_file.read(), **kwargs))

    def _test_compress_quality_1(self, test_data):
        self._compress(test_data, quality=1)
        self._check_decompression(test_data)

    def _test_compress_quality_6(self, test_data):
        self._compress(test_data, quality=6)
        self._check_decompression(test_data)

    def _test_compress_quality_9(self, test_data):
        self._compress(test_data, quality=9)
        self._check_decompression(test_data)

    def _test_compress_quality_11(self, test_data):
        self._compress(test_data, quality=11)
        self._check_decompression(test_data)

    def _test_compress_quality_1_lgwin_10(self, test_data):
        self._compress(test_data, quality=1, lgwin=10)
        self._check_decompression(test_data)

    def _test_compress_quality_6_lgwin_15(self, test_data):
        self._compress(test_data, quality=6, lgwin=15)
        self._check_decompression(test_data)

    def _test_compress_quality_9_lgwin_20(self, test_data):
        self._compress(test_data, quality=9, lgwin=20)
        self._check_decompression(test_data)

    def _test_compress_quality_11_lgwin_24(self, test_data):
        self._compress(test_data, quality=11, lgwin=24)
        self._check_decompression(test_data)

    def _test_compress_quality_1_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=1, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_6_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=6, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_9_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=9, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_11_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=11, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_1_lgwin_10_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=1, lgwin=10, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_6_lgwin_15_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=6, lgwin=15, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_9_lgwin_20_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=9, lgwin=20, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)

    def _test_compress_quality_11_lgwin_24_custom_dictionary(self, test_data):
        with open(test_data, 'rb') as in_file:
            dictionary = in_file.read()
        self._compress(test_data, quality=11, lgwin=24, dictionary=dictionary)
        self._check_decompression(test_data, dictionary=dictionary)


_test_utils.generate_test_methods(TestCompress)

if __name__ == '__main__':
    unittest.main()
