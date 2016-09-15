## Introduction

This directory contains several research tools that have been very useful during LZ77 backward distance encoding research.

Notice that all `FLAGS_*` variables were supposed to be command-line flags.

## Tools
### find\_opt\_references

This tool generates optimal (match-length-wise) backward references for every position in the input files and stores them in `*.dist` file described below.

Example usage:

    find_opt_references input.txt output.dist

### draw\_histogram

This tool generates a visualization of the distribution of backward references stored in `*.dist` file. The output is a grayscale PGM (binary) image.

Example usage:

    draw_histogram input.dist 65536 output.pgm

### draw\_diff

This tool generates a diff PPM (binary) image between two input PGM (binary) images. Input images must be of same size and contain 255 colors. Useful for comparing different backward references distributions for same input file.

Example usage:

    draw_diff image1.pgm image2.pgm diff.ppm


## Backward distance file format

The format of `*.dist` files is as follows:

    [[     0| match legnth][     1|position|distance]...]
     [1 byte|      4 bytes][1 byte| 4 bytes| 4 bytes]

More verbose explanation: for each backward reference there is a position-distance pair, also a copy length may be specified. Copy length is prefixed with flag byte 0, position-distance pair is prefixed with flag byte 1. Each number is a 32-bit integer. Copy length always comes before position-distance pair. Standalone copy length is allowed, in this case it is ignored.

Here's an example how to read from `*.dist` file:

```c++
#include "read_dist.h"

FILE* f;
int copy, pos, dist;
while (ReadBackwardReference(fin, &copy, &pos, &dist)) {
   ...
}
```
