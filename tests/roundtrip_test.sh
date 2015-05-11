#!/bin/bash
#
# Roundtrip test for the brotli command-line tool.

set -o errexit

BRO=../tools/bro
INPUTS="""
testdata/alice29.txt
testdata/asyoulik.txt
testdata/lcet10.txt
testdata/plrabn12.txt
../enc/encode.cc
../enc/dictionary.h
../dec/decode.c
$BRO
"""

for file in $INPUTS; do
  for quality in 1 6 9 11; do
    echo "Roundtrip testing $file at quality $quality"
    compressed=${file}.bro
    uncompressed=${file}.unbro
    $BRO -f -q $quality -i $file -o $compressed
    $BRO -f -d -i $compressed -o $uncompressed
    diff -q $file $uncompressed
    # Test the streaming version
    cat $file | $BRO -q $quality | $BRO -d >$uncompressed
    diff -q $file $uncompressed
  done
done
