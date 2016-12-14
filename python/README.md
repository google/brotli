This directory contains the code for the Python `brotli` module,
`bro.py` tool, and roundtrip tests.

### Installation

If you just want to install the module from source, execute the
following from the root project directory:

    $ python setup.py install

### Development

For development, reinstalling the module with every change is time
consuming.  Instead, we recommend using the `setuptools`
"[development mode][]" to make the module available while still being
able to edit the source files.

We provide a `Makefile` to simplify common commands:

    $ make          # Deploy the module in "development mode"
   
    $ make tests    # Test the module

    $ make clean    # Remove all temporary files and build output

### Code Style

Brotli's code follows the [Google Python Style Guide][].  To
automatically format your code, first install [YAPF][]:

    $ pip install yapf

Then, to format all files in the project, you can run:

    $ make fix      # Automatically format code

See the [YAPF usage][] documentation for more information.


[development mode]: https://setuptools.readthedocs.io/en/latest/setuptools.html#development-mode
[Google Python Style Guide]: https://google.github.io/styleguide/pyguide.html
[YAPF]: https://github.com/google/yapf
[YAPF usage]: https://github.com/google/yapf#usage
