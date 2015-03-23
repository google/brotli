#!/usr/bin/env python
from __future__ import print_function
import sys
import os
from subprocess import check_call, Popen, PIPE
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
testdata/alice29.txt
testdata/asyoulik.txt
testdata/lcet10.txt
testdata/plrabn12.txt
../enc/encode.cc
../enc/dictionary.h
../dec/decode.c
%s
""" % BRO

os.chdir(os.path.abspath("../../tests"))
for filename in INPUTS.splitlines():
    filename = os.path.abspath(filename)
    print('Roundtrip testing of file "%s"' % os.path.basename(filename))
    compressed = os.path.splitext(filename)[0] + ".bro"
    uncompressed = os.path.splitext(filename)[0] + ".unbro"
    check_call([PYTHON, BRO, "-f", "-i", filename, "-o", compressed])
    check_call([PYTHON, BRO, "-f", "-d", "-i", compressed, "-o", uncompressed])
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
    # Test the streaming version
    with open(filename, "rb") as infile, open(uncompressed, "wb") as outfile:
        p = Popen([PYTHON, BRO], stdin=infile, stdout=PIPE)
        check_call([PYTHON, BRO, "-d"], stdin=p.stdout, stdout=outfile)
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
