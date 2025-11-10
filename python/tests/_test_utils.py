"""Common utilities for Brotli tests."""

from __future__ import print_function
import filecmp
import glob
import itertools
import os
import pathlib
import sys
import sysconfig
import tempfile
import unittest

project_dir = str(pathlib.PurePath(__file__).parent.parent.parent)
test_dir = os.getenv('BROTLI_TESTS_PATH')
BRO_ARGS = [os.getenv('BROTLI_WRAPPER')]

# Fallbacks
if test_dir is None:
  test_dir = os.path.join(project_dir, 'tests')
if BRO_ARGS[0] is None:
  python_exe = sys.executable or 'python'
  bro_path = os.path.join(project_dir, 'python', 'bro.py')
  BRO_ARGS = [python_exe, bro_path]

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

TESTDATA_FILES = [
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

# Some files might be missing in a lightweight sources pack.
TESTDATA_PATH_CANDIDATES = [
    os.path.join(TESTDATA_DIR, f) for f in TESTDATA_FILES
]

TESTDATA_PATHS = [
    path for path in TESTDATA_PATH_CANDIDATES if os.path.isfile(path)
]

TESTDATA_PATHS_FOR_DECOMPRESSION = glob.glob(
    os.path.join(TESTDATA_DIR, '*.compressed')
)

TEMP_DIR = tempfile.mkdtemp()


def get_temp_compressed_name(filename):
  return os.path.join(TEMP_DIR, os.path.basename(filename + '.bro'))


def get_temp_uncompressed_name(filename):
  return os.path.join(TEMP_DIR, os.path.basename(filename + '.unbro'))


def bind_method_args(method, *args, **kwargs):
  return lambda self: method(self, *args, **kwargs)


# TODO(eustas): migrate to absl.testing.parameterized.
def generate_test_methods(
    test_case_class, for_decompression=False, variants=None
):
  """Adds test methods for each test data file and each variant.

  This makes identifying problems with specific compression scenarios easier.

  Args:
    test_case_class: The test class to add methods to.
    for_decompression: If True, uses compressed test data files.
    variants: A dictionary where keys are option names and values are lists of
      possible values for that option. Each combination of variants will
      generate a separate test method.
  """
  if for_decompression:
    paths = [
        path for path in TESTDATA_PATHS_FOR_DECOMPRESSION
        if os.path.exists(path.replace('.compressed', ''))
    ]
  else:
    paths = TESTDATA_PATHS
  opts = []
  if variants:
    opts_list = []
    for k, v in variants.items():
      opts_list.append([r for r in itertools.product([k], v)])
    for o in itertools.product(*opts_list):
      opts_name = '_'.join([str(i) for i in itertools.chain(*o)])
      opts_dict = dict(o)
      opts.append([opts_name, opts_dict])
  else:
    opts.append(['', {}])
  for method in [m for m in dir(test_case_class) if m.startswith('_test')]:
    for testdata in paths:
      for opts_name, opts_dict in opts:
        f = os.path.splitext(os.path.basename(testdata))[0]
        name = 'test_{method}_{options}_{file}'.format(
            method=method, options=opts_name, file=f
        )
        func = bind_method_args(
            getattr(test_case_class, method), testdata, **opts_dict
        )
        setattr(test_case_class, name, func)


class TestCase(unittest.TestCase):
  """Base class for Brotli test cases.

  Provides common setup and teardown logic, including cleaning up temporary
  files and a utility for comparing file contents.
  """

  def tearDown(self):
    for f in TESTDATA_PATHS:
      try:
        os.unlink(get_temp_compressed_name(f))
      except OSError:
        pass
      try:
        os.unlink(get_temp_uncompressed_name(f))
      except OSError:
        pass
    # super().tearDown()  # Requires Py3+

  def assert_files_match(self, first, second):
    self.assertTrue(
        filecmp.cmp(first, second, shallow=False),
        'File {} differs from {}'.format(first, second),
    )
