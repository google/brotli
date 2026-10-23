#!/usr/bin/env python3
# Copyright 2026 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""Regression suite for brotli CLI symlink output overwrite prevention (Issue #1462).

Validates that OpenOutputFile() does not follow symlinks when creating or
opening output files, preventing arbitrary file overwrite and truncation
even when -f / --force is specified.

Usage:
  python3 tests/regression/t02/symlink_regression_test.py /path/to/brotli
"""

import argparse
import hashlib
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

PLAIN_BYTES = b"The quick brown fox jumps over the lazy dog.\n" * 100
TARGET_BYTES = b"CRITICAL_TARGET_CONTENT_DO_NOT_OVERWRITE\n"


def sha256(path):
  h = hashlib.sha256()
  with open(path, "rb") as f:
    for chunk in iter(lambda: f.read(65536), b""):
      h.update(chunk)
  return h.hexdigest()


def file_mode(path):
  return stat.S_IMODE(os.stat(path).st_mode)


class SymlinkOutputRegressionTest(unittest.TestCase):
  brotli = None

  def setUp(self):
    self.tmpdir = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmpdir.cleanup)
    self.workdir = Path(self.tmpdir.name)

  def on_subprocess_failed(self, preamble: str, proc):
    retcode = proc.returncode
    stdout = proc.stdout.decode("utf-8", "replace")
    stderr = proc.stderr.decode("utf-8", "replace")
    self.fail(
        f"{preamble}\n"
        f"retcode:{retcode}\n"
        f"stdout:\n{stdout}\n"
        f"stderr:\n{stderr}\n")

  def run_brotli(self, *args, input_bytes=None, env=None, check=True):
    proc = subprocess.run(
        [str(self.brotli)] + [str(arg) for arg in args],
        input=input_bytes,
        capture_output=True,
        env=env,
        check=False)
    if check and proc.returncode != 0:
      self.on_subprocess_failed("brotli failed", proc)
    return proc

  def prepare_test_files(self):
    src = self.workdir / "input.txt"
    src.write_bytes(PLAIN_BYTES)
    target = self.workdir / "target.txt"
    target.write_bytes(TARGET_BYTES)
    os.chmod(target, 0o600)

    compressed_src = self.workdir / "input.txt.br"
    self.run_brotli("-f", "-k", str(src), "-o", str(compressed_src))
    return src, compressed_src, target

  def test_symlink_output_with_force_rejected_compress(self):
    src, _, target = self.prepare_test_files()
    target_hash = sha256(target)
    out_link = self.workdir / "out.br"
    out_link.symlink_to(target)

    proc = self.run_brotli("-f", "-k", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli should fail when output path is a symlink")
    self.assertTrue(out_link.is_symlink(), "output should still be a symlink")
    self.assertEqual(sha256(target), target_hash,
                     "target file must remain unchanged")
    if os.name != "nt":
      self.assertEqual(file_mode(target), 0o600,
                       "target file mode must remain unchanged")

  def test_symlink_output_with_force_rejected_decompress(self):
    _, compressed_src, target = self.prepare_test_files()
    target_hash = sha256(target)
    out_link = self.workdir / "out.txt"
    out_link.symlink_to(target)

    proc = self.run_brotli("-d", "-f", str(compressed_src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli -d -f should fail when output path is a symlink")
    self.assertTrue(out_link.is_symlink(), "output should still be a symlink")
    self.assertEqual(sha256(target), target_hash,
                     "target file must remain unchanged")
    if os.name != "nt":
      self.assertEqual(file_mode(target), 0o600,
                       "target file mode must remain unchanged")

  def test_symlink_output_without_force_rejected(self):
    src, _, target = self.prepare_test_files()
    target_hash = sha256(target)
    out_link = self.workdir / "out.br"
    out_link.symlink_to(target)

    proc = self.run_brotli("-k", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli should fail when output path is a pre-existing symlink without -f")
    self.assertTrue(out_link.is_symlink(), "output should still be a symlink")
    self.assertEqual(sha256(target), target_hash,
                     "target file must remain unchanged")

  def test_dangling_symlink_output_with_force_rejected(self):
    src, _, _ = self.prepare_test_files()
    nonexistent = self.workdir / "nonexistent_target.txt"
    out_link = self.workdir / "dangling.br"
    out_link.symlink_to(nonexistent)

    proc = self.run_brotli("-f", "-k", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli -f should fail on dangling symlink output")
    self.assertFalse(nonexistent.exists(),
                     "dangling symlink target must not be created")
    self.assertTrue(out_link.is_symlink(), "output should remain a symlink")

  def test_dangling_symlink_output_without_force_rejected(self):
    src, _, _ = self.prepare_test_files()
    nonexistent = self.workdir / "nonexistent_target.txt"
    out_link = self.workdir / "dangling.br"
    out_link.symlink_to(nonexistent)

    proc = self.run_brotli("-k", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli should fail on dangling symlink output without -f")
    self.assertFalse(nonexistent.exists(),
                     "dangling symlink target must not be created")

  def test_default_output_name_symlink_rejected_compress(self):
    src, _, target = self.prepare_test_files()
    target_hash = sha256(target)
    # Default output for 'src' (input.txt) is input.txt.br
    default_out = self.workdir / "default_test.txt"
    default_out.write_bytes(PLAIN_BYTES)
    default_link = self.workdir / "default_test.txt.br"
    default_link.symlink_to(target)

    proc = self.run_brotli("-f", "-k", str(default_out), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli -f should fail when default output path is a symlink")
    self.assertTrue(default_link.is_symlink())
    self.assertEqual(sha256(target), target_hash)

  def test_default_output_name_symlink_rejected_decompress(self):
    _, compressed_src, target = self.prepare_test_files()
    target_hash = sha256(target)
    # Default decompressed output for 'in2.txt.br' is in2.txt
    compressed_in = self.workdir / "in2.txt.br"
    compressed_in.write_bytes(compressed_src.read_bytes())
    default_link = self.workdir / "in2.txt"
    default_link.symlink_to(target)

    proc = self.run_brotli("-d", "-f", str(compressed_in), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli -d -f should fail when default output path is a symlink")
    self.assertTrue(default_link.is_symlink())
    self.assertEqual(sha256(target), target_hash)

  def test_regular_file_overwrite_with_force_succeeds(self):
    src, _, _ = self.prepare_test_files()
    reg_out = self.workdir / "regular.br"
    reg_out.write_bytes(b"OLD_CONTENT")

    proc = self.run_brotli("-f", "-k", str(src), "-o", str(reg_out), check=False)
    self.assertEqual(proc.returncode, 0,
                     "brotli -f should successfully overwrite regular file")

    decompressed = self.workdir / "decompressed.txt"
    self.run_brotli("-d", "-f", str(reg_out), "-o", str(decompressed))
    self.assertEqual(decompressed.read_bytes(), PLAIN_BYTES)

  def test_regular_file_without_force_fails(self):
    src, _, _ = self.prepare_test_files()
    reg_out = self.workdir / "regular.br"
    old_content = b"OLD_CONTENT"
    reg_out.write_bytes(old_content)

    proc = self.run_brotli("-k", str(src), "-o", str(reg_out), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli without -f must fail if output file exists")
    self.assertEqual(reg_out.read_bytes(), old_content)

  def test_symlink_in_directory_path_component_allowed(self):
    src, _, _ = self.prepare_test_files()
    real_dir = self.workdir / "real_dir"
    real_dir.mkdir()
    sym_dir = self.workdir / "sym_dir"
    sym_dir.symlink_to(real_dir)

    out_file = sym_dir / "out.br"
    proc = self.run_brotli("-f", "-k", str(src), "-o", str(out_file), check=False)
    self.assertEqual(proc.returncode, 0,
                     "brotli should succeed when symlink is in directory path, not leaf file")
    self.assertTrue(out_file.is_file())
    self.assertFalse(out_file.is_symlink())

    decompressed = self.workdir / "decompressed2.txt"
    self.run_brotli("-d", "-f", str(out_file), "-o", str(decompressed))
    self.assertEqual(decompressed.read_bytes(), PLAIN_BYTES)

  def test_symlink_output_with_rm_flag_preserves_input(self):
    src, _, target = self.prepare_test_files()
    target_hash = sha256(target)
    out_link = self.workdir / "out_rm.br"
    out_link.symlink_to(target)

    # When --rm / -j is passed and opening output symlink fails,
    # the input file must NOT be deleted.
    proc = self.run_brotli("-f", "--rm", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli should fail when output path is a symlink")
    self.assertTrue(src.exists(),
                    "source input file must NOT be removed when output open fails")
    self.assertEqual(src.read_bytes(), PLAIN_BYTES)
    self.assertTrue(out_link.is_symlink())
    self.assertEqual(sha256(target), target_hash)

  def test_symlink_chain_output_rejected(self):
    src, _, target = self.prepare_test_files()
    target_hash = sha256(target)
    mid_link = self.workdir / "mid_link.br"
    mid_link.symlink_to(target)
    chain_link = self.workdir / "chain_link.br"
    chain_link.symlink_to(mid_link)

    proc = self.run_brotli("-f", "-k", str(src), "-o", str(chain_link), check=False)
    self.assertNotEqual(proc.returncode, 0,
                        "brotli -f should fail when output is a symlink chain")
    self.assertTrue(chain_link.is_symlink())
    self.assertTrue(mid_link.is_symlink())
    self.assertEqual(sha256(target), target_hash)

  def test_symlink_output_error_message(self):
    src, _, target = self.prepare_test_files()
    out_link = self.workdir / "out_err.br"
    out_link.symlink_to(target)

    proc = self.run_brotli("-f", "-k", str(src), "-o", str(out_link), check=False)
    self.assertNotEqual(proc.returncode, 0)
    stderr = proc.stderr.decode("utf-8", "replace")
    self.assertIn("failed to open output file", stderr,
                  "stderr must report failed to open output file")
    self.assertIn(out_link.name, stderr,
                  "stderr must report the failing output filename")


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
