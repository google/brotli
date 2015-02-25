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

os.chdir(os.path.abspath(os.path.join("..", "..", "tests")))
for filename in INPUTS.splitlines():
    filename = os.path.abspath(filename)
    print('Roundtrip testing of file "%s"' % os.path.basename(filename))
    compressed = os.path.splitext(filename)[0] + ".bro"
    uncompressed = os.path.splitext(filename)[0] + ".unbro"
    with open(filename, "rb") as infile:
        data = infile.read()
    call('"%s" -f -i "%s" -o "%s"' % (BRO, filename, compressed), shell=True)
    call('"%s" -f -d -i "%s" -o "%s"' %
         (BRO, compressed, uncompressed), shell=True)
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
    # Test the streaming version
    p = Popen("%s | %s -d > %s" % (BRO, BRO, uncompressed), stdin=PIPE,
              shell=True)
    p.communicate(data)
    if diff_q(filename, uncompressed) != 0:
        sys.exit(1)
