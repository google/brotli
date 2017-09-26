#!/usr/bin/env bash
set -e

export CC=${CC:-cc}
export CXX=${CXX:-c++}

BROTLI="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../.." && pwd )"
SRC=$BROTLI/c

cd $BROTLI

rm -rf bin
mkdir bin
cd bin

cmake $BROTLI -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DBUILD_TESTING=OFF -DENABLE_SANITIZER=address
make -j$(nproc) brotlidec-static

${CXX} -c -std=c++11 $SRC/fuzz/decode_fuzzer.cc -I$SRC/include
ar rvs decode_fuzzer.a decode_fuzzer.o
${CXX} $SRC/fuzz/run_decode_fuzzer.cc -o run_decode_fuzzer \
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
