/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Example main() function for Brotli library. */

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ctime>
#include <string>

#include "../dec/decode.h"
#include "../enc/encode.h"


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
                      int *decompress,
                      int *repeat,
                      int *verbose,
                      int *lgwin) {
  *force = 0;
  *input_path = 0;
  *output_path = 0;
  *repeat = 1;
  *verbose = 0;
  *lgwin = 22;
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
    } else if (!strcmp("--verbose", argv[k]) ||
               !strcmp("-v", argv[k])) {
      if (*verbose != 0) {
        goto error;
      }
      *verbose = 1;
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
      } else if (!strcmp("--repeat", argv[k]) ||
                 !strcmp("-r", argv[k])) {
        if (!ParseQuality(argv[k + 1], repeat)) {
          goto error;
        }
        ++k;
        continue;
      }  else if (!strcmp("--window", argv[k]) ||
                  !strcmp("-w", argv[k])) {
        if (!ParseQuality(argv[k + 1], lgwin)) {
          goto error;
        }
        if (*lgwin < 10 || *lgwin >= 25) {
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
          " [--input filename] [--output filename] [--repeat iters]"
          " [--verbose] [--window n]\n",
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
  int excl = force ? 0 : O_EXCL;
#if defined(_WIN32)
  int fd = open(output_path, O_CREAT | excl | O_WRONLY | O_TRUNC | O_BINARY,
                S_IREAD | S_IWRITE);
#else
  int fd = open(output_path, O_CREAT | excl | O_WRONLY | O_TRUNC,
                S_IRUSR | S_IWUSR);
#endif
  if (fd < 0) {
    if (!force) {
      struct stat statbuf;
      if (stat(output_path, &statbuf) == 0) {
        fprintf(stderr, "output file exists\n");
        exit(1);
      }
    }
    perror("open");
    exit(1);
  }
  return fdopen(fd, "wb");
}

int64_t FileSize(char *path) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0L, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  int64_t retval = ftell(f);
  if (fclose(f) != 0) {
    return -1;
  }
  return retval;
}

static const size_t kFileBufferSize = 65536;

void Decompresss(FILE* fin, FILE* fout) {
  uint8_t* input = new uint8_t[kFileBufferSize];
  uint8_t* output = new uint8_t[kFileBufferSize];
  size_t total_out;
  size_t available_in;
  const uint8_t* next_in;
  size_t available_out = kFileBufferSize;
  uint8_t* next_out = output;
  BrotliResult result = BROTLI_RESULT_NEEDS_MORE_INPUT;
  BrotliState s;
  BrotliStateInit(&s);
  while (1) {
    if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
      if (feof(fin)) {
        break;
      }
      available_in = fread(input, 1, kFileBufferSize, fin);
      next_in = input;
      if (ferror(fin)) {
        break;
      }
    } else if (result == BROTLI_RESULT_NEEDS_MORE_OUTPUT) {
      fwrite(output, 1, kFileBufferSize, fout);
      if (ferror(fout)) {
        break;
      }
      available_out = kFileBufferSize;
      next_out = output;
    } else {
      break; /* Error or success. */
    }
    result = BrotliDecompressStream(&available_in, &next_in,
        &available_out, &next_out, &total_out, &s);
  }
  if (next_out != output) {
    fwrite(output, 1, next_out - output, fout);
  }
  BrotliStateCleanup(&s);
  delete[] input;
  delete[] output;
  if ((result == BROTLI_RESULT_NEEDS_MORE_OUTPUT) || ferror(fout)) {
    fprintf(stderr, "failed to write output\n");
    exit(1);
  } else if (result != BROTLI_RESULT_SUCCESS) { /* Error or needs more input. */
    fprintf(stderr, "corrupt input\n");
    exit(1);
  }
}

int main(int argc, char** argv) {
  char *input_path = 0;
  char *output_path = 0;
  int force = 0;
  int quality = 11;
  int decompress = 0;
  int repeat = 1;
  int verbose = 0;
  int lgwin = 0;
  ParseArgv(argc, argv, &input_path, &output_path, &force,
            &quality, &decompress, &repeat, &verbose, &lgwin);
  const clock_t clock_start = clock();
  for (int i = 0; i < repeat; ++i) {
    FILE* fin = OpenInputFile(input_path);
    FILE* fout = OpenOutputFile(output_path, force);
    if (decompress) {
      Decompresss(fin, fout);
    } else {
      brotli::BrotliParams params;
      params.lgwin = lgwin;
      params.quality = quality;
      try {
        brotli::BrotliFileIn in(fin, 1 << 16);
        brotli::BrotliFileOut out(fout);
        if (!BrotliCompress(params, &in, &out)) {
          fprintf(stderr, "compression failed\n");
          unlink(output_path);
          exit(1);
        }
      } catch (std::bad_alloc&) {
        fprintf(stderr, "not enough memory\n");
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
  }
  if (verbose) {
    const clock_t clock_end = clock();
    double duration =
        static_cast<double>(clock_end - clock_start) / CLOCKS_PER_SEC;
    if (duration < 1e-9) {
      duration = 1e-9;
    }
    int64_t uncompressed_size = FileSize(decompress ? output_path : input_path);
    if (uncompressed_size == -1) {
      fprintf(stderr, "failed to determine uncompressed file size\n");
      exit(1);
    }
    double uncompressed_bytes_in_MB =
        (repeat * uncompressed_size) / (1024.0 * 1024.0);
    if (decompress) {
      printf("Brotli decompression speed: ");
    } else {
      printf("Brotli compression speed: ");
    }
    printf("%g MB/s\n", uncompressed_bytes_in_MB / duration);
  }
  return 0;
}
