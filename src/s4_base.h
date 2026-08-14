#pragma once
#include <windows.h>

// All the patch addresses in this project were reversed against the client whose
// ImageBase is 0x00E80000. An unpacked client keeps the exact same code but at a
// different base (e.g. 0x002A0000), so every hardcoded address has to be rebased
// or the patches write into whatever happens to be there.
//
// Verified against both exes: same SizeOfImage, same section sizes, and identical
// bytes at every one of the patch sites once shifted by the base delta.
//
// Keep the base cached: GetModuleHandleW hits the loader lock and S4 runs this
// from inside the hooks.
__declspec(selectany) uintptr_t g_s4_base = 0;

static inline uintptr_t s4_base() {
    if (!g_s4_base) g_s4_base = (uintptr_t)GetModuleHandleW(NULL);
    return g_s4_base;
}

#define S4(va) (s4_base() + ((va) - 0x00E80000u))
