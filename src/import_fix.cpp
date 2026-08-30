// import_fix.cpp -- make an unpacked client run on any Windows build.
//
// The client is a dump: its .idata is 1KB for a 24MB image, so almost none of its ~749
// imports go through a real import table. They were resolved when the dump was taken and
// BAKED IN as absolute addresses of ntdll/kernel32/... as those DLLs happened to be laid
// out on that machine. On another Windows build those addresses point at something else,
// and the client faults a few milliseconds into startup reading one of them (the report we
// chased: push [ebx] with ebx = 0x77C50E7C, identical every run, only on the other PC).
//
// The fix stops depending on addresses and depends on NAMES instead:
//
//   HARVEST (once, on a machine where the client runs): walk every loaded system module's
//   export directory, then scan the client's data sections for DWORDs that are EXACTLY the
//   entry point of one of those exports. Each hit is an import slot. Write "RVA mod!export".
//
//   APPLY (every startup, anywhere): read that table, GetProcAddress each name on THIS
//   machine, and write the result back into the slot.
//
// Only data sections are scanned. A baked address inlined into an instruction would need
// disassembly to rewrite safely, and getting that wrong corrupts code; the thunk arrays the
// client actually calls through live in data.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "log.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;   // this DLL, to place the table next to it

namespace ne {

static const char* kTableName = "s4_imports.txt";

// SEH cannot live in a function that unwinds C++ objects, so every guarded read is its own
// little function. A dump can point these anywhere; a bad one must not take the client down.
static bool SafeReadPtr(const void* p, uintptr_t& out) {
    __try { out = *(const uintptr_t*)p; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct ExpInfo { DWORD dirRva, dirSize, names, ords, funcs, count; };
static bool SafeExportDir(BYTE* base, ExpInfo& e) {
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!d.VirtualAddress || !d.Size) return false;
        IMAGE_EXPORT_DIRECTORY* ex = (IMAGE_EXPORT_DIRECTORY*)(base + d.VirtualAddress);
        e.dirRva = d.VirtualAddress; e.dirSize = d.Size;
        e.names = ex->AddressOfNames; e.ords = ex->AddressOfNameOrdinals;
        e.funcs = ex->AddressOfFunctions; e.count = ex->NumberOfNames;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// address -> "module!export", for every export of every loaded module.
static void BuildExportMap(std::unordered_map<uintptr_t, std::string>& out) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    HMODULE self = GetModuleHandleW(NULL);
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        if ((HMODULE)me.modBaseAddr == self) continue;   // the client itself: not what we resolve
        BYTE* base = me.modBaseAddr;
        ExpInfo e{};
        if (!SafeExportDir(base, e)) continue;
        char modname[64] = "";
        WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, modname, sizeof(modname) - 1, nullptr, nullptr);
        DWORD* names = (DWORD*)(base + e.names);
        WORD*  ords  = (WORD*)(base + e.ords);
        DWORD* funcs = (DWORD*)(base + e.funcs);
        for (DWORD i = 0; i < e.count; ++i) {
            uintptr_t nameRva = 0, fnRva = 0;
            if (!SafeReadPtr(&names[i], nameRva)) break;
            WORD ord = 0;
            { uintptr_t tmp = 0; if (!SafeReadPtr((BYTE*)&ords[i] - ((uintptr_t)&ords[i] & 3), tmp)) break; ord = ords[i]; }
            if (!SafeReadPtr(&funcs[ord], fnRva)) break;
            DWORD rva = (DWORD)fnRva;
            // forwarders point back inside the export directory; they are not code
            if (rva >= e.dirRva && rva < e.dirRva + e.dirSize) continue;
            uintptr_t addr = (uintptr_t)(base + rva);
            if (out.count(addr)) continue;   // first name wins; aliases are equivalent
            out[addr] = std::string(modname) + "!" + (const char*)(base + (DWORD)nameRva);
        }
    }
    CloseHandle(snap);
}

// The client's data sections: where a baked import slot can live without being code.
struct Range { BYTE* begin; BYTE* end; };
static void ClientDataRanges(std::vector<Range>& out, BYTE*& imageBase) {
    imageBase = (BYTE*)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)imageBase;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(imageBase + dos->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (UINT i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        // skip pure code: rewriting an immediate inside an instruction needs disassembly
        if (s->Characteristics & IMAGE_SCN_CNT_CODE) continue;
        if (!(s->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        BYTE* b = imageBase + s->VirtualAddress;
        out.push_back({ b, b + s->Misc.VirtualSize });
    }
}

static void TablePath(char* path, const char* name = kTableName) {
    GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) lstrcpyA(slash + 1, name);
}

// Runs where the client WORKS. Produces the table next to the DLL.
//
// Called repeatedly: the slots that matter most are NOT the ones the Windows loader fills.
// The packer's own stubs build their tables lazily, well after startup, so a single sweep
// during init sees them empty and reports only the ordinary IAT. Every pass merges into the
// same map, so a late-filled slot is picked up whenever it appears.
static std::unordered_map<unsigned, std::string> g_harvest;   // RVA -> module!export

void HarvestImports() {
    static int pass = 0;
    ++pass;

    std::unordered_map<uintptr_t, std::string> exports;
    BuildExportMap(exports);
    BYTE* base = nullptr; std::vector<Range> ranges;
    ClientDataRanges(ranges, base);

    int fresh = 0;
    for (auto& r : ranges) {
        for (BYTE* p = (BYTE*)(((uintptr_t)r.begin + 3) & ~(uintptr_t)3); p + 4 <= r.end; p += 4) {
            uintptr_t v = 0;
            if (!SafeReadPtr(p, v)) break;
            auto it = exports.find(v);
            if (it == exports.end()) continue;
            unsigned rva = (unsigned)(p - base);
            if (g_harvest.insert({ rva, it->second }).second) fresh++;
        }
    }

    std::string text;
    for (auto& kv : g_harvest) {
        char line[192];
        wsprintfA(line, "%08X %s\n", kv.first, kv.second.c_str());
        text += line;
    }
    char path[MAX_PATH]; TablePath(path, "s4_imports_harvest.txt");
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, text.data(), (DWORD)text.size(), &w, nullptr); CloseHandle(h); }
    Log("[imp] harvest pass %d: %d new, %d total -> %s\n", pass, fresh, (int)g_harvest.size(), path);
}

// Runs on EVERY machine, before the client uses any of them.
// noLoadLibrary: called from DllMain with the loader lock held, so only modules that are
// already mapped may be touched. The slots that matter (user32, kernel32, ntdll) are all
// loaded by then; the rest get picked up by the second pass from InitThread.
void ApplyImportTable(bool noLoadLibrary) {
    char path[MAX_PATH]; TablePath(path);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;   // no table: no-op, the client runs as before
    DWORD sz = GetFileSize(h, nullptr);
    std::string text; text.resize(sz + 1);
    DWORD rd = 0; ReadFile(h, &text[0], sz, &rd, nullptr); CloseHandle(h);
    text.resize(rd);

    BYTE* base = (BYTE*)GetModuleHandleW(NULL);
    int fixed = 0, same = 0, missing = 0;
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i); if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(i, nl - i); i = nl + 1;
        if (line.size() < 12) continue;
        unsigned rva = strtoul(line.c_str(), nullptr, 16);
        size_t sp = line.find(' '); if (sp == std::string::npos) continue;
        size_t bang = line.find('!', sp); if (bang == std::string::npos) continue;
        std::string mod = line.substr(sp + 1, bang - sp - 1);
        std::string fn = line.substr(bang + 1);
        while (!fn.empty() && (fn.back() == '\r' || fn.back() == ' ')) fn.pop_back();

        HMODULE m = GetModuleHandleA(mod.c_str());
        if (!m && !noLoadLibrary) m = LoadLibraryA(mod.c_str());
        if (!m) { missing++; continue; }
        FARPROC f = GetProcAddress(m, fn.c_str());
        if (!f) { missing++; continue; }

        uintptr_t* slot = (uintptr_t*)(base + rva);
        DWORD old;
        if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old)) continue;
        uintptr_t cur = 0;
        if (SafeReadPtr(slot, cur)) {
            if (cur == (uintptr_t)f) same++;
            else { *slot = (uintptr_t)f; fixed++; }
        }
        VirtualProtect(slot, sizeof(uintptr_t), old, &old);
    }
    Log("[imp] %s pass: %d rewritten, %d already correct, %d unresolved\n",
        noLoadLibrary ? "DllMain" : "thread", fixed, same, missing);
}

} // namespace ne
