#!/usr/bin/env python
from __future__ import print_function
import sys
import os
from subprocess import check_call
import filecmp


def diff_q(first_file, second_file):
    """Simulate call to POSIX diff with -q argument"""
    if not filecmp.cmp(first_file, second_file, shallow=False):
        print("Files %s and %s differ" % (first_file, second_file))
        return 1
    return 0


PYTHON = sys.executable or "python"
BRO = os.path.abspath("../bro.py")

INPUTS = """\
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

os.chdir(os.path.abspath("../../tests"))
for filename in INPUTS.splitlines():
    filename = os.path.abspath(filename)
    print('Testing decompression of file "%s"' % os.path.basename(filename))
    uncompressed = os.path.splitext(filename)[0] + ".uncompressed"
    expected = os.path.splitext(filename)[0]
    check_call([PYTHON, BRO, "-f", "-d", "-i", filename, "-o", uncompressed])
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
    # Test the streaming version
    with open(filename, "rb") as infile, open(uncompressed, "wb") as outfile:
        check_call([PYTHON, BRO, '-d'], stdin=infile, stdout=outfile)
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
