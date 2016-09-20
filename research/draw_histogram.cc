/* Copyright 2016 Google Inc. All Rights Reserved.
   Author: vanickulin@google.com (Ivan Nikulin)

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Backward reference visualization tool. Accepts file with backward references
   as an input and produces PGM image with histogram of those references. */

#include <algorithm> /* min */
#include <cassert>
#include <cstring> /* memset */
#include <cmath> /* log, round */
#include <cstdio> /* fscanf, fprintf */
#include <cstdint>

#include "./read_dist.h"

const int FLAGS_height = 1000;  // Height of the resulting histogam.
const int FLAGS_width = 1000;  // Width of the resulting histogam.
int FLAGS_size;  // Size of the compressed file.
const int FLAGS_brotli_window = 0;  // Size of brotli window in bits.
const uint64_t FLAGS_min_distance = 0;  // Minimum distance.
uint64_t FLAGS_max_distance = 0;  // Maximum distance.
const bool FLAGS_with_copies = false;  // True if input contains copy length.
const bool FLAGS_simple = false;  // True if using only black and white pixels.
const bool FLAGS_linear = true;  // True if using linear distance mapping.
const uint64_t FLAGS_skip = 0;  // Number of bytes to skip.

inline double DistanceTransform(double x) {
  static bool linear = FLAGS_linear;
  if (linear) {
    return x;
  } else {
    /* Using log^2 scale because log scale produces big white gap at the bottom
       of image. */
    return log(x) * log(x);
  }
}

/* Mapping pixel density on arc function to increase contrast. */
inline double DensityTransform(double x) {
  double z = 255 - x;
  return sqrt(255 * 255 - z * z);
}

inline int GetMaxDistance() {
  return FLAGS_max_distance;
}

void AdjustPosition(int* pos) {
  static uint32_t offset = 0;
  static int last = 0;
  static uint32_t window_size = (1 << FLAGS_brotli_window);
  assert(*pos >= 0 && *pos < window_size);
  if (*pos < last) {
    offset += window_size;
  }
  last = *pos;
  *pos += offset;
}

void BuildHistogram(FILE* fin, int** histo) {
  int height = FLAGS_height;
  int width = FLAGS_width;
  int skip = FLAGS_skip;
  size_t min_distance = FLAGS_min_distance;

  printf("height = %d, width = %d\n", height, width);

  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      histo[i][j] = 0;
    }
  }

  int max_pos = FLAGS_size - skip;
  double min_dist = min_distance > 0 ? DistanceTransform(min_distance) : 0;
  double max_dist = DistanceTransform(GetMaxDistance()) - min_dist;
  int copy, pos, distance, x, y;
  double dist;
  while (ReadBackwardReference(fin, &copy, &pos, &distance)) {
    if (pos == -1) continue;  // In case when only insert is present.
    if (distance < min_distance || distance >= GetMaxDistance()) continue;
    if (FLAGS_brotli_window != 0) {
      AdjustPosition(&pos);
    }
    if (pos >= skip && distance <= pos) {
      pos -= skip;
      if (pos >= max_pos) break;
      dist = DistanceTransform(static_cast<double>(distance)) - min_dist;

      x = std::min(static_cast<int>(round(dist / max_dist * height)),
                   height - 1);
      y = 1ul * pos * width / max_pos;
      if (!(y >= 0 && y < width)) {
        printf("pos = %d, max_pos = %d, y = %d\n", pos, max_pos, y);
        assert(y >= 0 && y < width);
      }

      if (FLAGS_with_copies) {
        int right = 1ul * (pos + copy - 1) * width / max_pos;
        if (right < 0) {
          printf("pos = %d, distance = %d, copy = %d, y = %d, right = %d\n",
                  pos, distance, copy, y, right);
          assert(right >= 0);
        }
        if (y == right) {
          histo[x][y] += copy;
        } else {
          int pos2 = static_cast<int>(ceil(1.0 * (y + 1) * max_pos / width));
          histo[x][y] += pos2 - pos;
          for (int i = y + 1; i < right && i < width; ++i) {
            histo[x][i] += max_pos / width;  // Sometimes 1 more, but who cares.
          }
          // Make sure the match doesn't go beyond the image.
          if (right < width) {
            pos2 = static_cast<int>(ceil(1.0 * right * max_pos / width));
            histo[x][right] += pos + copy - 1 - pos2 + 1;
          }
        }
      } else {
        histo[x][y]++;
      }
    }
  }
}

void ConvertToPixels(int** histo, uint8_t** pixel) {
  int height = FLAGS_height;
  int width = FLAGS_width;

  int maxs = 0;
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      if (maxs < histo[i][j]) maxs = histo[i][j];
    }
  }

  bool simple = FLAGS_simple;
  double max_histo = static_cast<double>(maxs);
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      if (simple) {
        pixel[i][j] = histo[i][j] > 0 ? 0 : 255;
      } else {
        pixel[i][j] = static_cast<uint8_t>(
            255 - DensityTransform(histo[i][j] / max_histo * 255));
      }
    }
  }
}

void DrawPixels(uint8_t** pixel, FILE* fout) {
  int height = FLAGS_height;
  int width = FLAGS_width;

  fprintf(fout, "P5\n%d %d\n255\n", width, height);
  for (int i = height - 1; i >= 0; i--) {
    fwrite(pixel[i], 1, width, fout);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    printf("usage: draw_histogram.cc dist_file input_size output_file\n");
    return 1;
  }

  int height = FLAGS_height;
  int width = FLAGS_width;

  FILE* fin = fopen(argv[1], "r");
  FILE* fout = fopen(argv[3], "wb");

  FLAGS_size = atoi(argv[2]);

  if (FLAGS_max_distance == 0) FLAGS_max_distance = FLAGS_size;

  uint8_t** pixel = new uint8_t*[height];
  int** histo = new int*[height];
  for (int i = 0; i < height; i++) {
    pixel[i] = new uint8_t[width];
    histo[i] = new int[width];
  }

  BuildHistogram(fin, histo);
  fclose(fin);

  ConvertToPixels(histo, pixel);

  DrawPixels(pixel, fout);
  fclose(fout);

  return 0;
}
