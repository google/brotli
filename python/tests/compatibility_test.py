#!/usr/bin/env python
from __future__ import print_function
import glob
import sys
import os
from subprocess import check_call

from test_utils import PYTHON, BRO, TEST_ENV, diff_q


os.chdir(os.path.abspath("../../tests"))
for filename in glob.glob("testdata/*.compressed*"):
    filename = os.path.abspath(filename)
    print('Testing decompression of file "%s"' % os.path.basename(filename))
    expected = filename.split(".compressed")[0]
    uncompressed = expected + ".uncompressed"
    check_call([PYTHON, BRO, "-f", "-d", "-i", filename, "-o", uncompressed],
               env=TEST_ENV)
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
    # Test the streaming version
    with open(filename, "rb") as infile, open(uncompressed, "wb") as outfile:
        check_call([PYTHON, BRO, '-d'], stdin=infile, stdout=outfile,
                   env=TEST_ENV)
    if diff_q(uncompressed, expected) != 0:
        sys.exit(1)
    os.unlink(uncompressed)
