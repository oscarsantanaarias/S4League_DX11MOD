#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <detours.h>
#include "log.h"
#include "s4_base.h"
#include "backend_shared.h"

namespace ne {

typedef void (__thiscall* FogApplyFn)(void*, float*, float, float);
static FogApplyFn g_fogApply = nullptr;
typedef void (__thiscall* FogSetterFn)(void*, float, float, float, float, float);
static FogSetterFn g_fogSetter = nullptr;
static volatile LONG g_manual = 0;
static volatile LONG g_step = 0;

static DWORD WINAPI FogKeys(void*) {
    AllocConsole();
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    SetConsoleTitleA("S4League fog monitor");
    printf("Fog monitor: X=manual +/-=max distance\n");
    bool xWasDown = false, plusWasDown = false, minusWasDown = false;
    for (;;) {
        bool x = (GetAsyncKeyState('X') & 0x8000) != 0;
        bool plus = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) || (GetAsyncKeyState(VK_ADD) & 0x8000);
        bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000);
        if (x && !xWasDown) {
            InterlockedXor(&g_manual, 1);
            printf("manual=%s\n", g_manual ? "ON" : "OFF");
        }
        if (g_manual && plus && !plusWasDown) InterlockedExchangeAdd(&g_step, 500);
        if (g_manual && minus && !minusWasDown) InterlockedExchangeAdd(&g_step, -500);
        xWasDown = x; plusWasDown = plus; minusWasDown = minus;
        Sleep(35);
    }
}

static void __fastcall TraceFogApply(void* self, void*, float* color, float minDist, float maxDist) {
    LONG step = g_manual ? g_step : 0;
    float adjustedMax = maxDist + (float)step;
    if (g_manual && adjustedMax < minDist + 1.f) adjustedMax = minDist + 1.f;
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 8)
        Log("[fog-call] apply self=%p min=%d max=%d\n", self, (int)minDist, (int)adjustedMax);
    if (g_fogApply) g_fogApply(self, color, minDist, adjustedMax);
}
static void __fastcall TraceFogSetter(void* self, void*, float minDist, float maxDist, float r, float g, float b) {
    NE_SetFogValues(minDist, maxDist, r, g, b);
    Log("[fog-call] values min=%d max=%d rgb=%d,%d,%d\n",
        (int)minDist, (int)maxDist, (int)(r * 1000.f), (int)(g * 1000.f), (int)(b * 1000.f));
    if (g_fogSetter) g_fogSetter(self, minDist, maxDist, r, g, b);
}

void InstallFogApplyTrace() {
    g_fogApply = (FogApplyFn)S4(0x01D8FF30);
    g_fogSetter = (FogSetterFn)S4(0x0117D7B0);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG rc = DetourAttach(&(PVOID&)g_fogApply, (PVOID)&TraceFogApply);
    if (rc == NO_ERROR) rc = DetourAttach(&(PVOID&)g_fogSetter, (PVOID)&TraceFogSetter);
    if (rc == NO_ERROR) rc = DetourTransactionCommit();
    else DetourTransactionAbort();
    Log("[fog-call] Detours %s rc=%ld target=%p\n",
        rc == NO_ERROR ? "installed" : "failed", rc, (void*)g_fogApply);
    CreateThread(nullptr, 0, FogKeys, nullptr, 0, nullptr);
}

}
