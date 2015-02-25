#! /usr/bin/env python
"""bro %s -- compression/decompression utility using the Brotli algorithm."""

from __future__ import print_function
import brotli
import getopt
import sys
import os


__usage__ = """\
Usage: bro [--force] [--decompress] [--input filename] [--output filename]
    [--mode 'text'|'font'] [--transform] [--bufsize]"""

__version__ = '0.1'


BROTLI_MODES = {
    'text': brotli.MODE_TEXT,
    'font': brotli.MODE_FONT
}


def get_binary_stdout():
    if sys.version_info[0] < 3:
        if sys.platform == 'win32':
            # set stdout binary flag on python2.x (Windows)
            import msvcrt
            msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
        return sys.stdout
    else:
        # use 'buffer' attribute to write binary to stdout on python3.x
        if hasattr(sys.stdout, 'buffer'):
            return sys.stdout.buffer
        else:
            return sys.__stdout__.buffer


def get_binary_stdin():
    if sys.version_info[0] < 3:
        if sys.platform == 'win32':
            # set stdin binary flag on python2.x (Windows)
            import msvcrt
            msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
        return sys.stdin
    else:
        # use 'buffer' attribute to read binary from stdin on python3.x
        if hasattr(sys.stdin, 'buffer'):
            return sys.stdin.buffer
        else:
            return sys.__stdin__.buffer


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
        infile = get_binary_stdin()
        data = infile.read()

    if options.outfile:
        if os.path.isfile(options.outfile) and not options.force:
            print('output file exists')
            sys.exit(1)
        outfile = open(options.outfile, "wb")
    else:
        outfile = get_binary_stdout()

    try:
        if options.decompress:
            bufsize = options.bufsize or 10*len(data)
            try:
                data = brotli.decompress(data, bufsize)
            except brotli.error:
                raise
        else:
            data = brotli.compress(data, options.mode, options.transform)
            if outfile.isatty():
                # print compressed binary data as hex strings
                from binascii import hexlify
                data = hexlify(data)
    except brotli.error as e:
        print('[ERROR] %s: %s' % (e, options.infile or 'sys.stdin'),
              file=sys.stderr)
        sys.exit(1)

    outfile.write(data)
    outfile.close()


def parse_options(args):
    try:
        raw_options, dummy = getopt.gnu_getopt(
            args, "?hdi:o:fm:tb:",
            ["help", "decompress", "input=", "output=", "force", "mode=",
             "transform", "bufsize="])
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
        self.bufsize = None
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
            elif option in ('-b', '--bufsize'):
                self.bufsize = int(value)

if __name__ == '__main__':
    main(sys.argv[1:])
