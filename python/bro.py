#! /usr/bin/env python
"""bro %s -- compression/decompression utility using the Brotli algorithm."""

from __future__ import print_function
import argparse
import sys
import os
import brotli
import platform


__version__ = '1.0'


BROTLI_MODES = {
    'text': brotli.MODE_TEXT,
    'font': brotli.MODE_FONT
}


def get_binary_stdio(stream):
    """ Return the specified standard input, output or errors stream as a
    'raw' buffer object suitable for reading/writing binary data from/to it.
    """
    assert stream in ['stdin', 'stdout', 'stderr'], "invalid stream name"
    stdio = getattr(sys, stream)
    if sys.version_info[0] < 3:
        if sys.platform == 'win32':
            # set I/O stream binary flag on python2.x (Windows)
            runtime = platform.python_implementation()
            if runtime == "PyPy":
                # the msvcrt trick doesn't work in pypy, so I use fdopen
                mode = "rb" if stream == "stdin" else "wb"
                stdio = os.fdopen(stdio.fileno(), mode, 0)
            else:
                # this works with CPython -- untested on other implementations
                import msvcrt
                msvcrt.setmode(stdio.fileno(), os.O_BINARY)
        return stdio
    else:
        # get 'buffer' attribute to read/write binary data on python3.x
        if hasattr(stdio, 'buffer'):
            return stdio.buffer
        else:
            orig_stdio = getattr(sys, "__%s__" % stream)
            return orig_stdio.buffer


def main():

    parser = argparse.ArgumentParser(
        prog='bro', description="Compression utility using the Brotli algorithm.",
        usage='%(prog)s [-h] [-i FILE] [-o FILE] [-d] [-f] [-m MODE] [-t]')
    parser.add_argument('--version', action='version', version='%(prog)s 1.0')
    parser.add_argument('-i', '--input', metavar='FILE', type=str, dest='infile',
                        help='input file', default=None)
    parser.add_argument('-o', '--output', metavar='FILE', type=str, dest='outfile',
                        help='output file', default=None)
    parser.add_argument('-d', '--decompress', action='store_true',
                        help='decompress input file', default=False)
    parser.add_argument('-f', '--force', action='store_true',
                        help='overwrite existing output file', default=False)
    parser.add_argument('-m', '--mode', metavar="MODE", type=int,
                        choices=[0, 1], default=0,
                        help='the compression mode can be 0 (text) or 1 (font). '
                        'Defaults to text mode.')
    parser.add_argument('-t', '--transform', action='store_true',
                        help='enable encoder transforms.', default=False)

    options = parser.parse_args()

    if options.infile:
        if not os.path.isfile(options.infile):
            parser.error('file "%s" not found' % options.infile)
        with open(options.infile, "rb") as infile:
            data = infile.read()
    else:
        if sys.stdin.isatty():
            # interactive console, just quit
            parser.error('no input')
        infile = get_binary_stdio('stdin')
        data = infile.read()

    if options.outfile:
        if os.path.isfile(options.outfile) and not options.force:
            parser.error('output file exists')
        outfile = open(options.outfile, "wb")
    else:
        outfile = get_binary_stdio('stdout')

    try:
        if options.decompress:
            data = brotli.decompress(data)
        else:
            data = brotli.compress(data, mode=options.mode, enable_transforms=options.transform)
    except brotli.error as e:
        parser.exit(status=1, message='bro: error: %s: %s' % (e, options.infile or 'sys.stdin'))

    outfile.write(data)
    outfile.close()


if __name__ == '__main__':
    main()
