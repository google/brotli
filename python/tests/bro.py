#! /usr/bin/env python
"""bro %s -- compression/decompression utility using the Brotli algorithm."""

from __future__ import print_function
import getopt
import sys
import os
import brotli

__usage__ = """\
Usage: bro [--force] [--decompress] [--input filename] [--output filename]
    [--mode 'text'|'font'] [--transform]"""

__version__ = '0.1'


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


def main(args):

    options = parse_options(args)

    if options.infile:
        if not os.path.isfile(options.infile):
            print('file "%s" not found' % options.infile, file=sys.stderr)
            sys.exit(1)
        with open(options.infile, "rb") as infile:
            data = infile.read()
    else:
        if sys.stdin.isatty():
            # interactive console, just quit
            usage()
        infile = get_binary_stdio('stdin')
        data = infile.read()

    if options.outfile:
        if os.path.isfile(options.outfile) and not options.force:
            print('output file exists')
            sys.exit(1)
        outfile = open(options.outfile, "wb")
    else:
        outfile = get_binary_stdio('stdout')

    try:
        if options.decompress:
            data = brotli.decompress(data)
        else:
            data = brotli.compress(data, options.mode, options.transform)
    except brotli.error as e:
        print('[ERROR] %s: %s' % (e, options.infile or 'sys.stdin'),
              file=sys.stderr)
        sys.exit(1)

    outfile.write(data)
    outfile.close()


def parse_options(args):
    try:
        raw_options, dummy = getopt.gnu_getopt(
            args, "?hdi:o:fm:t",
            ["help", "decompress", "input=", "output=", "force", "mode=",
             "transform"])
    except getopt.GetoptError as e:
        print(e, file=sys.stderr)
        usage()
    options = Options(raw_options)
    return options


def usage():
    print(__usage__, file=sys.stderr)
    sys.exit(1)


class Options(object):

    def __init__(self, raw_options):
        self.decompress = self.force = self.transform = False
        self.infile = self.outfile = None
        self.mode = BROTLI_MODES['text']
        for option, value in raw_options:
            if option in ("-h", "--help"):
                print(__doc__ % (__version__))
                print("\n%s" % __usage__)
                sys.exit(0)
            elif option in ('-d', '--decompress'):
                self.decompress = True
            elif option in ('-i', '--input'):
                self.infile = value
            elif option in ('-o', '--output'):
                self.outfile = value
            elif option in ('-f', '--force'):
                self.force = True
            elif option in ('-m', '--mode'):
                value = value.lower()
                if value not in ('text', 'font'):
                    print('mode "%s" not recognized' % value, file=sys.stderr)
                    usage()
                self.mode = BROTLI_MODES[value]
            elif option in ('-t', '--transform'):
                self.transform = True


if __name__ == '__main__':
    main(sys.argv[1:])
