from __future__ import print_function
import sys
import os
import sysconfig
import filecmp


def diff_q(first_file, second_file):
    """Simulate call to POSIX diff with -q argument"""
    if not filecmp.cmp(first_file, second_file, shallow=False):
        print("Files %s and %s differ" % (first_file, second_file),
              file=sys.stderr)
        return 1
    return 0


PYTHON = sys.executable or "python"

# 'bro.py' script should be in parent directory
BRO = os.path.abspath("../bro.py")

# get platform- and version-specific build/lib folder
platform_lib_name = "lib.{platform}-{version[0]}.{version[1]}".format(
    platform=sysconfig.get_platform(),
    version=sys.version_info)

# by default, distutils' build base is in the same location as setup.py
build_base = os.path.abspath(os.path.join("..", "..", "build"))
build_lib = os.path.join(build_base, platform_lib_name)

# prepend build/lib to PYTHONPATH environment variable
TEST_ENV = os.environ.copy()
if 'PYTHONPATH' not in TEST_ENV:
    TEST_ENV['PYTHONPATH'] = build_lib
else:
    TEST_ENV['PYTHONPATH'] = build_lib + os.pathsep + TEST_ENV['PYTHONPATH']
