"""Common utilities for Brotli tests."""

from __future__ import print_function
import glob
import os
import pathlib
import sys
import sysconfig

project_dir = str(pathlib.PurePath(__file__).parent.parent.parent)
runtime_dir = os.getenv('TEST_SRCDIR')
test_dir = os.getenv('BROTLI_TESTS_PATH')

# Fallbacks
if test_dir and runtime_dir:
  test_dir = os.path.join(runtime_dir, test_dir)
elif test_dir is None:
  test_dir = os.path.join(project_dir, 'tests')

# Get the platform/version-specific build folder.
# By default, the distutils build base is in the same location as setup.py.
platform_lib_name = 'lib.{platform}-{version[0]}.{version[1]}'.format(
    platform=sysconfig.get_platform(), version=sys.version_info
)
build_dir = os.path.join(project_dir, 'bin', platform_lib_name)

# Prepend the build folder to sys.path and the PYTHONPATH environment variable.
if build_dir not in sys.path:
  sys.path.insert(0, build_dir)
TEST_ENV = dict(os.environ)
if 'PYTHONPATH' not in TEST_ENV:
  TEST_ENV['PYTHONPATH'] = build_dir
else:
  TEST_ENV['PYTHONPATH'] = build_dir + os.pathsep + TEST_ENV['PYTHONPATH']

TESTDATA_DIR = os.path.join(test_dir, 'testdata')


def gather_text_inputs():
  """Discover inputs for decompression tests."""
  all_inputs = [
      'empty',  # Empty file
      '10x10y',  # Small text
      'alice29.txt',  # Large text
      'random_org_10k.bin',  # Small data
      'mapsdatazrh',  # Large data
      'ukkonooa',  # Poem
      'cp1251-utf16le',  # Codepage 1251 table saved in UTF16-LE encoding
      'cp852-utf8',  # Codepage 852 table saved in UTF8 encoding
      # TODO(eustas): add test on already compressed content
  ]
  # Filter out non-existing files; e.g. in lightweight sources pack.
  return [
      f for f in all_inputs if os.path.isfile(os.path.join(TESTDATA_DIR, f))
  ]


def gather_compressed_inputs():
  """Discover inputs for compression tests."""
  candidates = glob.glob(os.path.join(TESTDATA_DIR, '*.compressed'))
  pairs = [(f, f.split('.compressed')[0]) for f in candidates]
  existing = [
      pair
      for pair in pairs
      if os.path.isfile(pair[0]) and os.path.isfile(pair[1])
  ]
  return [
      (os.path.basename(pair[0]), (os.path.basename(pair[1])))
      for pair in existing
  ]


def take_input(input_name):
  with open(os.path.join(TESTDATA_DIR, input_name), 'rb') as f:
    return f.read()


def has_input(input_name):
  return os.path.isfile(os.path.join(TESTDATA_DIR, input_name))


def chunk_input(data, chunk_size):
  return [data[i:i + chunk_size] for i in range(0, len(data), chunk_size)]
