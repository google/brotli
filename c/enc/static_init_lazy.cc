#include "third_party/brotli/enc/static_init.h"

void BrotliEncoderLazyStaticInit(void) {
  static bool ok = [](){
    BrotliEncoderLazyStaticInit();
    return true;
  }();
  if (!ok) __builtin_trap();
}
