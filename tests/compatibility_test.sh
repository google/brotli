#!/bin/bash
#
# Test that the brotli command-line tool can decompress old brotli-compressed
# files.

set -o errexit

BRO=../tools/bro
INPUTS="""
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

