#!/usr/bin/env python3
# Copyright 2026 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""Regression suite for brotli CLI output-path symlink handling.

Usage:
  python3 tests/regression/t02/symlink_output_regression_test.py /path/to/brotli
"""

import argparse
import subprocess
import tempfile
import unittest
from pathlib import Path

PLAIN_BYTES = b"seed content for the symlink regression test\n"
TARGET_BYTES = b"do not touch me\n"


class SymlinkOutputRegressionTest(unittest.TestCase):
  brotli = None

  def setUp(self):
    self.tmpdir = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmpdir.cleanup)
    self.workdir = Path(self.tmpdir.name)

  def run_brotli(self, *args, check=False):
    return subprocess.run(
        [str(self.brotli)] + [str(arg) for arg in args],
        capture_output=True,
        check=False)

  def make_compressed_input(self, payload=PLAIN_BYTES):
    src = self.workdir / "in.bin"
    compressed = self.workdir / "in.bin.br"
    src.write_bytes(payload)
    self.run_brotli("-f", "-k", src, "-o", compressed)
    return compressed

  def test_force_decompress_refuses_symlinked_output(self):
    compressed = self.make_compressed_input()
    target = self.workdir / "target.txt"
    target.write_bytes(TARGET_BYTES)
    link = self.workdir / "out.br.out"
    link.symlink_to(target)

    proc = self.run_brotli("-d", "-f", compressed, "-o", link)

    self.assertNotEqual(proc.returncode, 0,
                        "brotli should refuse to write through a symlink")
    self.assertEqual(target.read_bytes(), TARGET_BYTES,
                     "target file must be untouched by the refused write")
    self.assertTrue(link.is_symlink(), "the symlink itself must survive")

  def test_force_decompress_still_overwrites_a_regular_file(self):
    compressed = self.make_compressed_input()
    out_path = self.workdir / "out.bin"
    out_path.write_bytes(b"stale contents from a previous run\n")

    proc = self.run_brotli("-d", "-f", compressed, "-o", out_path)

    self.assertEqual(proc.returncode, 0, proc.stderr.decode("utf-8", "replace"))
    self.assertEqual(out_path.read_bytes(), PLAIN_BYTES)

  def test_decompress_without_force_still_refuses_symlinked_output(self):
    compressed = self.make_compressed_input()
    target = self.workdir / "target2.txt"
    target.write_bytes(TARGET_BYTES)
    link = self.workdir / "out2.br.out"
    link.symlink_to(target)

    proc = self.run_brotli("-d", compressed, "-o", link)

    self.assertNotEqual(proc.returncode, 0)
    self.assertEqual(target.read_bytes(), TARGET_BYTES)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("brotli", help="path to the brotli CLI binary to test")
  parser.add_argument("-v", "--verbose", action="store_true")
  args = parser.parse_args()

  brotli = Path(args.brotli).resolve()
  if not brotli.is_file():
    parser.error(f"{brotli} is not a file")
  SymlinkOutputRegressionTest.brotli = brotli

  unittest.main(argv=[__file__], verbosity=2 if args.verbose else 1)


if __name__ == "__main__":
  main()
