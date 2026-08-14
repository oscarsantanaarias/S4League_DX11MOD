// NativeEngine -- sneoz.dll: native D3D11 render engine for S4 League.
// See ARCHITECTURE.md.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backend_d3d11.h"
#include "effects.h"
#include "log.h"
#include "s4_base.h"

namespace ne { void InstallStringIterGuard(); void InstallBackHook(); void InstallFontLockCrashFix(); void InstallHashIndexGuards(); void InstallFontMemsetGuard(); void InstallContainerGuard(); void InstallMatrixCopyGuard(); void InstallSafeMemcpyS(); void InstallComAssignGuard(); void InstallRenderTargetStackGuard(); void InstallPoolDestroyGuard(); }

// diagnostics: log where it crashes (module + offset + backtrace)
static void ModOf(void* addr, char* out, void** base) {
    HMODULE m = nullptr; *base = nullptr; out[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m) {
        *base = m; char p[MAX_PATH] = ""; GetModuleFileNameA(m, p, MAX_PATH);
        const char* s = p; for (const char* c = p; *c; ++c) if (*c == '\\') s = c + 1; lstrcpynA(out, s, 64);
    }
}
// A VEH sees EVERY first-chance exception, including the ones the game catches and
// handles without dying, and including the ones our own SEH guards raise on purpose.
// Logging all of them is not free: one run produced 3140 identical exceptions from
// the same PopRenderTarget, each writing 21 lines, each line opening and closing the
// log file. That is ~66k file operations and it starves the client (the run only got
// to 141 frames). We log the first few of each distinct address and then go quiet.
static LONG WINAPI Veh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000409 && code != 0xC0000005 && code != 0x80000003) return EXCEPTION_CONTINUE_SEARCH;
    void* addr = ep->ExceptionRecord->ExceptionAddress;

    static void* seen[16] = {}; static LONG hits[16] = {}; static int nseen = 0;
    static LONG total = 0;
    int slot = -1;
    for (int i = 0; i < nseen; ++i) if (seen[i] == addr) { slot = i; break; }
    if (slot < 0) { if (nseen >= 16) return EXCEPTION_CONTINUE_SEARCH; slot = nseen++; seen[slot] = addr; }
    LONG n = ++hits[slot];
    if (n > 3) {                      // same site, already reported
        if ((n % 1000) == 0) ne::Log("[VEH] %p seen %ld times (still firing)\n", addr, n);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ++total;

    char mn[64]; void* base;
    ModOf(addr, mn, &base);
    ne::Log("[VEH] code=0x%08X addr=%p mod=%s +0x%X (hit %ld)\n", code, addr, mn, base ? (int)((char*)addr - (char*)base) : 0, n);
    void* bt[20]; USHORT c = RtlCaptureStackBackTrace(0, 20, bt, nullptr);
    for (USHORT i = 0; i < c; ++i) { char m2[64]; void* b2; ModOf(bt[i], m2, &b2); ne::Log("[VEH]   %p %s +0x%X\n", bt[i], m2, b2 ? (int)((char*)bt[i] - (char*)b2) : 0); }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Memory Jump Fix (from s4fixes): FUN_01aec1a0, the negative-value branch. When the
// value goes negative the block stores it as-is in the struct and the caller later
// uses it as a size. Forcing the jump to unconditional means that branch never runs.
// JNS(0x79) -> JMP(0xEB).
//
// Idempotent: it only writes if the byte is still the conditional, so applying it
// twice (or on an exe already patched by hand) does nothing.
static void InstallMemoryJumpFix() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    BYTE* site = (BYTE*)S4(0x01AEC298);
    DWORD old;
    if (*site == 0xEB) { ne::Log("[ne] MemoryJumpFix already applied at %p\n", site); return; }
    if (*site != 0x79) { ne::Log("[ne] MemoryJumpFix NOT applied: %p holds 0x%02X\n", site, *site); return; }
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
    ne::Log("[ne] MemoryJumpFix applied at %p\n", site);
}

static DWORD WINAPI InitThread(LPVOID) {
    ne::Log("[ne] NativeEngine init\n");
    InstallMemoryJumpFix();
    ne::InstallStringIterGuard();
    ne::InstallFontLockCrashFix();
    ne::InstallHashIndexGuards();
    ne::InstallFontMemsetGuard();
    ne::InstallContainerGuard();
    ne::InstallMatrixCopyGuard();
    ne::InstallSafeMemcpyS();
    ne::InstallComAssignGuard();
    ne::InstallRenderTargetStackGuard();
    ne::InstallPoolDestroyGuard();
    // ne::InstallBackHook();  // the 5-byte trampoline split an instruction -> crash on startup
    AddVectoredExceptionHandler(1, Veh);
    AddVectoredContinueHandler(1, Veh);   // a 0xC0000409 fastfail does not always go through the normal VEH
    // 1) native backend: Direct3DCreate9 -> real D3D11 device.
    InstallNativeD3D9();
    // 2) effect capture (D3DXCreateEffect) for PHASE 2.
    ne::InstallEffectsHook();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // S4 multi-client (see the hooks-dllmain-load-mutex note)
       // LoadLibraryA("mutex.dll");
        // fixes.dll: client patches (heap_fixes, etc). It only redirects D3D9 if it finds
        // vulkan-1.dll; without Vulkan it stays on "native D3D9" and does not compete with our hook.
       // LoadLibraryA("fixes.dll");
       //LoadLibraryA("crash_catch.dll"); // optional: the user's crash dll (if present)
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
