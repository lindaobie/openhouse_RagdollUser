// Loads the original driver_lighthouse.so and exposes internal functions by
// address, so ported math can be compared against the real implementation.
// Addresses are Ghidra addresses (ELF VA + 0x100000).
#pragma once
#include <cstdint>

namespace oracle {
// Returns the address of a function/data given its Ghidra address; aborts if the
// library cannot be loaded.
void* At(uint64_t ghidraAddr);
template <typename F>
F Fn(uint64_t ghidraAddr) { return reinterpret_cast<F>(At(ghidraAddr)); }
}  // namespace oracle
