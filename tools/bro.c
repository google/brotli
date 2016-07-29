/* Copyright 2014 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Example main() function for Brotli library. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "../dec/decode.h"
#include "../enc/encode.h"

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#include <share.h>

#define MAKE_BINARY(FILENO) (_setmode((FILENO), _O_BINARY), (FILENO))

#if !defined(__MINGW32__)
#define STDIN_FILENO MAKE_BINARY(_fileno(stdin))
#define STDOUT_FILENO MAKE_BINARY(_fileno(stdout))
#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#endif
#define fdopen _fdopen
#define unlink _unlink

#define fopen ms_fopen
#define open ms_open

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
#define fseek _fseeki64
#define ftell _ftelli64
#endif

static FILE* ms_fopen(const char *filename, const char *mode) {
  FILE* result = 0;
  fopen_s(&result, filename, mode);
  return result;
}

static int ms_open(const char *filename, int oflag, int pmode) {
  int result = -1;
  _sopen_s(&result, filename, oflag | O_BINARY, _SH_DENYNO, pmode);
  return result;
}
#endif  /* WIN32 */

static int ParseQuality(const char* s, int* quality) {
  if (s[0] >= '0' && s[0] <= '9') {
    *quality = s[0] - '0';
    if (s[1] >= '0' && s[1] <= '9') {
      *quality = *quality * 10 + s[1] - '0';
      return (s[2] == 0) ? 1 : 0;
    }
    return (s[1] == 0) ? 1 : 0;
  }
  return 0;
}

static void ParseArgv(int argc, char **argv,
                      char **input_path,
                      char **output_path,
                      char **dictionary_path,
                      int *force,
                      int *quality,
                      int *decompress,
                      int *repeat,
                      int *verbose,
                      int *lgwin) {
  int k;
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
  for (k = 1; k < argc; ++k) {
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
      } else if (!strcmp("--custom-dictionary", argv[k])) {
        if (*dictionary_path != 0) {
          goto error;
        }
        *dictionary_path = argv[k + 1];
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
          " [--verbose] [--window n] [--custom-dictionary filename]\n",
          argv[0]);
  exit(1);
}

static FILE* OpenInputFile(const char* input_path) {
  FILE* f;
  if (input_path == 0) {
    return fdopen(STDIN_FILENO, "rb");
  }
  f = fopen(input_path, "rb");
  if (f == 0) {
    perror("fopen");
    exit(1);
  }
  return f;
}

static FILE *OpenOutputFile(const char *output_path, const int force) {
  int fd;
  if (output_path == 0) {
    return fdopen(STDOUT_FILENO, "wb");
  }
  fd = open(output_path, O_CREAT | (force ? 0 : O_EXCL) | O_WRONLY | O_TRUNC,
            S_IRUSR | S_IWUSR);
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

static int64_t FileSize(const char *path) {
  FILE *f = fopen(path, "rb");
  int64_t retval;
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0L, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  retval = ftell(f);
  if (fclose(f) != 0) {
    return -1;
  }
  return retval;
}

/* Result ownersip is passed to caller.
   |*dictionary_size| is set to resulting buffer size. */
static uint8_t* ReadDictionary(const char* path, size_t* dictionary_size) {
  static const int kMaxDictionarySize = (1 << 24) - 16;
  FILE *f = fopen(path, "rb");
  int64_t file_size_64;
  uint8_t* buffer;
  size_t bytes_read;

  if (f == NULL) {
    perror("fopen");
    exit(1);
  }

  file_size_64 = FileSize(path);
  if (file_size_64 == -1) {
    fprintf(stderr, "could not get size of dictionary file");
    exit(1);
  }

  if (file_size_64 > kMaxDictionarySize) {
    fprintf(stderr, "dictionary is larger than maximum allowed: %d\n",
            kMaxDictionarySize);
    exit(1);
  }
  *dictionary_size = (size_t)file_size_64;

  buffer = (uint8_t*)malloc(*dictionary_size);
  if (!buffer) {
    fprintf(stderr, "could not read dictionary: out of memory\n");
    exit(1);
  }
  bytes_read = fread(buffer, sizeof(uint8_t), *dictionary_size, f);
  if (bytes_read != *dictionary_size) {
    fprintf(stderr, "could not read dictionary\n");
    exit(1);
  }
  fclose(f);
  return buffer;
}

static const size_t kFileBufferSize = 65536;

static int Decompress(FILE* fin, FILE* fout, const char* dictionary_path) {
  /* Dictionary should be kept during first rounds of decompression. */
  uint8_t* dictionary = NULL;
  uint8_t* input;
  uint8_t* output;
  size_t total_out;
  size_t available_in;
  const uint8_t* next_in;
  size_t available_out = kFileBufferSize;
  uint8_t* next_out;
  BrotliResult result = BROTLI_RESULT_ERROR;
  BrotliState* s = BrotliCreateState(NULL, NULL, NULL);
  if (!s) {
    fprintf(stderr, "out of memory\n");
    return 0;
  }
  if (dictionary_path != NULL) {
    size_t dictionary_size = 0;
    dictionary = ReadDictionary(dictionary_path, &dictionary_size);
    BrotliSetCustomDictionary(dictionary_size, dictionary, s);
  }
  input = (uint8_t*)malloc(kFileBufferSize);
  output = (uint8_t*)malloc(kFileBufferSize);
  if (!input || !output) {
    fprintf(stderr, "out of memory\n");
    goto end;
  }
  next_out = output;
  result = BROTLI_RESULT_NEEDS_MORE_INPUT;
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
        &available_out, &next_out, &total_out, s);
  }
  if (next_out != output) {
    fwrite(output, 1, (size_t)(next_out - output), fout);
  }

  if ((result == BROTLI_RESULT_NEEDS_MORE_OUTPUT) || ferror(fout)) {
    fprintf(stderr, "failed to write output\n");
  } else if (result != BROTLI_RESULT_SUCCESS) { /* Error or needs more input. */
    fprintf(stderr, "corrupt input\n");
  }

end:
  free(dictionary);
  free(input);
  free(output);
  BrotliDestroyState(s);
  return (result == BROTLI_RESULT_SUCCESS) ? 1 : 0;
}

static int Compress(int quality, int lgwin, FILE* fin, FILE* fout,
    const char *dictionary_path) {
  BrotliEncoderState* s = BrotliEncoderCreateInstance(0, 0, 0);
  uint8_t* buffer = (uint8_t*)malloc(kFileBufferSize << 1);
  uint8_t* input = buffer;
  uint8_t* output = buffer + kFileBufferSize;
  size_t available_in = 0;
  const uint8_t* next_in = NULL;
  size_t available_out = kFileBufferSize;
  uint8_t* next_out = output;
  int is_eof = 0;
  int is_ok = 1;

  if (!s || !buffer) {
    is_ok = 0;
    goto finish;
  }

  BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)quality);
  BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)lgwin);
  if (dictionary_path != NULL) {
    size_t dictionary_size = 0;
    uint8_t* dictionary = ReadDictionary(dictionary_path, &dictionary_size);
    BrotliEncoderSetCustomDictionary(s, dictionary_size, dictionary);
    free(dictionary);
  }

  while (1) {
    if (available_in == 0 && !is_eof) {
      available_in = fread(input, 1, kFileBufferSize, fin);
      next_in = input;
      if (ferror(fin)) break;
      is_eof = feof(fin);
    }

    if (!BrotliEncoderCompressStream(s,
        is_eof ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS,
        &available_in, &next_in, &available_out, &next_out, NULL)) {
      is_ok = 0;
      break;
    }

    if (available_out != kFileBufferSize) {
      size_t out_size = kFileBufferSize - available_out;
      fwrite(output, 1, out_size, fout);
      if (ferror(fout)) break;
      available_out = kFileBufferSize;
      next_out = output;
    }

    if (BrotliEncoderIsFinished(s)) break;
  }

finish:
  free(buffer);
  BrotliEncoderDestroyInstance(s);

  if (!is_ok) {
    /* Should detect OOM? */
    fprintf(stderr, "failed to compress data\n");
    return 0;
  } else if (ferror(fout)) {
    fprintf(stderr, "failed to write output\n");
    return 0;
  } else if (ferror(fin)) {
    fprintf(stderr, "failed to read input\n");
    return 0;
  }
  return 1;
}

int main(int argc, char** argv) {
  char *input_path = 0;
  char *output_path = 0;
  char *dictionary_path = 0;
  int force = 0;
  int quality = 11;
  int decompress = 0;
  int repeat = 1;
  int verbose = 0;
  int lgwin = 0;
  clock_t clock_start;
  int i;
  ParseArgv(argc, argv, &input_path, &output_path, &dictionary_path, &force,
            &quality, &decompress, &repeat, &verbose, &lgwin);
  clock_start = clock();
  for (i = 0; i < repeat; ++i) {
    FILE* fin = OpenInputFile(input_path);
    FILE* fout = OpenOutputFile(output_path, force || repeat);
    int is_ok = 0;
    if (decompress) {
      is_ok = Decompress(fin, fout, dictionary_path);
    } else {
      is_ok = Compress(quality, lgwin, fin, fout, dictionary_path);
    }
    if (!is_ok) {
      unlink(output_path);
      exit(1);
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
    clock_t clock_end = clock();
    double duration = (double)(clock_end - clock_start) / CLOCKS_PER_SEC;
    int64_t uncompressed_size;
    double uncompressed_bytes_in_MB;
    if (duration < 1e-9) {
      duration = 1e-9;
    }
    uncompressed_size = FileSize(decompress ? output_path : input_path);
    if (uncompressed_size == -1) {
      fprintf(stderr, "failed to determine uncompressed file size\n");
      exit(1);
    }
    uncompressed_bytes_in_MB =
        (double)(repeat * uncompressed_size) / (1024.0 * 1024.0);
    if (decompress) {
      printf("Brotli decompression speed: ");
    } else {
      printf("Brotli compression speed: ");
    }
    printf("%g MB/s\n", uncompressed_bytes_in_MB / duration);
  }
  return 0;
}
