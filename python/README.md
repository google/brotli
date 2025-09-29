This directory contains the code for the Python `brotli` module,
`bro.py` tool, and roundtrip tests.

Only Python 2.7+ is supported.

We provide a `Makefile` to simplify common development commands.

### Installation

If you just want to install the latest release of the Python `brotli`
module, we recommend installing from [PyPI][]:

    $ pip install brotli

Alternatively, you may install directly from source by running the
following command from this directory:

    $ make install

If you already have native Brotli installed on your system and want to use this one instead of the vendored sources, you
should set the `USE_SYSTEM_BROTLI=1` environment variable when building the wheel, like this:

    $ USE_SYSTEM_BROTLI=1 pip install brotli --no-binary brotli

Brotli is found via the `pkg-config` utility. Moreover, you must build all 3 `brotlicommon`, `brotlienc`, and `brotlidec`
components. If you're installing brotli from the package manager, you need the development package, like this on Fedora:

    $ dnf install brotli brotli-devel

### Development

You may run the following commands from this directory:

    $ make          # Build the module in-place

    $ make test     # Test the module

    $ make clean    # Remove all temporary files and build output

If you wish to make the module available while still being
able to edit the source files, you can use the `setuptools`
"[development mode][]":

    $ make develop  # Install the module in "development mode"

### Code Style

Brotli's code follows the [Google Python Style Guide][].  To
automatically format your code, first install [YAPF][]:

    $ pip install yapf

Then, to format all files in the project, you can run:

    $ make fix      # Automatically format code

See the [YAPF usage][] documentation for more information.


[PyPI]: https://pypi.org/project/Brotli/
[development mode]: https://setuptools.readthedocs.io/en/latest/setuptools.html#development-mode
[Google Python Style Guide]: https://google.github.io/styleguide/pyguide.html
[YAPF]: https://github.com/google/yapf
[YAPF usage]: https://github.com/google/yapf#usage
