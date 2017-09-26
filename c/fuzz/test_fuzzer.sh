#!/usr/bin/env bash

BROTLI="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../.." && pwd )"
SRC=$BROTLI/c

cd $BROTLI

rm -rf bin
mkdir bin
cd bin

cmake $BROTLI -B./ -DBUILD_TESTING=OFF -DENABLE_SANITIZER=address
make clean
make -j$(nproc) brotlidec-static

c++ -c -std=c++11 $SRC/fuzz/decode_fuzzer.cc -I$SRC/include
ar rvs decode_fuzzer.a decode_fuzzer.o
c++ $SRC/fuzz/run_decode_fuzzer.cc -o run_decode_fuzzer \
    -lasan decode_fuzzer.a ./libbrotlidec-static.a ./libbrotlicommon-static.a

mkdir decode_corpora
unzip $BROTLI/java/org/brotli/integration/fuzz_data.zip -d decode_corpora

for f in `ls decode_corpora`
do
 echo "Testing $f"
 ./run_decode_fuzzer decode_corpora/$f
done

cd $BROTLI
rm -rf bin
