#include "oracle.hpp"

#include <dlfcn.h>
#include <link.h>

#include <cstdio>
#include <cstdlib>

namespace oracle {
void* At(uint64_t ghidraAddr) {
  static uintptr_t base = [] {
    void* h = dlopen(LH_ORIGINAL_SO, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
      fprintf(stderr, "oracle: dlopen failed: %s\n", dlerror());
      abort();
    }
    link_map* lm = nullptr;
    dlinfo(h, RTLD_DI_LINKMAP, &lm);
    return uintptr_t(lm->l_addr);
  }();
  return reinterpret_cast<void*>(base + ghidraAddr - 0x100000);
}
}  // namespace oracle
