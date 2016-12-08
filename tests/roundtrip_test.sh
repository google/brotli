#!/usr/bin/env bash
#
# Roundtrip test for the brotli command-line tool.

set -o errexit

BRO=bin/bro
TMP_DIR=bin/tmp
INPUTS="""
tests/testdata/alice29.txt
tests/testdata/asyoulik.txt
tests/testdata/lcet10.txt
tests/testdata/plrabn12.txt
enc/encode.c
common/dictionary.h
dec/decode.c
$BRO
"""

for file in $INPUTS; do
  for quality in 1 6 9 11; do
    echo "Roundtrip testing $file at quality $quality"
    compressed=${TMP_DIR}/${file##*/}.bro
    uncompressed=${TMP_DIR}/${file##*/}.unbro
    $BRO -f -q $quality -i $file -o $compressed
    $BRO -f -d -i $compressed -o $uncompressed
    diff -q $file $uncompressed
    # Test the streaming version
    cat $file | $BRO -q $quality | $BRO -d >$uncompressed
    diff -q $file $uncompressed
  done
done
