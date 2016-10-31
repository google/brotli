This directory contains the code for the Python `brotli` module,
`bro.py` tool, and roundtrip tests.


### Development

To build the module, execute the following from the root project
directory:

    $ python setup.py build

To test the module, execute the following from the root project
directory:

    $ python setup.py test


### Code Style

Brotli's code follows the [Google Python Style Guide][].  To
automatically format your code, install [YAPF][]:

    $ pip install yapf

Then, either format a single file:

    $ yapf --in-place FILE

Or, format all files in a directory:

    $ yapf --in-place --recursive DIR

See the [YAPF usage][] documentation for more information.


[Google Python Style Guide]: https://google.github.io/styleguide/pyguide.html
[YAPF]: https://github.com/google/yapf
[YAPF usage]: https://github.com/google/yapf#usage
