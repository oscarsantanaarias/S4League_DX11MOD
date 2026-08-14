// d3d9spy.dll -- IT ONLY WATCHES AND WRITES. It translates nothing, it draws nothing.
//
// It is loaded into an UNMODIFIED client to capture the TRUTH: what the game
// actually calls in D3D9, in what order and with what values. Everything we had been
// comparing against NativeEngine was against an idea of how it should be; this is
// against what the game does.
//
// Method: it does NOT wrap the device (that would be 100+ forwarders). It patches
// the vtable entries we care about IN PLACE and calls the original. ~10 hooks.
//
// It writes d3d9spy.log next to the client.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>

static CRITICAL_SECTION g_cs;
static void Log(const char* fmt, ...) {
    EnterCriticalSection(&g_cs);
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);   // NOTE: wvsprintfA does NOT support %f, %g or precision
    va_end(ap);
    HANDLE h = CreateFileA("d3d9spy.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, buf, n, &w, nullptr); CloseHandle(h); }
    LeaveCriticalSection(&g_cs);
}

// ---- patching one vtable entry ----
static void* PatchVtbl(void* obj, int index, void* newf) {
    void** vt = *(void***)obj;
    DWORD old;
    if (!VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
    void* orig = vt[index];
    vt[index] = newf;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return orig;
}

// IDirect3DDevice9 vtable indices (offset/4)
enum {
    IDX_Present = 17, IDX_SetRenderState = 57, IDX_SetTexture = 65,
    IDX_SetTextureStageState = 67, IDX_SetSamplerState = 69,
    IDX_DrawIndexedPrimitive = 82, IDX_SetVertexDeclaration = 87,
};

typedef HRESULT(WINAPI* tPresent)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(WINAPI* tSetRenderState)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT(WINAPI* tSetTexture)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(WINAPI* tDrawIndexedPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

static tPresent oPresent;
static tSetRenderState oSetRenderState;
static tSetTexture oSetTexture;
static tDrawIndexedPrimitive oDIP;

static LONG g_frame = 0, g_dip = 0;

// BURST mode: with F9 it records N COMPLETE frames (every call, in order).
// Outside a burst only changes are logged, so the log does not explode.
static volatile LONG g_burst = 0;
static DWORD g_tss[8][8];      // mirror of the texture stage states
static DWORD g_ss[8][14];      // mirror of the sampler states
static void* g_tex[8];         // texture per stage
static bool B() { return g_burst > 0; }

static HRESULT WINAPI hPresent(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND c, const RGNDATA* e) {
    LONG f = InterlockedIncrement(&g_frame);
    // NO TRIGGER. The file-based one never reacted and the obvious causes were
    // already ruled out (the deployed DLL had the code, Present was running, the file
    // existed, the path was absolute). Instead of fighting it: it records BY ITSELF.
    // Three bursts of 3 frames, spaced out, starting ~20s after launch. That way you
    // only need to be on the map.
    static LONG bursts = 0;
    if (f > 1200 && bursts < 3 && (f % 1200) == 0 && g_burst <= 0) {
        bursts++;
        g_burst = 3;
        Log("=== BURST %d of 3 (frame %d) ===\r\n", (int)bursts, (int)f);
    }
    if (g_burst > 0) { if (--g_burst == 0) Log("=== end of burst ===\r\n"); }
    if ((f % 120) == 1) Log("[spy] frame=%d DIP=%d\r\n", f, g_dip);
    return oPresent(d, a, b, c, e);
}

// Only the states we care about, and only when they CHANGE (otherwise it is thousands per frame).
static HRESULT WINAPI hSetRenderState(IDirect3DDevice9* d, D3DRENDERSTATETYPE s, DWORD v) {
    switch (s) {
        case D3DRS_FOGENABLE: case D3DRS_FOGCOLOR: case D3DRS_FOGTABLEMODE:
        case D3DRS_FOGVERTEXMODE: case D3DRS_FOGSTART: case D3DRS_FOGEND:
        case D3DRS_FOGDENSITY: case D3DRS_RANGEFOGENABLE: {
            static DWORD last[256]; static bool seen[256];
            if (!seen[s] || last[s] != v) {
                seen[s] = true; last[s] = v;
                Log("[spy] RS %d = 0x%08X\r\n", (int)s, v);
            }
            break;
        }
        default: break;
    }
    return oSetRenderState(d, s, v);
}

// Which texture goes to each stage. It tells us whether the ShadeMap (stage 1) really arrives.
static HRESULT WINAPI hSetTexture(IDirect3DDevice9* d, DWORD stage, IDirect3DBaseTexture9* t) {
    if (stage < 4) {
        static void* last[4];
        if (last[stage] != t) {
            last[stage] = t;
            UINT w = 0, h = 0; D3DFORMAT fmt = D3DFMT_UNKNOWN;
            if (t && t->GetType() == D3DRTYPE_TEXTURE) {
                D3DSURFACE_DESC sd{};
                if (SUCCEEDED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &sd))) { w = sd.Width; h = sd.Height; fmt = sd.Format; }
            }
            Log("[spy] SetTexture stage=%d tex=%p %dx%d fmt=%d\r\n", (int)stage, t, w, h, (int)fmt);
        }
    }
    if (stage < 8) g_tex[stage] = t;
    return oSetTexture(d, stage, t);
}

static HRESULT WINAPI hDIP(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt, INT bv, UINT mi, UINT nv, UINT si, UINT pc) {
    InterlockedIncrement(&g_dip);
    if (B()) {
        DWORD fog = 0, fogc = 0, fs = 0, fe = 0, cull = 0, zw = 0, ab = 0, sb = 0, db = 0, at = 0;
        d->GetRenderState(D3DRS_FOGENABLE, &fog);      d->GetRenderState(D3DRS_FOGCOLOR, &fogc);
        d->GetRenderState(D3DRS_FOGSTART, &fs);        d->GetRenderState(D3DRS_FOGEND, &fe);
        d->GetRenderState(D3DRS_CULLMODE, &cull);      d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
        d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);d->GetRenderState(D3DRS_SRCBLEND, &sb);
        d->GetRenderState(D3DRS_DESTBLEND, &db);       d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
        Log("DIP prim=%d tris=%d | tex0=%p tex1=%p | cop0=%d ca1=%d ca2=%d aop0=%d | fog=%d fogc=%08X fs=%08X fe=%08X | cull=%d zw=%d ab=%d src=%d dst=%d atest=%d\r\n",
            (int)pt, (int)pc, g_tex[0], g_tex[1],
            (int)g_tss[0][D3DTSS_COLOROP], (int)g_tss[0][D3DTSS_COLORARG1], (int)g_tss[0][D3DTSS_COLORARG2],
            (int)g_tss[0][D3DTSS_ALPHAOP],
            (int)fog, fogc, fs, fe, (int)cull, (int)zw, (int)ab, (int)sb, (int)db, (int)at);
    }
    return oDIP(d, pt, bv, mi, nv, si, pc);
}

typedef HRESULT(WINAPI* tSetTSS)(IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
typedef HRESULT(WINAPI* tSetSamp)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
typedef HRESULT(WINAPI* tSetVDecl)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
typedef HRESULT(WINAPI* tSetFVF)(IDirect3DDevice9*, DWORD);
static tSetTSS oSetTSS; static tSetSamp oSetSamp; static tSetVDecl oSetVDecl; static tSetFVF oSetFVF;

static HRESULT WINAPI hSetTSS(IDirect3DDevice9* d, DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v) {
    if (st < 8 && t < 8) g_tss[st][t] = v;
    if (B()) Log("  TSS stage=%d type=%d val=%d\r\n", (int)st, (int)t, (int)v);
    return oSetTSS(d, st, t, v);
}
static HRESULT WINAPI hSetSamp(IDirect3DDevice9* d, DWORD st, D3DSAMPLERSTATETYPE t, DWORD v) {
    if (st < 8 && t < 14) g_ss[st][t] = v;
    if (B()) Log("  SAMP stage=%d type=%d val=%d\r\n", (int)st, (int)t, (int)v);
    return oSetSamp(d, st, t, v);
}
static HRESULT WINAPI hSetVDecl(IDirect3DDevice9* d, IDirect3DVertexDeclaration9* dc) {
    if (B()) Log("  VDECL %p\r\n", dc);
    return oSetVDecl(d, dc);
}
static HRESULT WINAPI hSetFVF(IDirect3DDevice9* d, DWORD f) {
    if (B()) Log("  FVF 0x%X\r\n", f);
    return oSetFVF(d, f);
}

static void HookDevice(IDirect3DDevice9* dev) {
    oPresent = (tPresent)PatchVtbl(dev, IDX_Present, (void*)&hPresent);
    oSetRenderState = (tSetRenderState)PatchVtbl(dev, IDX_SetRenderState, (void*)&hSetRenderState);
    oSetTexture = (tSetTexture)PatchVtbl(dev, IDX_SetTexture, (void*)&hSetTexture);
    oDIP = (tDrawIndexedPrimitive)PatchVtbl(dev, IDX_DrawIndexedPrimitive, (void*)&hDIP);
    oSetTSS = (tSetTSS)PatchVtbl(dev, IDX_SetTextureStageState, (void*)&hSetTSS);
    oSetSamp = (tSetSamp)PatchVtbl(dev, IDX_SetSamplerState, (void*)&hSetSamp);
    oSetVDecl = (tSetVDecl)PatchVtbl(dev, IDX_SetVertexDeclaration, (void*)&hSetVDecl);
    oSetFVF = (tSetFVF)PatchVtbl(dev, 89, (void*)&hSetFVF);
    Log("[spy] device hooked %p\r\n", dev);
}

// ---- hook of IDirect3D9::CreateDevice (index 16) to grab the device ----
typedef HRESULT(WINAPI* tCreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static tCreateDevice oCreateDevice;
static HRESULT WINAPI hCreateDevice(IDirect3D9* self, UINT a, D3DDEVTYPE t, HWND w, DWORD f,
                                    D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** ret) {
    HRESULT hr = oCreateDevice(self, a, t, w, f, pp, ret);
    if (SUCCEEDED(hr) && ret && *ret) {
        Log("[spy] CreateDevice %dx%d interval=0x%X\r\n",
            pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? pp->PresentationInterval : 0);
        HookDevice(*ret);
    }
    return hr;
}

// ---- hook of Direct3DCreate9 through the IAT ----
typedef IDirect3D9*(WINAPI* tD3DCreate9)(UINT);
static tD3DCreate9 oD3DCreate9;
static IDirect3D9* WINAPI hD3DCreate9(UINT sdk) {
    IDirect3D9* d3d = oD3DCreate9(sdk);
    if (d3d) { oCreateDevice = (tCreateDevice)PatchVtbl(d3d, 16, (void*)&hCreateDevice); Log("[spy] Direct3DCreate9 ok\r\n"); }
    return d3d;
}

static void* PatchIAT(HMODULE mod, const char* dll, const char* fn, void* newf) {
    BYTE* base = (BYTE*)mod;
    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA((char*)(base + imp->Name), dll) != 0) continue;
        auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        auto orig = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; orig->u1.AddressOfData; ++orig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
            if (lstrcmpA((char*)ibn->Name, fn) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
            void* prev = (void*)thunk->u1.Function;
            thunk->u1.Function = (uintptr_t)newf;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return prev;
        }
    }
    return nullptr;
}

#include <tlhelp32.h>
// The exe imports Direct3DCreate9 but NEVER calls it through there: the clean client
// has d3d9Remix.dll and the call comes out of another module. We patch the IAT of
// EVERY loaded module, and retry in case one loads late.
static int PatchAllModules() {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me; me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            if (lstrcmpiA(me.szModule, "d3d9.dll") == 0) continue;
            if (lstrcmpiA(me.szModule, "sneoz.dll") == 0) continue;
            void* prev = PatchIAT(me.hModule, "d3d9.dll", "Direct3DCreate9", (void*)&hD3DCreate9);
            if (prev) { if (!oD3DCreate9) oD3DCreate9 = (tD3DCreate9)prev; n++; Log("[spy] IAT patched in %s\r\n", me.szModule); }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return n;
}

// The exe is packed (Themida): it resolves the imports on its own and calls through
// its own table, so patching the static IAT does NOT work (the log confirmed it:
// "NOBODY called Direct3DCreate9 through the IAT"). We hook the d3d9.dll EXPORT with
// a 5-byte JMP: that way any caller falls into it, however it resolves.
static BYTE g_tramp[16];
static IDirect3D9* WINAPI Trampoline(UINT sdk) {
    return ((tD3DCreate9)(void*)g_tramp)(sdk);
}
static bool HookExport() {
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) d3d9 = LoadLibraryA("d3d9.dll");
    if (!d3d9) { Log("[spy] no d3d9.dll\r\n"); return false; }
    BYTE* fn = (BYTE*)GetProcAddress(d3d9, "Direct3DCreate9");
    if (!fn) { Log("[spy] no Direct3DCreate9 export\r\n"); return false; }
    // Typical hotpatch prologue: 8B FF 55 8B EC (mov edi,edi; push ebp; mov ebp,esp)
    Log("[spy] export at %p bytes %02X %02X %02X %02X %02X\r\n", fn, fn[0], fn[1], fn[2], fn[3], fn[4]);
    if (!(fn[0] == 0x8B && fn[1] == 0xFF && fn[2] == 0x55 && fn[3] == 0x8B && fn[4] == 0xEC)) {
        Log("[spy] unexpected prologue, not hooking (it would need decoding)\r\n");
        return false;
    }
    DWORD old;
    if (!VirtualProtect(g_tramp, sizeof(g_tramp), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(g_tramp, fn, 5);                       // the original 5 bytes
    g_tramp[5] = 0xE9;                            // JMP back to fn+5
    *(int*)(g_tramp + 6) = (int)((fn + 5) - (g_tramp + 10));
    oD3DCreate9 = (tD3DCreate9)&Trampoline;
    if (!VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    fn[0] = 0xE9;                                 // JMP to our hook
    *(int*)(fn + 1) = (int)((BYTE*)&hD3DCreate9 - (fn + 5));
    VirtualProtect(fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 5);
    Log("[spy] export hooked\r\n");
    return true;
}

static DWORD WINAPI Init(LPVOID) {
    Log("[spy] === d3d9spy loaded ===\r\n");
    if (!HookExport()) PatchAllModules();   // plan B in case the prologue is not the expected one
    for (int attempt = 0; attempt < 60 && !oCreateDevice; ++attempt) Sleep(250);
    if (!oCreateDevice) Log("[spy] the device was never created\r\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE m, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&g_cs);
        DisableThreadLibraryCalls(m);
        CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    }
    return TRUE;
}
