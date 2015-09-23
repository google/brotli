#!/bin/sh

# Copyright (C) 2015 Assaf Gordon (assafgordon@gmail.com)
# License: http://www.apache.org/licenses/LICENSE-2.0
#
# Test harness for 'brot' compression program,
# compare compression sizes of different inputs with gzip/bzip2/xz

set -e

die()
{
    base=$(basename "$0")
    echo "$base: error: $@" >&2
    test -n "$D" && echo "$base: temp test directory: $D" >&2
    exit 1
}

gen_random_num()
{
  N="$1"
  shuf -n $N -i 1-1000000
}

gen_random_words()
{
  N="$1"
  shuf -n $N /usr/share/dict/words
}

gen_random_bin()
{
  N="$1"
  openssl enc -aes-256-ctr -pass pass:"42" \
          -nosalt </dev/zero 2>/dev/null | head -c "$N"
}

## Test locally compiled brotli compression program
for p in brot unbrot brotcat ; do
    test -x "$p" \
        || die "'$p' program not find in current directory " \
               "(run 'make' to build it)"
done
export PATH=$CWD:$PATH

D=$(mktemp -d test-brot-speed.XXXXXX) || die "failed to create temp dir"


echo "Creating random data files in $D..."
gen_random_num 1000    > $D/num1K || die "setup failed"
gen_random_num 50000   > $D/num50K || die "setup failed"
gen_random_num 100000  > $D/num100K || die "setup failed"
gen_random_words 1000  > $D/words1K || die "setup failed"
gen_random_words 5000  > $D/words5K || die "setup failed"
gen_random_words 10000 > $D/words10K || die "setup failed"
gen_random_bin 100K    > $D/bin100K || die "setup failed"
gen_random_bin 1M      > $D/bin1M || die "setup failed"
gen_random_bin 10M     > $D/bin10M || die "setup failed"

# Compress all files
# TODO: measure time?
for i in $D/* ; do
  for p in gzip bzip2 xz brot ; do
    echo "running $p on $i..."
    $p -k $i
  done
done

# Show results
printf "%-10s %10s %10s %10s %10s %10s\n" file size .gz .bz2 .xz .bro
for f in num1K num50K num100K words1K words5K words10K bin100K bin1M bin10M ; do
  # Sizes for different compressions
  orig=$(stat -c %s $D/$f)
  gz=$(  stat -c %s $D/$f.gz)
  bz2=$( stat -c %s $D/$f.bz2)
  xz=$(  stat -c %s $D/$f.xz)
  bro=$( stat -c %s $D/$f.bro)

  printf "%-10s %10s %10s %10s %10s %10s\n" $f $orig $gz $bz2 $xz $bro
done

rm -r "$D"
