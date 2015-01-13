#!/bin/bash
#
# Test that the brotli command-line tool can decompress old brotli-compressed
# files.

set -o errexit

BRO=../tools/bro
INPUTS="""
testdata/empty.compressed
testdata/x.compressed
testdata/64x.compressed
testdata/10x10y.compressed
testdata/xyzzy.compressed
testdata/quickfox.compressed
testdata/ukkonooa.compressed
testdata/monkey.compressed
testdata/backward65536.compressed
testdata/zeros.compressed
testdata/quickfox_repeated.compressed
testdata/compressed_file.compressed
testdata/compressed_repeated.compressed
testdata/alice29.txt.compressed
testdata/asyoulik.txt.compressed
testdata/lcet10.txt.compressed
testdata/plrabn12.txt.compressed
"""

for file in $INPUTS; do
  echo "Testing decompression of file $file"
  uncompressed=${file%.compressed}.uncompressed
  expected=${file%.compressed}
  $BRO -f -d -i $file -o $uncompressed
  diff -q $uncompressed $expected
  # Test the streaming version
  cat $file | $BRO -d > $uncompressed
  diff -q $uncompressed $expected
done

