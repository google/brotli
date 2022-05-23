#!/bin/bash -eu
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

cmake . -DBUILD_TESTING=OFF
make clean
make -j$(nproc) brotlidec-static

$CC $CFLAGS -c -std=c99 -I. -I./c/include c/fuzz/decode_fuzzer.c 

$CXX $CXXFLAGS ./decode_fuzzer.o  -o $OUT/decode_fuzzer \
    $LIB_FUZZING_ENGINE ./libbrotlidec-static.a ./libbrotlicommon-static.a

cp java/org/brotli/integration/fuzz_data.zip $OUT/decode_fuzzer_seed_corpus.zip
chmod a-x $OUT/decode_fuzzer_seed_corpus.zip # we will try to run it otherwise
