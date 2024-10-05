# Copyright 2015 The Brotli Authors. All rights reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

import os
import platform
import re
import unittest
import sys

try:
    from setuptools import Extension
    from setuptools import setup
except:
    from distutils.core import Extension
    from distutils.core import setup
from distutils.command.build_ext import build_ext
from distutils import dep_util
from distutils import log


IS_PYTHON3 = sys.version_info[0] == 3
CIBUILDWHEEL = os.environ.get('CIBUILDWHEEL', '0') == '1'

CURR_DIR = os.path.abspath(os.path.dirname(os.path.realpath(__file__)))


def read_define(path, macro):
  """ Return macro value from the given file. """
  with open(path, 'r') as f:
    for line in f:
      m = re.match('#define\\s{macro}\\s+(.+)'.format(macro=macro), line)
      if m:
        return m.group(1)
  return ''


def get_version():
  """ Return library version string from 'common/version.h' file. """
  version_file_path = os.path.join(CURR_DIR, 'c', 'common', 'version.h')
  major = read_define(version_file_path, 'BROTLI_VERSION_MAJOR')
  minor = read_define(version_file_path, 'BROTLI_VERSION_MINOR')
  patch = read_define(version_file_path, 'BROTLI_VERSION_PATCH')
  if not major or not minor or not patch:
    return ''
  return '{major}.{minor}.{patch}'.format(major=major, minor=minor, patch=patch)


def get_test_suite():
  test_loader = unittest.TestLoader()
  test_suite = test_loader.discover('python', pattern='*_test.py')
  return test_suite


class BuildExt(build_ext):
  def build_extension(self, ext):
    ext_path = self.get_ext_fullpath(ext.name)
    depends = ext.sources + ext.depends
    if not (self.force or dep_util.newer_group(depends, ext_path, 'newer')):
      log.debug("skipping '%s' extension (up-to-date)", ext.name)
      return
    else:
      log.info("building '%s' extension", ext.name)

    if self.compiler.compiler_type == 'mingw32':
      # On Windows Python 2.7, pyconfig.h defines "hypot" as "_hypot",
      # This clashes with GCC's cmath, and causes compilation errors when
      # building under MinGW: http://bugs.python.org/issue11566
      ext.define_macros.append(('_hypot', 'hypot'))

    build_ext.build_extension(self, ext)


NAME = 'Brotli'

VERSION = get_version()

URL = 'https://github.com/google/brotli'

DESCRIPTION = 'Python bindings for the Brotli compression library'

AUTHOR = 'The Brotli Authors'

LICENSE = 'MIT'

PLATFORMS = ['Posix', 'MacOS X', 'Windows']

CLASSIFIERS = [
    'Development Status :: 4 - Beta',
    'Environment :: Console',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: MIT License',
    'Operating System :: MacOS :: MacOS X',
    'Operating System :: Microsoft :: Windows',
    'Operating System :: POSIX :: Linux',
    'Programming Language :: C',
    'Programming Language :: C++',
    'Programming Language :: Python',
    'Programming Language :: Python :: 2',
    'Programming Language :: Python :: 2.7',
    'Programming Language :: Python :: 3',
    'Programming Language :: Python :: 3.3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: Python :: 3.5',
    'Programming Language :: Unix Shell',
    'Topic :: Software Development :: Libraries',
    'Topic :: Software Development :: Libraries :: Python Modules',
    'Topic :: System :: Archiving',
    'Topic :: System :: Archiving :: Compression',
    'Topic :: Text Processing :: Fonts',
    'Topic :: Utilities',
]

PACKAGE_DIR = {'': 'python'}

PY_MODULES = ['brotli']

class VersionedExtension(Extension):
  def __init__(self, *args, **kwargs):
    define_macros = []

    if IS_PYTHON3 and CIBUILDWHEEL:
      kwargs['py_limited_api'] = True
      define_macros.append(('Py_LIMITED_API', '0x03060000'))
    
    if platform.system() == 'Darwin':
      define_macros.append(('OS_MACOSX', '1'))
    
    kwargs['define_macros'] = kwargs.get('define_macros', []) + define_macros
    
    Extension.__init__(self, *args, **kwargs)


EXT_MODULES = [
    VersionedExtension(
        name='_brotli',
        sources=[
            'python/_brotli.c',
            'c/common/constants.c',
            'c/common/context.c',
            'c/common/dictionary.c',
            'c/common/platform.c',
            'c/common/shared_dictionary.c',
            'c/common/transform.c',
            'c/dec/bit_reader.c',
            'c/dec/decode.c',
            'c/dec/huffman.c',
            'c/dec/state.c',
            'c/enc/backward_references.c',
            'c/enc/backward_references_hq.c',
            'c/enc/bit_cost.c',
            'c/enc/block_splitter.c',
            'c/enc/brotli_bit_stream.c',
            'c/enc/cluster.c',
            'c/enc/command.c',
            'c/enc/compound_dictionary.c',
            'c/enc/compress_fragment.c',
            'c/enc/compress_fragment_two_pass.c',
            'c/enc/dictionary_hash.c',
            'c/enc/encode.c',
            'c/enc/encoder_dict.c',
            'c/enc/entropy_encode.c',
            'c/enc/fast_log.c',
            'c/enc/histogram.c',
            'c/enc/literal_cost.c',
            'c/enc/memory.c',
            'c/enc/metablock.c',
            'c/enc/static_dict.c',
            'c/enc/utf8_util.c',
        ],
        depends=[
            'c/common/constants.h',
            'c/common/context.h',
            'c/common/dictionary.h',
            'c/common/platform.h',
            'c/common/shared_dictionary_internal.h',
            'c/common/transform.h',
            'c/common/version.h',
            'c/dec/bit_reader.h',
            'c/dec/huffman.h',
            'c/dec/prefix.h',
            'c/dec/state.h',
            'c/enc/backward_references.h',
            'c/enc/backward_references_hq.h',
            'c/enc/backward_references_inc.h',
            'c/enc/bit_cost.h',
            'c/enc/bit_cost_inc.h',
            'c/enc/block_encoder_inc.h',
            'c/enc/block_splitter.h',
            'c/enc/block_splitter_inc.h',
            'c/enc/brotli_bit_stream.h',
            'c/enc/cluster.h',
            'c/enc/cluster_inc.h',
            'c/enc/command.h',
            'c/enc/compound_dictionary.h',
            'c/enc/compress_fragment.h',
            'c/enc/compress_fragment_two_pass.h',
            'c/enc/dictionary_hash.h',
            'c/enc/encoder_dict.h',
            'c/enc/entropy_encode.h',
            'c/enc/entropy_encode_static.h',
            'c/enc/fast_log.h',
            'c/enc/find_match_length.h',
            'c/enc/hash.h',
            'c/enc/hash_composite_inc.h',
            'c/enc/hash_forgetful_chain_inc.h',
            'c/enc/hash_longest_match64_inc.h',
            'c/enc/hash_longest_match_inc.h',
            'c/enc/hash_longest_match_quickly_inc.h',
            'c/enc/hash_rolling_inc.h',
            'c/enc/hash_to_binary_tree_inc.h',
            'c/enc/histogram.h',
            'c/enc/histogram_inc.h',
            'c/enc/literal_cost.h',
            'c/enc/memory.h',
            'c/enc/metablock.h',
            'c/enc/metablock_inc.h',
            'c/enc/params.h',
            'c/enc/prefix.h',
            'c/enc/quality.h',
            'c/enc/ringbuffer.h',
            'c/enc/static_dict.h',
            'c/enc/static_dict_lut.h',
            'c/enc/utf8_util.h',
            'c/enc/write_bits.h',
        ],
        include_dirs=[
            'c/include',
        ]),
]

TEST_SUITE = 'setup.get_test_suite'

CMD_CLASS = {
    'build_ext': BuildExt,
}

if IS_PYTHON3 and CIBUILDWHEEL:
  from wheel.bdist_wheel import bdist_wheel
  # adopted from:
  # https://github.com/joerick/python-abi3-package-sample/blob/7f05b22b9e0cfb4e60293bc85252e95278a80720/setup.py
  class bdist_wheel_abi3(bdist_wheel):
    def get_tag(self):
      python, abi, plat = super().get_tag()

      if python.startswith("cp"):
        # on CPython, our wheels are abi3 and compatible back to 3.6
        return "cp36", "abi3", plat

      return python, abi, plat
      
  CMD_CLASS["bdist_wheel"] = bdist_wheel_abi3


with open("README.md", "r") as f:
    README = f.read()

setup(
    name=NAME,
    description=DESCRIPTION,
    long_description=README,
    long_description_content_type="text/markdown",
    version=VERSION,
    url=URL,
    author=AUTHOR,
    license=LICENSE,
    platforms=PLATFORMS,
    classifiers=CLASSIFIERS,
    package_dir=PACKAGE_DIR,
    py_modules=PY_MODULES,
    ext_modules=EXT_MODULES,
    test_suite=TEST_SUITE,
    cmdclass=CMD_CLASS)
