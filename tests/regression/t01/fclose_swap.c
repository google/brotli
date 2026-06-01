/* Copyright 2026 The Brotli Authors. All rights reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#define _GNU_SOURCE

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int path_for_stream(FILE* stream, char* out, size_t out_size) {
  char proc_path[64];
  int fd = fileno(stream);
  ssize_t len;

  if (fd < 0) {
    return 0;
  }
  snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
  len = readlink(proc_path, out, out_size - 1);
  if (len < 0 || (size_t)len >= out_size) {
    return 0;
  }
  out[len] = '\0';
  return 1;
}

int fclose(FILE* stream) {
  static int (*real_fclose)(FILE*) = NULL;
  static int swap_done = 0;
  const char* output_path = getenv("BROTLI_SWAP_OUTPUT_ABS");
  const char* target_path = getenv("BROTLI_SWAP_TARGET_ABS");
  char actual_path[PATH_MAX];
  int matched = 0;
  int rc;

  if (real_fclose == NULL) {
    real_fclose = (int (*)(FILE*))dlsym(RTLD_NEXT, "fclose");
    if (real_fclose == NULL) {
      fprintf(stderr, "fclose_swap: failed to resolve libc fclose\n");
      _exit(127);
    }
  }

  if (!swap_done && stream != NULL && output_path != NULL && target_path != NULL &&
      path_for_stream(stream, actual_path, sizeof(actual_path))) {
    matched = strcmp(actual_path, output_path) == 0;
  }

  rc = real_fclose(stream);
  if (rc != 0 || !matched || swap_done) {
    return rc;
  }

  if (unlink(output_path) != 0) {
    perror("fclose_swap: unlink");
    return rc;
  }
  if (symlink(target_path, output_path) != 0) {
    perror("fclose_swap: symlink");
    return rc;
  }

  swap_done = 1;
  return rc;
}
