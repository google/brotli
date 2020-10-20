/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions to estimate the bit cost of Huffman trees. */

#ifdef __VMS
#include "bit_cost.h"
#else
#include "./bit_cost.h"
#endif

#include "../common/constants.h"
#include "../common/platform.h"
#include <brotli/types.h>
#ifdef __VMS
#include "fast_log.h"
#else
#include "./fast_log.h"
#endif
#ifdef __VMS
#include "histogram.h"
#else
#include "./histogram.h"
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define FN(X) X ## Literal
#ifdef __VMS
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#else
#include "./bit_cost_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#define FN(X) X ## Command
#ifdef __VMS
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#else
#include "./bit_cost_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#define FN(X) X ## Distance
#ifdef __VMS
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#else
#include "./bit_cost_inc.h"  /* NOLINT(build/include) */
#endif
#undef FN

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
