"""Functions that read and write brotli files.

The user of the file doesn't have to worry about the compression,
but random access is not allowed."""

# forked from CPython 3.8.1 gzip.py which is
# based on Andrew Kuchling's minigzip.py distributed with the zlib module

import os
import builtins
import io
import _compression
import brotli

__all__ = ["BrotliFile", "open"]

READ, WRITE = 1, 2


def open(filename, mode="rb", quality=11, lgwin=22, lgblock=0,
         encoding=None, errors=None, newline=None):
    """Open a brotli-compressed file in binary or text mode.

    The filename argument can be an actual filename (a str or bytes object), or
    an existing file object to read from or write to.

    The mode argument can be "r", "rb", "w", "wb", "x", "xb", "a" or "ab" for
    binary mode, or "rt", "wt", "xt" or "at" for text mode. The default mode is
    "rb", and the default compresslevel is 9.

    For binary mode, this function is equivalent to the BrotliFile constructor:
    BrotliFile(filename, mode, compresslevel). In this case, the encoding,
    errors and newline arguments must not be provided.

    For text mode, a BrotliFile object is created, and wrapped in an
    io.TextIOWrapper instance with the specified encoding, error handling
    behavior, and line ending(s).
    """
    if "t" in mode:
        if "b" in mode:
            raise ValueError("Invalid mode: %r" % (mode,))
    else:
        if encoding is not None:
            raise ValueError("Argument 'encoding' not supported in binary mode")
        if errors is not None:
            raise ValueError("Argument 'errors' not supported in binary mode")
        if newline is not None:
            raise ValueError("Argument 'newline' not supported in binary mode")

    gz_mode = mode.replace("t", "")
    if isinstance(filename, (str, bytes, os.PathLike)):
        binary_file = BrotliFile(filename, gz_mode, quality, lgwin, lgblock)
    elif hasattr(filename, "read") or hasattr(filename, "write"):
        binary_file = BrotliFile(
            None, gz_mode, quality, lgwin, lgblock, filename)
    else:
        raise TypeError("filename must be a str or bytes object, or a file")

    if "t" in mode:
        return io.TextIOWrapper(binary_file, encoding, errors, newline)
    else:
        return binary_file


class BrotliFile(_compression.BaseStream):
    """The BrotliFile class simulates most of the methods of a file object with
    the exception of the truncate() method.

    This class only supports opening files in binary mode. If you need to open
    a compressed file in text mode, use the brotli.open() function.
    """

    # Overridden with internal file object to be closed, if only a filename
    # is passed in
    myfileobj = None

    def __init__(self, filename=None, mode=None,
                 quality=11, lgwin=22, lgblock=0,
                 fileobj=None):
        """Constructor for the BrotliFile class.

        At least one of fileobj and filename must be given a
        non-trivial value.

        The new class instance is based on fileobj, which can be a regular
        file, an io.BytesIO object, or any other object which simulates a file.
        It defaults to None, in which case filename is opened to provide
        a file object.

        The mode argument can be any of 'r', 'rb', 'a', 'ab', 'w', 'wb', 'x',
        or 'xb' depending on whether the file will be read or written.  The
        default is the mode of fileobj if discernible; otherwise, the default
        is 'rb'. A mode of 'r' is equivalent to one of 'rb', and similarly for
        'w' and 'wb', 'a' and 'ab', and 'x' and 'xb'.
        """

        if mode and ('t' in mode or 'U' in mode):
            raise ValueError("Invalid mode: {!r}".format(mode))
        if mode and 'b' not in mode:
            mode += 'b'
        if fileobj is None:
            fileobj = self.myfileobj = builtins.open(filename, mode or 'rb')
        if filename is None:
            filename = getattr(fileobj, 'name', '')
            if not isinstance(filename, (str, bytes)):
                filename = ''
        else:
            filename = os.fspath(filename)
        if mode is None:
            mode = getattr(fileobj, 'mode', 'rb')

        if mode.startswith('r'):
            self.mode = READ
            raw = _BrotliReader(fileobj, _BrotliDecompressor)
            self._buffer = io.BufferedReader(raw)
            self.name = filename

        elif mode.startswith(('w', 'a', 'x')):
            self.mode = WRITE
            self.size = 0
            self.offset = 0
            self.name = filename
            self.compress = brotli.Compressor(
                quality=quality, lgwin=lgwin, lgblock=lgblock)
        else:
            raise ValueError("Invalid mode: {!r}".format(mode))

        self.fileobj = fileobj

    @property
    def mtime(self):
        """Last modification time read from stream, or None"""
        return self._buffer.raw._last_mtime

    def __repr__(self):
        s = repr(self.fileobj)
        return '<brotli ' + s[1:-1] + ' ' + hex(id(self)) + '>'

    def write(self, data):
        self._check_not_closed()
        if self.mode != WRITE:
            import errno
            raise OSError(errno.EBADF, "write() on read-only BrotliFile object")

        if self.fileobj is None:
            raise ValueError("write() on closed BrotliFile object")

        if isinstance(data, bytes):
            length = len(data)
        else:
            # accept any data that supports the buffer protocol
            data = memoryview(data)
            length = data.nbytes

        if length > 0:
            self.fileobj.write(self.compress.process(data))
            self.size += length
            self.offset += length

        return length

    def read(self, size=-1):
        self._check_not_closed()
        if self.mode != READ:
            import errno
            raise OSError(errno.EBADF, "read() on write-only BrotliFile object")
        return self._buffer.read(size)

    def read1(self, size=-1):
        """Implements BufferedIOBase.read1()

        Reads up to a buffer's worth of data if size is negative."""
        self._check_not_closed()
        if self.mode != READ:
            import errno
            raise OSError(errno.EBADF, "read1() on write-only BrotliFile object")

        if size < 0:
            size = io.DEFAULT_BUFFER_SIZE
        return self._buffer.read1(size)

    def peek(self, n):
        self._check_not_closed()
        if self.mode != READ:
            import errno
            raise OSError(errno.EBADF, "peek() on write-only BrotliFile object")
        return self._buffer.peek(n)

    @property
    def closed(self):
        return self.fileobj is None

    def close(self):
        fileobj = self.fileobj
        if fileobj is None:
            return
        self.fileobj = None
        try:
            if self.mode == WRITE:
                fileobj.write(self.compress.flush())
                fileobj.write(self.compress.finish())
            elif self.mode == READ:
                self._buffer.close()
        finally:
            myfileobj = self.myfileobj
            if myfileobj:
                self.myfileobj = None
                myfileobj.close()

    def flush(self):
        self._check_not_closed()
        if self.mode == WRITE:
            # Ensure the compressor's buffer is flushed
            self.fileobj.write(self.compress.flush())
            self.fileobj.flush()

    def fileno(self):
        """Invoke the underlying file object's fileno() method.

        This will raise AttributeError if the underlying file object
        doesn't support fileno().
        """
        return self.fileobj.fileno()

    def rewind(self):
        '''Return the uncompressed stream file position indicator to the
        beginning of the file'''
        if self.mode != READ:
            raise OSError("Can't rewind in write mode")
        self._buffer.seek(0)

    def readable(self):
        return self.mode == READ

    def writable(self):
        return self.mode == WRITE

    def seekable(self):
        return True

    def seek(self, offset, whence=io.SEEK_SET):
        if self.mode == WRITE:
            if whence != io.SEEK_SET:
                if whence == io.SEEK_CUR:
                    offset = self.offset + offset
                else:
                    raise ValueError('Seek from end not supported')
            if offset < self.offset:
                raise OSError('Negative seek in write mode')
            count = offset - self.offset
            chunk = b'\0' * 1024
            for i in range(count // 1024):
                self.write(chunk)
            self.write(b'\0' * (count % 1024))
        elif self.mode == READ:
            self._check_not_closed()
            return self._buffer.seek(offset, whence)

        return self.offset

    def readline(self, size=-1):
        self._check_not_closed()
        return self._buffer.readline(size)


class _BrotliDecompressor:
    eof = False

    def __init__(self):
        self.decompressor = brotli.Decompressor()
        self.needs_input = True
        self._buffer = bytearray(1)
        self._bufview = memoryview(self._buffer)
        self._buflen = len(self._buffer)
        self._pos = 0

    def _check_buffer(self, new_len):
        if self._buflen < new_len:
            new_len = max(self._buflen, new_len)
            del self._bufview
            self._buffer.extend(b'\0' * (new_len * 2))
            self._bufview = memoryview(self._buffer)
            self._buflen = len(self._buffer)

    def decompress(self, raw, size):
        if raw:
            uncompress = self.decompressor.process(raw)
            new_len = len(uncompress)
            self.needs_input = False
        else:
            uncompress = b''
            new_len = 0

        if self._pos >= size:
            r = bytes(self._bufview[:size])
            pos = self._pos - size

            self._check_buffer(pos + new_len)
            self._bufview[:pos] = self._bufview[size:self._pos]
            self._bufview[pos:pos + new_len] = uncompress
            self._pos = pos + new_len
        elif self._pos + new_len >= size:
            used_len = size - self._pos
            r = bytes(self._bufview[:self._pos]) + uncompress[:used_len]

            rem_len = new_len - used_len
            self._check_buffer(rem_len)
            self._bufview[:rem_len] = uncompress[used_len:]
            self._pos = rem_len
        else:
            r = bytes(self._bufview[:self._pos]) + uncompress
            self._pos = 0
            self.needs_input = True
        return r


class _BrotliReader(_compression.DecompressReader):
    def read(self, size=-1):
        try:
            return super(_BrotliReader, self).read(size)
        except EOFError:
            return b''
