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
    os.path.join("..", "tests", "testdata", "empty.compressed"),
    os.path.join("..", "tests", "testdata", "x.compressed"),
    os.path.join("..", "tests", "testdata", "64x.compressed"),
    os.path.join("..", "tests", "testdata", "10x10y.compressed"),
    os.path.join("..", "tests", "testdata", "xyzzy.compressed"),
    os.path.join("..", "tests", "testdata", "quickfox.compressed"),
    os.path.join("..", "tests", "testdata", "ukkonooa.compressed"),
    os.path.join("..", "tests", "testdata", "monkey.compressed"),
    os.path.join("..", "tests", "testdata", "backward65536.compressed"),
    os.path.join("..", "tests", "testdata", "zeros.compressed"),
    os.path.join("..", "tests", "testdata", "quickfox_repeated.compressed"),
    os.path.join("..", "tests", "testdata", "compressed_file.compressed"),
    os.path.join("..", "tests", "testdata", "compressed_repeated.compressed"),
    os.path.join("..", "tests", "testdata", "alice29.txt.compressed"),
    os.path.join("..", "tests", "testdata", "asyoulik.txt.compressed"),
    os.path.join("..", "tests", "testdata", "lcet10.txt.compressed"),
    os.path.join("..", "tests", "testdata", "plrabn12.txt.compressed"),
]

for filename in INPUTS:
    filename = os.path.abspath(filename)
    print('Testing decompression of file "%s"' % os.path.basename(filename))
    uncompressed = os.path.splitext(filename)[0] + ".uncompressed"
    expected = os.path.splitext(filename)[0]
    with open(expected, "rb") as f:
        f.seek(0, 2)
        bufsize = f.tell()
    call('"%s" -f -d -i "%s" -o "%s" -b %d' %
         (BRO, filename, uncompressed, bufsize), shell=True)
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
    # Test the streaming version
    p = Popen('"%s" -d -b %d > "%s"' % (BRO, bufsize, uncompressed),
              shell=True, stdin=PIPE)
    with open(filename, "rb") as infile:
        data = infile.read()
    p.communicate(data)
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
