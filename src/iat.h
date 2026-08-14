#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace ne {

// Base-independent IAT patch: in module `mod`, swap the import slot for
// `dll!fn` with `newf`. Returns the original function (or nullptr).
inline void* PatchIAT(HMODULE mod, const char* dll, const char* fn, void* newf) {
    auto base = (BYTE*)mod;
    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!d.VirtualAddress) return nullptr;
    auto desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + d.VirtualAddress);
    for (; desc->Name; ++desc) {
        if (lstrcmpiA((char*)(base + desc->Name), dll) != 0) continue;
        auto oth = (IMAGE_THUNK_DATA*)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        auto iat = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);
        for (; oth->u1.AddressOfData; ++oth, ++iat) {
            if (oth->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto imp = (IMAGE_IMPORT_BY_NAME*)(base + oth->u1.AddressOfData);
            if (lstrcmpA((char*)imp->Name, fn) == 0) {
                void* old = (void*)iat->u1.Function;
                DWORD op; VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &op);
                iat->u1.Function = (uintptr_t)newf;
                VirtualProtect(&iat->u1.Function, sizeof(void*), op, &op);
                return old;
            }
        }
    }
    return nullptr;
}

} // namespace ne
