/* Copyright 2014 Google Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Example main() function for Brotli library.
*/

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#include <fcntl.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <io.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#include "../dec/decode.h"
#include "../enc/encode.h"
#include "../enc/streams.h"


static bool ParseQuality(const char* s, int* quality) {
  if (s[0] >= '0' && s[0] <= '9') {
    *quality = s[0] - '0';
    if (s[1] >= '0' && s[1] <= '9') {
      *quality = *quality * 10 + s[1] - '0';
      return s[2] == 0;
    }
    return s[1] == 0;
  }
  return false;
}

static void ParseArgv(int argc, char **argv,
                      char **input_path,
                      char **output_path,
                      int *force,
                      int *quality,
                      int *decompress) {
  *force = 0;
  *input_path = 0;
  *output_path = 0;
  {
    size_t argv0_len = strlen(argv[0]);
    *decompress =
        argv0_len >= 5 && strcmp(&argv[0][argv0_len - 5], "unbro") == 0;
  }
  for (int k = 1; k < argc; ++k) {
    if (!strcmp("--force", argv[k]) ||
        !strcmp("-f", argv[k])) {
      if (*force != 0) {
        goto error;
      }
      *force = 1;
      continue;
    } else if (!strcmp("--decompress", argv[k]) ||
               !strcmp("--uncompress", argv[k]) ||
               !strcmp("-d", argv[k])) {
      *decompress = 1;
      continue;
    }
    if (k < argc - 1) {
      if (!strcmp("--input", argv[k]) ||
          !strcmp("--in", argv[k]) ||
          !strcmp("-i", argv[k])) {
        if (*input_path != 0) {
          goto error;
        }
        *input_path = argv[k + 1];
        ++k;
        continue;
      } else if (!strcmp("--output", argv[k]) ||
                 !strcmp("--out", argv[k]) ||
                 !strcmp("-o", argv[k])) {
        if (*output_path != 0) {
          goto error;
        }
        *output_path = argv[k + 1];
        ++k;
        continue;
      } else if (!strcmp("--quality", argv[k]) ||
                 !strcmp("-q", argv[k])) {
        if (!ParseQuality(argv[k + 1], quality)) {
          goto error;
        }
        ++k;
        continue;
      }
    }
    goto error;
  }
  return;
error:
  fprintf(stderr,
          "Usage: %s [--force] [--quality n] [--decompress]"
          " [--input filename] [--output filename]\n",
          argv[0]);
  exit(1);
}

static FILE* OpenInputFile(const char* input_path) {
  if (input_path == 0) {
    return fdopen(STDIN_FILENO, "rb");
  }
  FILE* f = fopen(input_path, "rb");
  if (f == 0) {
    perror("fopen");
    exit(1);
  }
  return f;
}

static FILE *OpenOutputFile(const char *output_path, const int force) {
  if (output_path == 0) {
    return fdopen(STDOUT_FILENO, "wb");
  }
  if (!force) {
    struct stat statbuf;
    if (stat(output_path, &statbuf) == 0) {
      fprintf(stderr, "output file exists\n");
      exit(1);
    }
  }
  int fd = open(output_path, O_CREAT | O_WRONLY | O_TRUNC
#ifdef _MSC_VER
	  | O_BINARY
#else
	  , S_IRUSR | S_IWUSR
#endif
	  );
  if (fd < 0) {
    perror("open");
    exit(1);
  }
  return fdopen(fd, "wb");
}

int main(int argc, char** argv) {
  char *input_path = 0;
  char *output_path = 0;
  int force = 0;
  int quality = 11;
  int decompress = 0;
  ParseArgv(argc, argv, &input_path, &output_path, &force,
            &quality, &decompress);
  FILE* fin = OpenInputFile(input_path);
  FILE* fout = OpenOutputFile(output_path, force);
  if (decompress) {
    BrotliInput in = BrotliFileInput(fin);
    BrotliOutput out = BrotliFileOutput(fout);
    if (!BrotliDecompress(in, out)) {
      fprintf(stderr, "corrupt input\n");
      exit(1);
    }
  } else {
    brotli::BrotliParams params;
    params.quality = quality;
    brotli::BrotliFileIn in(fin, 1 << 16);
    brotli::BrotliFileOut out(fout);
    if (!BrotliCompress(params, &in, &out)) {
      fprintf(stderr, "compression failed\n");
      unlink(output_path);
      exit(1);
    }
  }
  if (fclose(fin) != 0) {
    perror("fclose");
    exit(1);
  }
  if (fclose(fout) != 0) {
    perror("fclose");
    exit(1);
  }
  return 0;
}
