/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Literal cost model to allow backward reference replacement to be efficient.

#include "./literal_cost.h"

#include <math.h>
#include <algorithm>

#include "./fast_log.h"
#include "./types.h"
#include "./utf8_util.h"

namespace brotli {

static int UTF8Position(int last, int c, int clamp) {
  if (c < 128) {
    return 0;  // Next one is the 'Byte 1' again.
  } else if (c >= 192) {
    return std::min(1, clamp);  // Next one is the 'Byte 2' of utf-8 encoding.
  } else {
    // Let's decide over the last byte if this ends the sequence.
    if (last < 0xe0) {
      return 0;  // Completed two or three byte coding.
    } else {
      return std::min(2, clamp);  // Next one is the 'Byte 3' of utf-8 encoding.
    }
  }
}

static int DecideMultiByteStatsLevel(size_t pos, size_t len, size_t mask,
                                     const uint8_t *data) {
  int counts[3] = { 0 };
  int max_utf8 = 1;  // should be 2, but 1 compresses better.
  int last_c = 0;
  int utf8_pos = 0;
  for (size_t i = 0; i < len; ++i) {
    int c = data[(pos + i) & mask];
    utf8_pos = UTF8Position(last_c, c, 2);
    ++counts[utf8_pos];
    last_c = c;
  }
  if (counts[2] < 500) {
    max_utf8 = 1;
  }
  if (counts[1] + counts[2] < 25) {
    max_utf8 = 0;
  }
  return max_utf8;
}

void EstimateBitCostsForLiteralsUTF8(size_t pos, size_t len, size_t mask,
                                     const uint8_t *data, float *cost) {

  // max_utf8 is 0 (normal ascii single byte modeling),
  // 1 (for 2-byte utf-8 modeling), or 2 (for 3-byte utf-8 modeling).
  const int max_utf8 = DecideMultiByteStatsLevel(pos, len, mask, data);
  int histogram[3][256] = { { 0 } };
  int window_half = 495;
  int in_window = std::min(window_half, static_cast<int>(len));
  int in_window_utf8[3] = { 0 };

  // Bootstrap histograms.
  int last_c = 0;
  int utf8_pos = 0;
  for (int i = 0; i < in_window; ++i) {
    int c = data[(pos + i) & mask];
    ++histogram[utf8_pos][c];
    ++in_window_utf8[utf8_pos];
    utf8_pos = UTF8Position(last_c, c, max_utf8);
    last_c = c;
  }

  // Compute bit costs with sliding window.
  for (int i = 0; i < static_cast<int>(len); ++i) {
    if (i - window_half >= 0) {
      // Remove a byte in the past.
      int c = (i - window_half - 1) < 0 ?
          0 : data[(pos + i - window_half - 1) & mask];
      int last_c = (i - window_half - 2) < 0 ?
          0 : data[(pos + i - window_half - 2) & mask];
      int utf8_pos2 = UTF8Position(last_c, c, max_utf8);
      --histogram[utf8_pos2][data[(pos + i - window_half) & mask]];
      --in_window_utf8[utf8_pos2];
    }
    if (i + window_half < static_cast<int>(len)) {
      // Add a byte in the future.
      int c = data[(pos + i + window_half - 1) & mask];
      int last_c = data[(pos + i + window_half - 2) & mask];
      int utf8_pos2 = UTF8Position(last_c, c, max_utf8);
      ++histogram[utf8_pos2][data[(pos + i + window_half) & mask]];
      ++in_window_utf8[utf8_pos2];
    }
    int c = i < 1 ? 0 : data[(pos + i - 1) & mask];
    int last_c = i < 2 ? 0 : data[(pos + i - 2) & mask];
    int utf8_pos = UTF8Position(last_c, c, max_utf8);
    size_t masked_pos = (pos + i) & mask;
    int histo = histogram[utf8_pos][data[masked_pos]];
    if (histo == 0) {
      histo = 1;
    }
    double lit_cost = FastLog2(in_window_utf8[utf8_pos]) - FastLog2(histo);
    lit_cost += 0.02905;
    if (lit_cost < 1.0) {
      lit_cost *= 0.5;
      lit_cost += 0.5;
    }
    // Make the first bytes more expensive -- seems to help, not sure why.
    // Perhaps because the entropy source is changing its properties
    // rapidly in the beginning of the file, perhaps because the beginning
    // of the data is a statistical "anomaly".
    if (i < 2000) {
      lit_cost += 0.7 - ((2000 - i) / 2000.0 * 0.35);
    }
    cost[i] = static_cast<float>(lit_cost);
  }
}

void EstimateBitCostsForLiterals(size_t pos, size_t len, size_t mask,
                                 const uint8_t *data, float *cost) {
  if (IsMostlyUTF8(data, pos, mask, len, kMinUTF8Ratio)) {
    EstimateBitCostsForLiteralsUTF8(pos, len, mask, data, cost);
    return;
  }
  int histogram[256] = { 0 };
  int window_half = 2000;
  int in_window = std::min(window_half, static_cast<int>(len));

  // Bootstrap histogram.
  for (int i = 0; i < in_window; ++i) {
    ++histogram[data[(pos + i) & mask]];
  }

  // Compute bit costs with sliding window.
  for (int i = 0; i < static_cast<int>(len); ++i) {
    if (i - window_half >= 0) {
      // Remove a byte in the past.
      --histogram[data[(pos + i - window_half) & mask]];
      --in_window;
    }
    if (i + window_half < static_cast<int>(len)) {
      // Add a byte in the future.
      ++histogram[data[(pos + i + window_half) & mask]];
      ++in_window;
    }
    int histo = histogram[data[(pos + i) & mask]];
    if (histo == 0) {
      histo = 1;
    }
    double lit_cost = FastLog2(in_window) - FastLog2(histo);
    lit_cost += 0.029;
    if (lit_cost < 1.0) {
      lit_cost *= 0.5;
      lit_cost += 0.5;
    }
    cost[i] = static_cast<float>(lit_cost);
  }
}


}  // namespace brotli
