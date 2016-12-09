#!/usr/bin/env bash

BROTLI="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

cd $BROTLI

rm -rf bin
mkdir bin
cd bin

cmake .. -B./ -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DENABLE_SANITIZER=address
make clean
make -j$(nproc) brotlidec

c++ -c -std=c++11 ../fuzz/decode_fuzzer.cc -I./include
ar rvs decode_fuzzer.a decode_fuzzer.o
c++ ../fuzz/run_decode_fuzzer.cc -o run_decode_fuzzer -lasan decode_fuzzer.a ./libbrotlidec.a ./libbrotlicommon.a

mkdir decode_corpora
unzip ../java/integration/fuzz_data.zip -d decode_corpora

for f in `ls decode_corpora`
do
 echo "Testing $f"
 ./run_decode_fuzzer decode_corpora/$f
done

cd $BROTLI
rm -rf bin
