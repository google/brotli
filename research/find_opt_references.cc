/* Copyright 2016 Google Inc. All Rights Reserved.
   Author: zip753@gmail.com (Ivan Nikulin)

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Tool for generating optimal backward references for the input file. Uses
   sais-lite library for building suffix array. */

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

#include <gflags/gflags.h>
using gflags::ParseCommandLineFlags;

#include "./esaxx/sais.hxx"

DEFINE_int32(min_length, 1, "Minimal length of found backward references.");
DEFINE_int32(skip, 1, "Number of bytes to skip.");

const size_t kFileBufferSize = (1 << 16);  // 64KB

typedef int sarray_type;  // Can't make it unsigned because of templates :(
typedef uint8_t input_type;
typedef uint32_t lcp_type;

void ReadInput(FILE* fin, input_type* storage, size_t input_size) {
  size_t last_pos = 0;
  size_t available_in;
  fseek(fin, 0, SEEK_SET);
  do {
    available_in = fread(storage + last_pos, 1, kFileBufferSize, fin);
    last_pos += available_in;
  } while (available_in != 0);
  assert(last_pos == input_size);
}

void BuildLCP(input_type* storage, sarray_type* sarray, lcp_type* lcp,
              size_t size, uint32_t* pos) {
  for (int i = 0; i < size; ++i) {
    pos[sarray[i]] = i;
  }
  uint32_t k = 0;
  lcp[size - 1] = 0;
  for (int i = 0; i < size; ++i) {
    if (pos[i] == size - 1) {
      k = 0;
      continue;
    }
    uint32_t j = sarray[pos[i] + 1];  // Suffix which follow i-th suffix in SA.
    while (i + k < size && j + k < size && storage[i + k] == storage[j + k]) {
      ++k;
    }
    lcp[pos[i]] = k;
    if (k > 0) --k;
  }
}

void ProcessReferences(input_type* storage, sarray_type* sarray, lcp_type* lcp,
                       size_t size, uint32_t* pos, FILE* fout) {
  int min_length = FLAGS_min_length;
  for (int idx = FLAGS_skip; idx < size; ++idx) {
    int max_lcp = -1;
    int max_lcp_ix;
    int left_lcp = -1;
    int left_ix;
    for (left_ix = pos[idx] - 1; left_ix >= 0; --left_ix) {
      if (left_lcp == -1 || left_lcp > lcp[left_ix]) {
        left_lcp = lcp[left_ix];
      }
      if (left_lcp == 0) break;
      if (sarray[left_ix] < idx) break;
    }
    if (left_ix >= 0) {
      max_lcp = left_lcp;
      max_lcp_ix = left_ix;
    }

    int right_lcp = -1;
    int right_ix;
    for (right_ix = pos[idx]; right_ix < size - 1; ++right_ix) {
      if (right_lcp == -1 || right_lcp > lcp[right_ix]) {
        right_lcp = lcp[right_ix];
      }
      // Stop if we have better result from the left side already.
      if (right_lcp < max_lcp) break;
      if (right_lcp == 0) break;
      if (sarray[right_ix] < idx) break;
    }
    if (right_lcp > max_lcp && right_ix < size - 1) {
      max_lcp = right_lcp;
      max_lcp_ix = right_ix;
    }

    if (max_lcp >= min_length) {
      int dist = idx - sarray[max_lcp_ix];
      if (dist <= 0) {
        printf("idx = %d, pos[idx] = %u\n", idx, pos[idx]);
        printf("left_ix = %d, right_ix = %d\n",
                left_ix, right_ix);
        printf("left_lcp = %d, right_lcp = %d\n",
                left_lcp, right_lcp);
        printf("sarray[left_ix] = %d, sarray[right_ix] = %d\n",
                sarray[left_ix], sarray[right_ix]);
        assert(dist > 0);
      }
      fputc(1, fout);
      fwrite(&idx, sizeof(int), 1, fout);  // Position in input.
      fwrite(&dist, sizeof(int), 1, fout);  // Backward distance.
    }
  }
}

int main(int argc, char* argv[]) {
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 3) {
    printf("usage: %s input_file output_file\n", argv[0]);
    return 1;
  }

  FILE* fin = fopen(argv[1], "rb");
  FILE* fout = fopen(argv[2], "w");

  fseek(fin, 0, SEEK_END);
  int input_size = ftell(fin);
  fseek(fin, 0, SEEK_SET);
  printf("The file size is %u bytes\n", input_size);

  input_type* storage = new input_type[input_size];

  ReadInput(fin, storage, input_size);
  fclose(fin);

  sarray_type* sarray = new sarray_type[input_size];
  saisxx(storage, sarray, input_size);
  printf("Suffix array calculated.\n");

  // Inverse suffix array.
  uint32_t* pos = new uint32_t[input_size];

  lcp_type* lcp = new lcp_type[input_size];
  BuildLCP(storage, sarray, lcp, input_size, pos);
  printf("LCP array constructed.\n");

  ProcessReferences(storage, sarray, lcp, input_size, pos, fout);
  fclose(fout);
  return 0;
}
