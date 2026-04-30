#!/usr/bin/env python3
# Copyright 2026 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

"""Regression suite for brotli CLI CopyStat behavior.

Usage:
  python3 tests/regression/t01/copystat_regression_test.py /path/to/brotli
"""

import argparse
import contextlib
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest


PLAIN_BYTES = b"A" * 65537
TARGET_BYTES = b"TARGET\n"


@contextlib.contextmanager
def umask(mask):
  previous = os.umask(mask)
  try:
    yield
  finally:
    os.umask(previous)


def file_mode(path):
  return stat.S_IMODE(os.stat(path).st_mode)


def sha256(path):
  h = hashlib.sha256()
  with open(path, "rb") as f:
    for chunk in iter(lambda: f.read(65536), b""):
      h.update(chunk)
  return h.hexdigest()


class CopyStatRegressionTest(unittest.TestCase):
  brotli = None

  def setUp(self):
    self.tmpdir = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmpdir.cleanup)
    self.workdir = Path(self.tmpdir.name)

  def write_plain(self, path):
    path.write_bytes(PLAIN_BYTES)

  def write_target(self, path):
    path.write_bytes(TARGET_BYTES)

  def run_brotli(self, *args, input_bytes=None, env=None, check=True):
    proc = subprocess.run(
        [str(self.brotli)] + [str(arg) for arg in args],
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False)
    if check and proc.returncode != 0:
      self.fail(
          "brotli exited with %d\nstdout:\n%s\nstderr:\n%s" % (
              proc.returncode,
              proc.stdout.decode("utf-8", "replace"),
              proc.stderr.decode("utf-8", "replace")))
    return proc

  def compress(self, src, dst, no_copy_stat=False):
    args = ["-f", "-k"]
    if no_copy_stat:
      args.append("-n")
    args.extend([src, "-o", dst])
    return self.run_brotli(*args)

  def decompress(self, src, dst, no_copy_stat=False):
    args = ["-d", "-f"]
    if no_copy_stat:
      args.append("-n")
    args.extend([src, "-o", dst])
    return self.run_brotli(*args)

  def test_modes_propagate_on_compress_and_decompress(self):
    modes = [0o600, 0o640, 0o644, 0o664, 0o660,
             0o400, 0o444, 0o755, 0o700, 0o604]

    with umask(0o022):
      for mode in modes:
        with self.subTest(mode="%03o" % mode):
          case_dir = self.workdir / ("%03o" % mode)
          case_dir.mkdir()
          src = case_dir / "in.bin"
          compressed = case_dir / "in.bin.br"
          decoded = case_dir / "out.bin"

          self.write_plain(src)
          os.chmod(src, mode)

          self.compress(src, compressed)
          self.decompress(compressed, decoded)

          self.assertEqual(file_mode(compressed), mode)
          self.assertEqual(file_mode(decoded), mode)
          self.assertEqual(decoded.read_bytes(), PLAIN_BYTES)

  def test_special_mode_bits_are_masked(self):
    with umask(0o022):
      src = self.workdir / "in.bin"
      compressed = self.workdir / "in.bin.br"
      self.write_plain(src)

      os.chmod(src, 0o3644)
      actual_input_mode = file_mode(src)
      if actual_input_mode == 0o644:
        self.skipTest("filesystem stripped special mode bits from input")

      self.compress(src, compressed)
      compressed_mode = file_mode(compressed)

      self.assertEqual(compressed_mode & 0o7000, 0)
      self.assertEqual(compressed_mode & 0o777, actual_input_mode & 0o777)

  def test_no_copy_stat_flag_suppresses_mode_copy(self):
    with umask(0o022):
      src = self.workdir / "in.bin"
      without_copy = self.workdir / "without_copy.br"
      with_copy = self.workdir / "with_copy.br"
      self.write_plain(src)
      os.chmod(src, 0o606)

      self.compress(src, without_copy, no_copy_stat=True)
      self.compress(src, with_copy)

      self.assertNotEqual(file_mode(without_copy), 0o606)
      self.assertEqual(file_mode(with_copy), 0o606)

  def test_timestamp_is_copied(self):
    with umask(0o022):
      src = self.workdir / "in.bin"
      compressed = self.workdir / "in.bin.br"
      decoded = self.workdir / "out.bin"
      self.write_plain(src)

      expected_mtime = 1577934245
      os.utime(src, (expected_mtime, expected_mtime))

      self.compress(src, compressed)
      self.decompress(compressed, decoded)

      self.assertEqual(int(os.stat(compressed).st_mtime), expected_mtime)
      self.assertEqual(int(os.stat(decoded).st_mtime), expected_mtime)

  def test_stdin_input_has_no_path_to_copy_from(self):
    payload = b"stdin -> compressed file\n"
    compressed = self.run_brotli("-c", input_bytes=payload).stdout

    decoded = self.workdir / "decoded.txt"
    self.run_brotli("-d", "-o", decoded, input_bytes=compressed)

    self.assertEqual(decoded.read_bytes(), payload)
    self.assertEqual(file_mode(decoded) & ~0o666, 0)

  def test_stdout_path_skips_copystat(self):
    payload = b"hello brotli stdout pipeline\n"

    compressed = self.run_brotli("-c", input_bytes=payload).stdout
    decoded = self.run_brotli("-d", "-c", input_bytes=compressed).stdout

    self.assertEqual(decoded, payload)

  def test_content_roundtrip(self):
    cases = {
        "small-ascii": b"hello brotli\n",
        "empty": b"",
        "plain-65537": PLAIN_BYTES,
        "aligned-65536": b"\0" * 65536,
        "deterministic-binary": bytes((i * 73 + 41) % 256
                                      for i in range(12288)),
    }

    with umask(0o022):
      for label, data in cases.items():
        with self.subTest(label=label):
          case_dir = self.workdir / label
          case_dir.mkdir()
          src = case_dir / "in.bin"
          compressed = case_dir / "in.bin.br"
          decoded = case_dir / "out.bin"
          src.write_bytes(data)
          os.chmod(src, 0o644)

          self.compress(src, compressed)
          if data:
            self.assertGreater(compressed.stat().st_size, 0)
          self.decompress(compressed, decoded)

          self.assertEqual(decoded.read_bytes(), data)

  def test_fclose_swap_does_not_redirect_copystat(self):
    compressed, out_path, target_path = self.prepare_attack_files()
    pre_hash = sha256(target_path)

    self.run_brotli_with_swap(compressed, out_path, target_path)

    self.assertTrue(out_path.is_symlink())
    self.assertEqual(file_mode(target_path), 0o600)
    self.assertEqual(sha256(target_path), pre_hash)

  def test_fclose_swap_with_no_copy_stat_leaves_target_unchanged(self):
    compressed, out_path, target_path = self.prepare_attack_files()
    pre_hash = sha256(target_path)

    self.run_brotli_with_swap(compressed, out_path, target_path,
                              no_copy_stat=True)

    self.assertTrue(out_path.is_symlink())
    self.assertEqual(file_mode(target_path), 0o600)
    self.assertEqual(sha256(target_path), pre_hash)

  def prepare_attack_files(self):
    src = self.workdir / "plain.bin"
    compressed = self.workdir / "plain.bin.br"
    out_path = self.workdir / "out"
    target_path = self.workdir / "target.txt"

    self.write_plain(src)
    self.write_target(target_path)
    os.chmod(src, 0o644)
    os.chmod(target_path, 0o600)
    self.compress(src, compressed)
    os.chmod(compressed, 0o644)

    return compressed, out_path, target_path

  def run_brotli_with_swap(self, compressed, out_path, target_path,
                           no_copy_stat=False):
    env = dict(os.environ)
    env["BROTLI_SWAP_OUTPUT_ABS"] = str(out_path)
    env["BROTLI_SWAP_TARGET_ABS"] = str(target_path)
    env["LD_PRELOAD"] = self.ld_preload_value()

    args = ["-d", "-f"]
    if no_copy_stat:
      args.append("-n")
    args.extend(["-o", out_path, compressed])

    proc = self.run_brotli(*args, env=env, check=False)
    if proc.returncode != 0:
      self.fail(
          "brotli with fclose_swap exited with %d\nstdout:\n%s\nstderr:\n%s" % (
              proc.returncode,
              proc.stdout.decode("utf-8", "replace"),
              proc.stderr.decode("utf-8", "replace")))

  def ld_preload_value(self):
    preloads = []
    asan_runtime = self.asan_runtime()
    if asan_runtime:
      preloads.append(asan_runtime)
    preloads.append(str(self.swap_helper()))
    existing = os.environ.get("LD_PRELOAD")
    if existing:
      preloads.append(existing)
    return ":".join(preloads)

  def swap_helper(self):
    if not Path("/proc/self/fd").is_dir():
      self.skipTest("fclose_swap helper requires /proc/self/fd")
    cc = shutil.which("cc")
    if cc is None:
      self.skipTest("cc is required to build fclose_swap.so")

    helper_src = Path(__file__).with_name("fclose_swap.c")
    helper_out = self.workdir / "libfclose_swap.so"
    proc = subprocess.run(
        [cc, "-shared", "-fPIC", str(helper_src), "-o", str(helper_out),
         "-ldl"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False)
    if proc.returncode != 0:
      self.fail(
          "failed to build fclose_swap.so\nstdout:\n%s\nstderr:\n%s" % (
              proc.stdout.decode("utf-8", "replace"),
              proc.stderr.decode("utf-8", "replace")))
    return helper_out

  def asan_runtime(self):
    try:
      proc = subprocess.run(["ldd", str(self.brotli)],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            check=False)
    except FileNotFoundError:
      return None
    if b"libasan" not in proc.stdout + proc.stderr:
      return None

    cc = shutil.which("cc")
    if cc is None:
      return None
    proc = subprocess.run([cc, "-print-file-name=libasan.so"],
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,
                          text=True,
                          check=False)
    candidate = proc.stdout.strip()
    if candidate and Path(candidate).is_file():
      return candidate
    return None


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("brotli", help="path to the brotli CLI binary to test")
  parser.add_argument("-v", "--verbose", action="store_true")
  args = parser.parse_args()

  brotli = Path(args.brotli).resolve()
  if not brotli.is_file():
    parser.error("%s is not a file" % brotli)
  CopyStatRegressionTest.brotli = brotli

  unittest.main(argv=[__file__], verbosity=2 if args.verbose else 1)


if __name__ == "__main__":
  main()
