/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions for clustering similar histograms together. */

#ifndef BROTLI_ENC_CLUSTER_H_
#define BROTLI_ENC_CLUSTER_H_

#include "../common/platform.h"
#include <brotli/types.h>
#ifdef __VMS
#include "histogram.h"
#else
#include "./histogram.h"
#endif
#ifdef __VMS
#include "memory.h"
#else
#include "./memory.h"
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

typedef struct HistogramPair {
  uint32_t idx1;
  uint32_t idx2;
  double cost_combo;
  double cost_diff;
} HistogramPair;

#define CODE(X) /* Declaration */;

#define FN(X) X ## Literal
#ifdef __VMS
#include "cluster_inc.h"  /* NOLINT(build/include) */
#else
#include "./cluster_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#define FN(X) X ## Command
#ifdef __VMS
#include "cluster_inc.h"  /* NOLINT(build/include) */
#else
#include "./cluster_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#define FN(X) X ## Distance
#ifdef __VMS
#include "cluster_inc.h"  /* NOLINT(build/include) */
#else
#include "./cluster_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#undef CODE

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_CLUSTER_H_ */
