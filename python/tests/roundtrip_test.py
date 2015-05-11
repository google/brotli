#!/usr/bin/env python
from __future__ import print_function
import sys
import os
from subprocess import check_call, Popen, PIPE

from test_utils import PYTHON, BRO, TEST_ENV, diff_q


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
    for quality in (1, 6, 9, 11):
        filename = os.path.abspath(filename)
        print('Roundtrip testing file "%s" at quality %d' %
              (os.path.basename(filename), quality))
        compressed = os.path.splitext(filename)[0] + ".bro"
        uncompressed = os.path.splitext(filename)[0] + ".unbro"
        check_call([PYTHON, BRO, "-f", "-q", str(quality), "-i", filename,
                    "-o", compressed], env=TEST_ENV)
        check_call([PYTHON, BRO, "-f", "-d", "-i", compressed, "-o",
                    uncompressed], env=TEST_ENV)
        if diff_q(filename, uncompressed) != 0:
            sys.exit(1)
        # Test the streaming version
        with open(filename, "rb") as infile, \
                open(uncompressed, "wb") as outfile:
            p = Popen([PYTHON, BRO, "-q", str(quality)], stdin=infile,
                      stdout=PIPE, env=TEST_ENV)
            check_call([PYTHON, BRO, "-d"], stdin=p.stdout, stdout=outfile,
                       env=TEST_ENV)
        if diff_q(filename, uncompressed) != 0:
            sys.exit(1)
