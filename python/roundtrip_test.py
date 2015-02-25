#!/usr/bin/env python
from __future__ import print_function
import sys
import os
from subprocess import call, Popen, PIPE
import filecmp


def diff_q(first_file, second_file):
    """Simulate call to POSIX diff with -q argument"""
    if not filecmp.cmp(first_file, second_file, shallow=False):
        print("Files %s and %s differ" % (first_file, second_file))
        return 1
    return 0


BRO = os.path.abspath(os.path.join(".", "bro.py"))

INPUTS = [
    os.path.join("..", "tests", "testdata", "alice29.txt"),
    os.path.join("..", "tests", "testdata", "asyoulik.txt"),
    os.path.join("..", "tests", "testdata", "lcet10.txt"),
    os.path.join("..", "tests", "testdata", "plrabn12.txt"),
    os.path.join("..", "enc", "encode.cc"),
    os.path.join("..", "enc", "dictionary.h"),
    os.path.join("..", "dec", "decode.c"),
    BRO
]


for filename in INPUTS:
    filename = os.path.abspath(filename)
    print('Roundtrip testing of file "%s"' % os.path.basename(filename))
    compressed = os.path.splitext(filename)[0] + ".bro"
    uncompressed = os.path.splitext(filename)[0] + ".unbro"
    with open(filename, "rb") as infile:
        data = infile.read()
        bufsize = len(data)
    call('"%s" -f -i "%s" -o "%s"' % (BRO, filename, compressed), shell=True)
    call('"%s" -f -d -i "%s" -o "%s" -b %d' %
         (BRO, compressed, uncompressed, bufsize), shell=True)
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
    # Test the streaming version
    p = Popen("%s | %s -d -b %d > %s" % (BRO, BRO, bufsize, uncompressed), 
              shell=True, stdin=PIPE)
    p.communicate(data)
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
