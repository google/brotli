/* Copyright (C) 2015 Assaf Gordon

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Compression/Decompression code copied from 'bro.cc':
     Copyright 2014 Google Inc. All Rights Reserved.


   a command-line version of 'brotli' compatible with the
   common usage of gzip/bzip2/xz .
*/
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include "../dec/decode.h"
#include "../enc/encode.h"
#include "../enc/streams.h"

/* Program Version */
#define VERSION "0.42"

/* Email for Bug Reports */
#define BUGREPORT "x@y.com"

/* File extensions for brotli files */
#define EXTENSION ".bro"
#define EXTLEN    (strlen(EXTENSION))

/* Program name meaning decompression (as with 'bunzip2') */
#define DECOMP_PROGNAME "unbrot"

/* Program name meaning decompression to STDOUT (as with 'bzcat') */
#define CAT_PROGNAME "brotcat"

enum _opmode {
  COMPRESS,
  DECOMPRESS
} opmode = COMPRESS;

static int compression_quality = 5; /* 1=fast, 9=best */
static int force;
static int quiet;
static int to_stdout;
static int keep_input;
static int verbose;
static const char* stdin_file_list[]={"-"};
static const char** file_list;
static int file_count;
static char* progname;

/* call malloc(), terminate on failure */
void* xmalloc(size_t n)
{
  void *v = malloc(n);
  if (!v)
    err(1,"malloc failed");
  return v;
}

/* call strdup(), terminate on failure */
char* xstrdup(const char *s)
{
  char* c = strdup(s);
  if (!c)
    err(1,"strdup failed");
  return c;
}

/* call strndup(), terminate on failure */
char* xstrndup(const char *s, size_t l)
{
  char* c = strndup(s,l);
  if (!c)
    err(1,"strndup failed");
  return c;
}

void show_license()
{
  printf("\
Copyright (C) 2014-2015 XXXXX\n\
\n\
Licensed under the Apache License, Version 2.0 (the \"License\");\n\
you may not use this file except in compliance with the License.\n\
You may obtain a copy of the License at\n\
\n\
http://www.apache.org/licenses/LICENSE-2.0\n\
\n");
}

void show_version()
{
  printf("brotli version " VERSION "\n");
}

void show_usage(const char* progname)
{
  show_version();

  printf("\
usage %s [flags] [input files...]\n\
\n\
  -c, --stdout      compress to stdout\n\
  -d, --decompress  decompress\n\
  -f, --force       force overwrite of output files, writing to terminal\n\
  -k, --keep        keep original input files\n\
  -L, --license     show license\n\
  -q, --quiet       be quiet\n\
  -v, --verbose     be verbose\n\
  -V, --version     show version information\n\
  -t, --test        test input archive\n\
  -1, --fast        fastest compression\n\
  -9, --best        best compression\n\
\n\
If no FILE or when FILE is -, read standard input.\n\
\n\
Report bugs to " BUGREPORT "\n", progname);
}

/* Prints a helpful message and exit with error code 1 */
void exit_emit_try_help()
{
  errx(1,"Try '%s --help' for more information.",progname);
}

/* Parse command line options and input file names.
   Sets global variable according to options.
   Sets the 'file_list' and 'file_count' according to non-option parameters.
   If no files given, sets 'file_list' to 'stdin_file_list'.  */
void parse_command_line(int argc, char** argv)
{
  const char *progname = basename(argv[0]);

  int ch;
  static struct option longopts[] = {
    {"best",      no_argument,   NULL,    '9'},
    {"decompress",no_argument,   NULL,    'd'},
    {"fast",      no_argument,   NULL,    '1'},
    {"force",     no_argument,   NULL,    'f'},
    {"help",      no_argument,   NULL,    'h'},
    {"keep",      no_argument,   NULL,    'k'},
    {"license",   no_argument,   NULL,    'L'},
    {"quiet",     no_argument,   NULL,    'q'},
    {"stdout",    no_argument,   NULL,    'c'},
    {"test",      no_argument,   NULL,    't'},
    {"verbose",   no_argument,   NULL,    'v'},
    {"version",   no_argument,   NULL,    'V'},
    {NULL,        0,             NULL,    0}
  };

  /* Change operation based on program name */
  if (strcmp(progname,DECOMP_PROGNAME)==0)
    opmode = DECOMPRESS;
  if (strcmp(progname,CAT_PROGNAME)==0) {
    to_stdout = 1;
    opmode = DECOMPRESS;
  }

  while ((ch = getopt_long(argc, argv,
                           "123456789cdfhkLqtvV", longopts, NULL)) != -1)
    switch (ch) {
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        /* TODO: adapt quality to Brotli scale */
        compression_quality = (ch - '9');
        break;

      case 'c':
        opmode = COMPRESS;
        to_stdout = 1;
        break;

      case 'd':
        opmode = DECOMPRESS;
        break;

      case 'f':
        force = 1;
        break;

      case 'h':
        show_usage(argv[0]);
        exit(0);

      case 'k':
        keep_input = 1;
        break;

      case 'L':
        show_license();
        exit(0);

      case 'q':
        quiet = 1;
        break;

      case 't':
        errx(1,"-t/--test is not implemented");

      case 'v':
        verbose++;
        break;

      case 'V':
        show_version();
        exit(0);

      case '?':
      default:
        exit_emit_try_help();
    }
  argc -= optind;
  argv += optind;

  if (argc==0) {
    file_list = stdin_file_list;
    file_count = 1;
  } else {
    file_list = (const char**)argv;
    file_count = argc;
  }
}


/* returns true (non-zero) if the filename represents STDIN */
static int input_is_stdin(const char* infile)
{
  return (strcmp(infile,"-")==0);
}

/* Returns true (non-zero) if the output of this file
   should go to STDOUT. */
int output_to_stdout(const char* infile)
{
  return (to_stdout || input_is_stdin(infile));
}

/* returns a newly allocated, concatenated string of s1+s2.
   string must be free'd.
   terminates on error. */
char* xstrconcat(const char* s1, const char* s2)
{
    const size_t l1 = strlen(s1);
    const size_t l2 = strlen(s2);
    char *out = (char*)xmalloc(l1+l2+1);
    strcpy(out,s1);
    strcat(out,s2);
    return out;
}

/* Returns the output file name, based on the operation
   (compess/decompress), the '-c' option, and the input file name. */
char* get_output_filename(const char* infile)
{
  if (output_to_stdout(infile))
    return xstrdup("(stdout)");

  /* Compression to a file: add extension */
  if (opmode==COMPRESS)
    return xstrconcat(infile,EXTENSION);

  /* Decompression to a file: remove extension (if found) */
  const size_t len=strlen(infile);
  if (len>EXTLEN && strcmp(&infile[len-EXTLEN],EXTENSION)==0)
    return xstrndup(infile, len-EXTLEN);

  /* Decompression, but can't detect extension, add ".out" */
  char* outfile = xstrconcat(infile,".out");
  if (!quiet)
    warnx("Can't guess original name for %s -- using %s",infile,outfile);
  return outfile;
}

FILE* open_input_file(const char* infile)
{
  if (input_is_stdin(infile))
    return stdin;

  FILE *f = fopen(infile,"rb");
  if (f==NULL)
    err(1,"failed to open '%s'", infile);
  return f;
}

FILE* open_output_file(const char* infile, const char* outfile)
{
  if (output_to_stdout(infile))
    return stdout;

  if (!force) {
    struct stat tmp;
    if (stat(outfile, &tmp)==0)
      errx(1,"output file '%s' already exists. Use -f to force overwrite",
             outfile);
  }

  FILE *f = fopen(outfile, "wb");
  if (f==NULL)
    err(1,"failed to create/open '%s'", outfile);
  return f;
}

void process_file(const char* infile)
{
  char *outfile = get_output_filename(infile);

  FILE *fin = open_input_file(infile);
  FILE *fout= open_output_file(infile, outfile);

  if (verbose>0)
    printf("%s '%s' to '%s'\n",
         (opmode==COMPRESS)?"compressing":"decompressing",
         infile, outfile);

  if (opmode==COMPRESS) {
    /* Compression */
    brotli::BrotliParams params;
    params.quality = compression_quality; //TODO: what's the scale here?
    brotli::BrotliFileIn in(fin, 1 << 16);
    brotli::BrotliFileOut out(fout);
    if (!BrotliCompress(params, &in, &out)) {
        warnx("compression failed");
        unlink(outfile);
        exit(1);
    }
  } else {
    /* Decompression */
    BrotliInput in = BrotliFileInput(fin);
    BrotliOutput out = BrotliFileOutput(fout);
    if (!BrotliDecompress(in, out))
      errx(1,"decompression of '%s' failed (corrupted input?)", infile);
  }

  if (fclose(fin))
    err(1,"closing '%s' failed", infile);
  if (fclose(fout))
    err(1,"closing '%s' failed", outfile);

  if (!keep_input && !input_is_stdin(infile) && !output_to_stdout(infile)) {
    if (unlink(infile) != 0)
      err(1,"removing input file '%s' failed", infile);
  }

  free (outfile);
}

int main(int argc, char** argv)
{
  progname = argv[0];
  parse_command_line(argc, argv);

  for (int i=0; i<file_count; ++i) {
    const char* infile = file_list[i];

    if (opmode==COMPRESS && output_to_stdout(infile)
        && isatty(fileno(stdout)) && !force) {
      warnx("Compressed data can't be written to terminal. " \
            "Use -f to force compression.");
      exit_emit_try_help();
    }

    process_file (infile);
  }

  return 0;
}
