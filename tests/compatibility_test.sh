#!/bin/bash
#
# Test that the brotli command-line tool can decompress old brotli-compressed
# files.

set -o errexit

BRO=../tools/bro

for file in testdata/*.compressed*; do
  echo "Testing decompression of file $file"
  expected=${file%.compressed*}
  uncompressed=${expected}.uncompressed
  $BRO -f -d -i $file -o $uncompressed
  diff -q $uncompressed $expected
  # Test the streaming version
  cat $file | $BRO -d > $uncompressed
  diff -q $uncompressed $expected
  rm -f $uncompressed
done

