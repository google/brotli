from __future__ import print_function
import sys
import os
import sysconfig
import filecmp


def diff_q(first_file, second_file):
    """Simulate call to POSIX diff with -q argument"""
    if not filecmp.cmp(first_file, second_file, shallow=False):
        print(
            'Files %s and %s differ' % (first_file, second_file),
            file=sys.stderr)
        return 1
    return 0


project_dir = os.path.abspath(os.path.join(__file__, '..', '..', '..'))

PYTHON = sys.executable or 'python'

BRO = os.path.join(project_dir, 'python', 'bro.py')

TESTDATA_DIR = os.path.join(project_dir, 'tests', 'testdata')

# Get the platform/version-specific build folder.
# By default, the distutils build base is in the same location as setup.py.
platform_lib_name = 'lib.{platform}-{version[0]}.{version[1]}'.format(
    platform=sysconfig.get_platform(), version=sys.version_info)
build_dir = os.path.join(project_dir, 'bin', platform_lib_name)

# Prepend the build folder to sys.path and the PYTHONPATH environment variable.
if build_dir not in sys.path:
    sys.path.insert(0, build_dir)
TEST_ENV = os.environ.copy()
if 'PYTHONPATH' not in TEST_ENV:
    TEST_ENV['PYTHONPATH'] = build_dir
else:
    TEST_ENV['PYTHONPATH'] = build_dir + os.pathsep + TEST_ENV['PYTHONPATH']
