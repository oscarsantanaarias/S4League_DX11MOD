// guards.cpp -- targeted mitigations for the game's crashes.
//
// FUN_011bb0f0 (0x011BB137 = where it crashes) is the constructor of
// _String_const_iterator: it copies the "container proxy" of a std::wstring.
//
//   this[0] = 0; this[1] = 0;
//   A = *param_1;              // proxy
//   B = *(void**)A;            // container
//   if (A && B) this[0] = *B;  // <-- crashes here
//
// It validates A != 0 and B != 0, but NOT that B is a VALID pointer: in practice
// B arrives as 1 (reused memory) -> reads at address 1 -> AV.
// We replace it with a version that also checks that B is readable.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "log.h"
#include "s4_base.h"
#include "backend_shared.h"   // NE_BackbufferSurface, for the render target stack guard
#include <string.h>

namespace ne {

// ponytail: VirtualQuery is a syscall (~1us). These guards sit in the string
// iterator ctor and in the _Getcont of EVERY container: tens of thousands of
// calls per frame -> the game crawled. Cheap check + SEH over the real deref:
// when the pointer is valid it costs nothing (same approach as Safe_MemcpyS
// below). Readable() stays for the cold paths.
#define NE_LOW(p) (!(p) || (uintptr_t)(p) < 0x10000)   // garbage pointers like 1

static bool Readable(const void* p) {
    if (NE_LOW(p)) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
    return (mbi.Protect & bad) == 0;
}

static LONG g_blocked = 0;

// ponytail: approximate counter (no Interlocked on purpose: it is diagnostics and
// the bus lock shows up at this rate). Good enough for the order of magnitude.
volatile LONG g_guardCalls = 0;
LONG GuardCalls() { return g_guardCalls; }

// Reads an MSVC std::wstring (SSO): [union buf[8] wchar / ptr][size][capacity].
// If capacity >= 8 the text lives behind the pointer; otherwise it is inline in the buffer.
static void DumpWString(const void* strObj, const char* tag) {
    if (!Readable(strObj)) { Log("[str] %s: unreadable object %p\n", tag, strObj); return; }
    const unsigned* s = (const unsigned*)strObj;
    unsigned size = s[4], cap = s[5];
    const wchar_t* txt = (cap >= 8) ? *(const wchar_t**)strObj : (const wchar_t*)strObj;
    char out[160]; int n = 0;
    if (size <= 128 && Readable(txt)) {
        for (unsigned i = 0; i < size && n < 150; ++i) {
            wchar_t c = txt[i];
            out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
        }
    }
    out[n] = 0;
    Log("[str] %s: size=%u cap=%u ptr=%p txt=\"%s\"\n", tag, size, cap, txt, out);
}

// original __thiscall: this in ECX, param_1 on the stack -> __fastcall(ecx, edx, arg)
static void* __fastcall Safe_StringIterCtor(void* thisp, void* /*edx*/, void** param_1) {
    unsigned* out = (unsigned*)thisp;
    g_guardCalls++;
    if (!out) return thisp;
    out[0] = 0;
    out[1] = 0;
    if (NE_LOW(param_1)) return thisp;
    __try {
        void* A = *param_1;
        if (!NE_LOW(A)) {
            void* B = *(void**)A;
            if (!NE_LOW(B)) out[0] = *(unsigned*)B;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        if (InterlockedIncrement(&g_blocked) <= 5)
            Log("[guard] string iterator: invalid container -> crash avoided\n");
    }
    return thisp;
}

// --- hook for std::wstring::back() (0x01DE4250) ---
// The game calls .back() on a wstring; if it is EMPTY that is UB and ends up
// crashing. We log the contents + who called, to see which text it is.
typedef wchar_t* (__fastcall* PFN_Back)(void* thisp, void* edx);
static PFN_Back g_realBack = nullptr;
static BYTE g_backTramp[16];

static wchar_t* __fastcall Hook_Back(void* thisp, void* edx) {
    static LONG n = 0;
    LONG i = InterlockedIncrement(&n);
    const unsigned* s = (const unsigned*)thisp;
    unsigned size = Readable(thisp) ? s[4] : 0xFFFFFFFF;
    if (size == 0 || size == 0xFFFFFFFF || i <= 30) {
        char tag[64]; wsprintfA(tag, "back#%ld%s", i, size == 0 ? " EMPTY!" : "");
        DumpWString(thisp, tag);
        void* bt[8]; USHORT c = RtlCaptureStackBackTrace(1, 8, bt, nullptr);
        char line[256]; int p = 0;
        for (USHORT k = 0; k < c && p < 200; ++k) p += wsprintfA(line + p, "%p ", bt[k]);
        line[p] = 0;
        Log("[str]   callers: %s\n", line);
    }
    if (size == 0 || size == 0xFFFFFFFF) { static wchar_t z = 0; return &z; } // empty: return something valid
    return g_realBack(thisp, edx);
}

void InstallBackHook() {
    BYTE* target = (BYTE*)S4(0x01DE4250);
    DWORD old;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return;
    // trampoline: copy 5 bytes + a JMP back (assuming they do not split an instruction)
    memcpy(g_backTramp, target, 5);
    g_backTramp[5] = 0xE9;
    *(int*)(g_backTramp + 6) = (int)((intptr_t)(target + 5) - ((intptr_t)g_backTramp + 5 + 5));
    DWORD o2; VirtualProtect(g_backTramp, sizeof(g_backTramp), PAGE_EXECUTE_READWRITE, &o2);
    g_realBack = (PFN_Back)(void*)&g_backTramp[0];
    intptr_t rel = (intptr_t)&Hook_Back - ((intptr_t)target + 5);
    target[0] = 0xE9; *(int*)(target + 1) = (int)rel;
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    Log("[ne] BackHook installed at %p\n", target);
}

// CFont FreeType::WriteT (FUN_01df08b0): if the atlas Lock fails, the game LOGS
// the error and enters the memset loop ANYWAY, writing at (NULL + offset) -> crash.
//   01df0a59  JNZ 01df0a6e    ; ptr != 0 -> loop (the good path)
//   01df0a5b  PUSH "ERROR..." ; failure path, falls into the loop anyway  <-- we divert this
//   01df0a6e  memset loop
//   01df0ab5  after the loop
// Patch: at 01df0a5b, JMP 01df0ab5 (skip the loop and continue to unlock/cleanup).
void InstallFontLockCrashFix() {
    BYTE* site = (BYTE*)S4(0x01DF0A5B);
    const BYTE* target = (const BYTE*)S4(0x01DF0AB5);
    DWORD old;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    bool ok = (*site == 0x68); // PUSH imm32 (the push of the error string)
    if (ok) { site[0] = 0xE9; *(int*)(site + 1) = (int)(target - (site + 5)); }
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("[ne] FontLockCrashFix applied=%d\n", ok);
}

// Bucket indices of the game's hash maps. They arrive with a garbage `this`
// (value 1) from the same container system as the string iterator crash:
//   FUN_01cae4f0: return param_1 >> 2 & *(int*)(this+8) - 1;
//   FUN_01c80ba0: return *(int*)(this+8) - 1 & param_1;
// Without validating `this` they read at 0x8/0x9 and crash. We return bucket 0
// (the lookup fails cleanly) instead of blowing up.
static unsigned __fastcall Safe_HashIndexShift(void* thisp, void*, unsigned key) {
    if (NE_LOW(thisp)) return 0;
    __try { unsigned cap = *(unsigned*)((char*)thisp + 8); return cap ? ((key >> 2) & (cap - 1)) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static unsigned __fastcall Safe_HashIndexPlain(void* thisp, void*, unsigned key) {
    if (NE_LOW(thisp)) return 0;
    __try { unsigned cap = *(unsigned*)((char*)thisp + 8); return cap ? ((cap - 1) & key) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// `expect` = the first 3 bytes of the original prologue. Without this check a wrong
// address (another build, a bad rebase) gets a JMP written into the middle of some
// unrelated function and the client dies somewhere else entirely. Failing here and
// logging it is always better than corrupting code.
static bool JmpPatch(void* site, void* fn, const char* name, const char* expect) {
    BYTE* p = (BYTE*)site;
    if (memcmp(p, expect, 3) != 0) {
        Log("[ne] %s NOT patched: %p holds %02X %02X %02X, expected %02X %02X %02X\n",
            name, p, p[0], p[1], p[2], (BYTE)expect[0], (BYTE)expect[1], (BYTE)expect[2]);
        return false;
    }
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    p[0] = 0xE9; *(int*)(p + 1) = (int)((BYTE*)fn - (p + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("[ne] %s patched at %p\n", name, p);
    return true;
}
void InstallHashIndexGuards() {
    JmpPatch((void*)S4(0x01CAE4F0), (void*)&Safe_HashIndexShift, "HashIndexShift", "\x55\x8B\xEC");
    JmpPatch((void*)S4(0x01C80BA0), (void*)&Safe_HashIndexPlain, "HashIndexPlain", "\x55\x8B\xEC");
}

// The game's own check (`ptr == 0`) is not enough: the atlas Lock returns GARBAGE
// but non-null pointers (0x203, 0x4B06...), they pass the check and the memset
// writes there. We intercept the memset of the font loop and drop invalid targets.
static void* __cdecl Safe_Memset(void* dst, int c, size_t n) {
    if ((uintptr_t)dst < 0x10000) return dst; // garbage pointer: do not write
    return memset(dst, c, n);
}
// The client's memcpy_s, replaced GLOBALLY in its IAT slot.
// The CRT version KILLS the process (invalid parameter handler -> `mov [0],0`)
// when a pointer is invalid. The client uses it in several places with garbage
// pointers (font atlas, matrix copies...). Ours drops the copy and carries on,
// which is what one would expect from a "safe" function.
// NOTE: do NOT reimplement the function (reimplementing it broke the glyph copy
// and the letters disappeared). We only intercept: if the pointers are valid we
// call the ORIGINAL as-is; if not, we drop the copy and continue.
typedef int(__cdecl* PFN_MemcpyS)(void*, size_t, const void*, size_t);
static PFN_MemcpyS g_realMemcpyS = nullptr;
// Watch the cost: memcpy_s is called thousands of times per frame. Validating the
// range with VirtualQuery on every call kept the game from finishing loading.
// Cheap check + SEH: if the access blows up we catch it, and when everything is
// fine it costs nothing.
static int __cdecl Safe_MemcpyS(void* dst, size_t dstSize, const void* src, size_t count) {
    if ((uintptr_t)dst < 0x10000 || (uintptr_t)src < 0x10000) return 0;
    if (!g_realMemcpyS) return 0;
    __try { return g_realMemcpyS(dst, dstSize, src, count); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void InstallSafeMemcpyS() {
    void** slot = (void**)S4(0x01F12804); // the IAT slot CFont::WriteT uses
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_realMemcpyS = (PFN_MemcpyS)*slot;
    *slot = (void*)&Safe_MemcpyS;
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("[ne] SafeMemcpyS installed (original=%p)\n", (void*)g_realMemcpyS);
}

void InstallFontMemsetGuard() {
    BYTE* call = (BYTE*)S4(0x01DF0AAB); // CALL memset inside the CFont::WriteT loop
    DWORD old;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    bool ok = (*call == 0xE8);
    if (ok) *(int*)(call + 1) = (int)((BYTE*)&Safe_Memset - (call + 5));
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    Log("[ne] FontMemsetGuard applied=%d\n", ok);
}

// ROOT of the container crash family: FUN_01ae88a0 returns the container from the
// proxy (`A = *param_1; if (A) return *A;`). It validates A != 0 but NOT what it
// returns: if `*A` is garbage (value 1), the caller does this[1] and blows up
// (e.g. FUN_01de3db0 at 01de3deb -> MOV EAX,[EDX+4]).
// Returning 0 is not enough: the callers still do this[1] and read at address 4.
// We return an EMPTY but VALID container, so any caller (there are several:
// FUN_01de3db0, FUN_01cbe470, ...) walks a real structure and gets 0 without crashing.
//   [1] = bucket table (zeros)   [2] = capacity 1  -> index is always 0
static unsigned g_dummyTable[64] = { 0 };
static unsigned g_dummyCont[8] = { 0 };
static void* DummyContainer() {
    if (!g_dummyCont[1]) { g_dummyCont[1] = (unsigned)(uintptr_t)g_dummyTable; g_dummyCont[2] = 1; }
    return g_dummyCont;
}
static void* __fastcall Safe_GetContainer(void** param_1, void*) {
    g_guardCalls++;
    if (NE_LOW(param_1)) return DummyContainer();
    __try {
        void* proxy = *param_1;
        if (NE_LOW(proxy)) return DummyContainer();
        void* cont = *(void**)proxy;
        if (NE_LOW(cont)) return DummyContainer();
        (void)*(volatile unsigned*)cont;   // let it blow up here and not in the caller
        return cont;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return DummyContainer(); }
}
// FUN_01de3db0 (crashes at 01de3deb: MOV EAX,[EDX+4] with a garbage EDX). Original:
//   this = FUN_01ae88a0(param_1);              // container from the proxy
//   idx  = FUN_01c80ba0(this, param_1[2]);     // bucket = (cap-1) & key
//   return *(this[1] + idx*4);                 // table[idx]
// (the asm's DIV by 1 always yields remainder 0, it adds nothing). 1:1 version with validation.
static unsigned __fastcall Safe_BucketLookup(int* param_1, void*) {
    if (NE_LOW(param_1)) return 0;
    __try {
        void* proxy = *(void**)param_1;
        if (NE_LOW(proxy)) return 0;
        unsigned* cont = *(unsigned**)proxy;
        if (NE_LOW(cont)) return 0;
        unsigned cap = cont[2];                       // this[2] = capacity
        unsigned idx = cap ? ((cap - 1) & (unsigned)param_1[2]) : 0;
        unsigned* table = (unsigned*)cont[1];         // this[1] = bucket table
        if (NE_LOW(table)) return 0;
        return table[idx];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// FUN_01b4c980: `memcpy_s(this, 0x40, param_1, 0x40)` = copies a 4x4 matrix (64 bytes).
// memcpy_s is the SAFE version: if a pointer is invalid it calls the CRT invalid
// parameter handler, which in this build is FUN_00f53690 -> `MOV [0],0` = a
// DELIBERATE abort (that is why crash_catch reports a WRITE to 0x00000000).
// We validate before copying: without valid pointers we do not copy and the trap never fires.
static void* __fastcall Safe_CopyMatrix(void* thisp, void*, const void* src) {
    if (NE_LOW(thisp) || NE_LOW(src)) return thisp;
    __try { memcpy(thisp, src, 0x40); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return thisp;
}
void InstallMatrixCopyGuard() {
    JmpPatch((void*)S4(0x01B4C980), (void*)&Safe_CopyMatrix, "MatrixCopyGuard", "\x55\x8B\xEC");
}

// FUN_01b36ab0: ctor of a COM smart pointer. It stores the object and AddRefs it:
//   *this = obj;  (**(code**)(**this + 4))();   // obj->vtable[1] = AddRef
// It validates nothing: objects arrive with the vtable at 0 (or garbage) and it
// blows up reading vtable+4. We validate object and vtable before calling.
static void* __fastcall Safe_ComAssign(void* thisp, void*, void* obj) {
    if (!thisp) return thisp;
    *(void**)thisp = obj;
    if (NE_LOW(obj)) return thisp;
    void* addRef = nullptr;
    __try { void** vt = *(void***)obj; if (!NE_LOW(vt)) addRef = vt[1]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { addRef = nullptr; }
    if (!NE_LOW(addRef))
        ((unsigned long(__fastcall*)(void*, void*))addRef)(obj, nullptr); // __thiscall: this in ECX
    return thisp;
}
// FUN_01b36b10: the Release of the same smart pointer:
//   (**(code**)(*(int*)*param_1 + 8))();   // obj->vtable[2] = Release
// When opening the inventory it arrives with the object ALREADY freed: the vtable
// read comes back as 2, so it jumps to [2+8] = 0xA. We validate object, vtable
// and pointer before calling.
static void __fastcall Safe_ComRelease(void** thisp, void*) {
    if (NE_LOW(thisp)) return;
    void* obj = nullptr; void* release = nullptr;
    __try {
        obj = *thisp;
        if (!NE_LOW(obj)) { void** vt = *(void***)obj; if (!NE_LOW(vt)) release = vt[2]; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { release = nullptr; }
    if (!NE_LOW(release))
        ((unsigned long(__fastcall*)(void*, void*))release)(obj, nullptr); // __thiscall: this in ECX
}

// FUN_01cae900: the front() of the render target std::deque
// (this[1]=block map, this[3]=offset, this[4]=count; empty() looks ONLY at
// this[4]). If the counter says there are elements but the block map got
// corrupted, front() returns garbage and PopRenderTarget blows up at 0x01D90450
// doing `mov edx,[eax]` (eax=0x0C was observed).
// It returns a POINTER TO POINTER. Returning the address of a zero was not enough:
// PopRenderTarget DEREFERENCES what it gets before using it, so a null there just
// moved the crash from "read 0x0C" to "read 0x00000000" at 0x01D9048C (seen in
// crash_catch, exceptions #1 and #2 of the same pop). We hand back the address of a
// pointer to the real backbuffer surface instead, which is also what a failed pop
// should restore. That pop is lost, not the match.
static void* g_nullSurface = nullptr;
static void* g_fallbackSurface = nullptr;   // holds the backbuffer surface pointer
static LONG g_dequeRescued = 0;
typedef void* (__fastcall* tFront)(void*, void*);
static tFront g_realFront = nullptr;
static BYTE g_frontTramp[16];
// The address of a pointer to a REAL surface. Only falls back to the null slot if
// the device does not exist yet, which would only happen before CreateDevice.
static void** FallbackFront() {
    if (!g_fallbackSurface) g_fallbackSurface = NE_BackbufferSurface();
    return g_fallbackSurface ? &g_fallbackSurface : &g_nullSurface;
}
static void* __fastcall Safe_DequeFront(void* thisp, void* unused) {
    if (!g_realFront || NE_LOW(thisp)) return FallbackFront();
    void* r = nullptr;
    __try { r = g_realFront(thisp, unused); (void)*(volatile unsigned*)r; }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (InterlockedIncrement(&g_dequeRescued) <= 5)
            Log("[ne] render target deque corrupted -> returning the backbuffer\n");
        return FallbackFront();
    }
    return r;
}
LONG DequeRescued() { return g_dequeRescued; }
void InstallRenderTargetStackGuard() {
    // The prologue is exactly 5 bytes (push ebp; mov ebp,esp; push -1), so the
    // trampoline does not split any instruction.
    BYTE* target = (BYTE*)S4(0x01CAE900);
    DWORD old;
    if (!VirtualProtect(target, 16, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(g_frontTramp, target, 5);
    g_frontTramp[5] = 0xE9;
    *(int*)(g_frontTramp + 6) = (int)((intptr_t)(target + 5) - ((intptr_t)g_frontTramp + 10));
    DWORD o2; VirtualProtect(g_frontTramp, sizeof(g_frontTramp), PAGE_EXECUTE_READWRITE, &o2);
    g_realFront = (tFront)(void*)&g_frontTramp[0];
    target[0] = 0xE9;
    *(int*)(target + 1) = (int)((intptr_t)&Safe_DequeFront - ((intptr_t)target + 5));
    VirtualProtect(target, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 16);
    Log("[ne] RenderTargetStackGuard installed at 01CAE900\n");
}

// FUN_00e98d00: destroys one element of a pool and frees it.
//   mov eax,[esi+0xC]   ; esi = the element ("this" comes in ESI, not ECX)
//   mov eax,[eax+8]     ; optional destructor slot
//   test eax,eax / mov ecx,[esi] / mov edi,[ecx+0x64]
//   je +6 / push esi / call eax     ; run the destructor
//   push esi / push edi / call FUN_00e98690   ; free
// Its caller (0x00E999E0) walks the whole pool: call this, then table[i] = 0,
// i++, while i < count. Coming back from a match to the lobby, one of those
// elements holds 1 instead of a pointer -> [1+0xC] = 0x0000000D -> AV.
// Same garbage-pointer family as the container and string iterator guards.
// If the element is not addressable we skip its destruction: that one free is
// lost, the client keeps running.
//
// It cannot be a normal C function because "this" arrives in ESI, so it is a
// naked stub. We overwrite 6 bytes (the first two instructions) and jump back
// past them; the return address is rebased at install time, hence the indirect
// jump through a variable.
static uintptr_t g_poolDestroyBack = 0;
static LONG g_poolDestroySkipped = 0;
static void __declspec(naked) Safe_PoolDestroy() {
    __asm {
        cmp esi, 0x10000
        jb  bail
        mov eax, dword ptr [esi + 0x0C]
        cmp eax, 0x10000
        jb  bail
        mov eax, dword ptr [eax + 8]
        jmp dword ptr [g_poolDestroyBack]
    bail:
        inc dword ptr [g_poolDestroySkipped]
        xor eax, eax
        ret
    }
}
void InstallPoolDestroyGuard() {
    BYTE* site = (BYTE*)S4(0x00E98D00);
    static const BYTE expect[6] = { 0x8B,0x46,0x0C, 0x8B,0x40,0x08 };   // mov eax,[esi+0xC] / mov eax,[eax+8]
    if (memcmp(site, expect, sizeof(expect)) != 0) {
        Log("[ne] PoolDestroyGuard NOT patched: %p holds %02X %02X %02X %02X %02X %02X\n",
            site, site[0], site[1], site[2], site[3], site[4], site[5]);
        return;
    }
    g_poolDestroyBack = (uintptr_t)(site + 6);
    DWORD old;
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old)) return;
    site[0] = 0xE9;
    *(int*)(site + 1) = (int)((BYTE*)&Safe_PoolDestroy - (site + 5));
    site[5] = 0x90;   // nop: the JMP is 5 bytes, the two instructions were 6
    VirtualProtect(site, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
    Log("[ne] PoolDestroyGuard patched at %p (back=%p)\n", site, (void*)g_poolDestroyBack);
}
LONG PoolDestroySkipped() { return g_poolDestroySkipped; }

void InstallComAssignGuard() {
    JmpPatch((void*)S4(0x01B36AB0), (void*)&Safe_ComAssign, "ComAssignGuard", "\x55\x8B\xEC");
    JmpPatch((void*)S4(0x01B36B10), (void*)&Safe_ComRelease, "ComReleaseGuard", "\x55\x8B\xEC");
}

void InstallContainerGuard() {
    // With the dummy container the root guard is enough: the callers (FUN_01de3db0,
    // FUN_01cbe470, ...) walk a valid structure and return 0 on their own.
    JmpPatch((void*)S4(0x01AE88A0), (void*)&Safe_GetContainer, "ContainerGuard", "\x8B\x01\x85");
    (void)&Safe_BucketLookup;
}

void InstallStringIterGuard() {
    BYTE* target = (BYTE*)S4(0x011BB0F0);
    DWORD old;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    // JMP rel32 to our version
    intptr_t rel = (intptr_t)&Safe_StringIterCtor - ((intptr_t)target + 5);
    target[0] = 0xE9;
    *(int*)(target + 1) = (int)rel;
    VirtualProtect(target, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    Log("[ne] StringIterGuard installed at %p\n", target);
}

} // namespace ne
