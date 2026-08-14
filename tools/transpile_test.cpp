// transpile_test.cpp -- OFFLINE harness for the fx_2_0 -> SM4/5 transpiler.
// Reads a captured .fx, strips the technique/pass blocks, and tries to compile the
// VS/PS entry points with D3DCompile (+backwards compat). Prints the errors so the
// transforms can be iterated without putting the game in the loop.
//
// build: cl /EHsc transpile_test.cpp d3dcompiler.lib
// run:   transpile_test <file.fx> <VS_entry> <PS_entry>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <vector>
#pragma comment(lib, "d3dcompiler.lib")

static std::string ReadFile(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return {};
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s; s.resize(n); fread(&s[0], 1, n, f); fclose(f); return s;
}

// strips every "technique ... { ... }" (balancing braces) out of the source
static std::string StripTechniques(const std::string& in) {
    std::string out; size_t i = 0;
    while (i < in.size()) {
        size_t t = in.find("technique", i);
        if (t == std::string::npos) { out += in.substr(i); break; }
        out += in.substr(i, t - i);
        size_t brace = in.find('{', t);
        if (brace == std::string::npos) { break; }
        int depth = 0; size_t j = brace;
        for (; j < in.size(); ++j) { if (in[j] == '{') depth++; else if (in[j] == '}') { depth--; if (depth == 0) { j++; break; } } }
        i = j;
    }
    return out;
}

static bool Compile(const std::string& src, const char* entry, const char* target) {
    ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
    HRESULT hr = D3DCompile(src.data(), src.size(), "fx", nullptr, nullptr, entry, target, flags, 0, &code, &err);
    printf("  %-40s [%s] hr=0x%08X %s\n", entry, target, hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    if (err) { printf("    %.600s\n", (char*)err->GetBufferPointer()); err->Release(); }
    if (code) code->Release();
    return SUCCEEDED(hr);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: transpile_test <fx> [VS] [PS]\n"); return 1; }
    std::string fx = ReadFile(argv[1]);
    if (fx.empty()) { printf("could not read %s\n", argv[1]); return 1; }
    std::string src = StripTechniques(fx);
    printf("== %s (%zu -> %zu bytes after stripping techniques) ==\n", argv[1], fx.size(), src.size());
    const char* vs = argc > 2 ? argv[2] : "VS_FX";
    const char* ps = argc > 3 ? argv[3] : "PS_FX";
    Compile(src, vs, "vs_4_0");
    Compile(src, ps, "ps_4_0");
    return 0;
}
