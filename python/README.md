This directory contains the code for the Python `brotli` module,
`bro.py` tool, and roundtrip tests.

We provide a `Makefile` to simplify common development commands.

### Installation

If you just want to install the latest release of the Python `brotli`
module, we recommend installing from [PyPI][]:

    $ pip install brotli

Alternatively, you may install directly from source by running the
following command from this directory:

    $ make install

### Development

For development, reinstalling the module with every change is time
consuming.  Instead, we recommend using the `setuptools`
"[development mode][]" to make the module available while still being
able to edit the source files.

For convenience, you may run the following commands from this directory:

    $ make          # Build the module in-place
   
    $ make tests    # Test the module

    $ make clean    # Remove all temporary files and build output

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
