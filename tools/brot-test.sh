#!/bin/sh
#
# Copyright (C) 2015 Assaf Gordon (assafgordon@gmail.com)
# License: http://www.apache.org/licenses/LICENSE-2.0
#
# Test harness for 'brot' compression program,
# compare it with the common usage of gzip/bzip2/xz .

set -e

die()
{
    base=$(basename "$0")
    echo "$base: error: $@" >&2
    test -n "$D" && echo "$base: temp test directory: $D" >&2
    exit 1
}

## Test locally compiled brotli compression program
for p in brot unbrot brotcat ; do
    test -x "$p" \
        || die "'$p' program not find in current directory " \
               "(run 'make' to build it)"
done
export PATH=$CWD:$PATH


# Run a small set of tests, exercising the most common usage pattern
# for a unix compression program, e.g:
#   comp FILE
#   comp < FILE > OUTPUT
#   comp FILE > OUTPUT
#   comp -d FILE > OUTPUT
#   comp -k FILE > OUTPUT
#   decomp FILE > OUTPUT
#   Xcat FILE > OUTPUT
#
# parameters:
#   $1 = compression program (e.g. bzip2)
#   $2 = decompression program (e.g. bunzip)
#   $3 = decomp-cat program (e.g. bzcat)
#   $4 = compressed file extension (e.g. 'bz2')
#
# dies on error with exit-code 1
runtests()
{
  comp="$1"
  decomp="$2"
  cat="$3"
  ext="$4"

  D=$(mktemp -d test-$comp.XXXXXX) \
      || die "failed to create temp test directory"

  printf "hello world" > "$D/data"

  # Compress stdin to stdout
  $comp < $D/data > $D/out1.$ext || die "$comp - test 1 failed"

  # decompress stdin to stdout
  $comp -d < $D/out1.$ext > $D/out1 || die "$comp - test 2 failed"
  cmp $D/data $D/out1 || die "$comp - test 3 failed"

  # Compress a file to stdout (-c)
  $comp -c $D/data > $D/out2.$ext || die "$comp - test 4 failed"

  # Decompress and compare
  $comp -d < $D/out2.$ext > $D/out3 || die "$comp - test 5 failed"
  cmp $D/data $D/out3 || die "$comp - test 6 failed"

  # Compress a file into a '.bro' file
  $comp $D/data || die "$comp - test 8 failed"
  # After compression, input file should not exist any more
  test -e $D/data && die "$comp - test 9 failed"
  # After compression, a '.bro' file should be created
  test -e $D/data.$ext || die "$comp - test 10 failed"

  # Decompress a '.bro' file into the original file
  $comp -d $D/data.$ext || die "$comp - test 11 failed"
  # After decompression, input file should not exist any more
  test -e $D/data.$ext && die "$comp - test 12 failed"
  # After decompression, the original input file name (without '.bro') should exist
  test -e $D/data || die "$comp - test 13 failed"


  # Compress and Keep the original
  $comp -k $D/data || die "$comp - test 14 failed"
  # After compression, original input file should STILL exist
  test -e $D/data || die "$comp - test 15 failed"
  # After compression, the compressed file sohuld also exist
  test -e $D/data || die "$comp - test 16 failed"


  # Decompress, while the expected output file already exist.
  # The compression program should abort with an error
  # (the printf is used to set STDIN to NULL, so gzip will not ask for confirmation)
  printf "" | $comp -d $D/data.$ext 2>/dev/null \
      && die "$comp - test 17 failed (brot did not warn about existing file)"
  # Decompess again with force-overwrite
  $comp -df $D/data.$ext || die "$comp - test 18 failed"

  # Compress again
  $comp -k $D/data || die "$comp - test 19 failed"
  # Print with brotcat
  $cat $D/data.$ext > $D/out4 || die "$comp - test 20 failed"
  # compare output
  cmp $D/data $D/out4 || die "$comp - test 21 failed"

  # decompress with unbrot, overwrite existing output file
  $decomp -f $D/data.$ext || die "$comp - test 22 failed"

  # After decompression, original should exist, compressed file should not
  test -e $D/data || die "$comp - test 23 failed"
  test -e $D/data.$ext && die "$comp - test 24 failed"

  # End of tests, delete temp dir
  rm -r "$D"
  echo "$comp - no tests failure"
}

# Run the same tests on each program.
# we're not really testing gzip/bzip2/xz - just making sure they all behave
# the same as 'brot'.
runtests gzip  gunzip  zcat    gz
runtests bzip2 bunzip2 bzcat   bz2
runtests xz    unxz    xzcat   xz
runtests brot  unbrot  brotcat bro
