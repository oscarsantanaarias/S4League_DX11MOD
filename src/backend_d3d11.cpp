// backend_d3d11.cpp -- IDirect3D9/IDirect3DDevice9 nativo sobre D3D11 real.
//
// PHASE 1 (this file): real device + swapchain + present; VB/IB/texture resources
// as real D3D11 resources; draws with an input layout derived from the FVF and a
// default shader (it renders the fixed-function/FVF path = the 2D UI). The draws
// that use the game's programmable shaders (vs_1_1/ps_2_0) arrive through
// ID3DXEffect and are resolved in PHASE 2 (effects.cpp + fx_transpiler): that is
// where native VS/PS get bound. That is why here, if a programmable shader is set,
// the draw is skipped.
//
// See ARCHITECTURE.md. None of this is a shim: it is native D3D11.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d11_1.h>   // ID3D11DeviceContext1::ClearView, for D3D9 rectangle clears
#include <d3d11sdklayers.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <intrin.h>
#undef GetMessage
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "iat.h"
#include "log.h"
#include "s4_base.h"
#include "backend_shared.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace ne {

// ---- nativeengine_config.json ----
// A flat, hand-rolled reader instead of pulling in a JSON library for six numbers:
// the file only ever holds top-level "key": value pairs (numbers/true/false), so a
// substring search per key is all this needs. Lets these be toggled by editing a
// text file next to the game instead of setting environment variables and
// relaunching from a shell every time.
struct NEConfig {
    int msaa = -1;          // -1 = auto (highest the GPU supports), 0/1 = off, else forced count
    bool fog = true;
    bool ao = true;
    float aoRadius = 3.0f;
    float aoStrength = 1.0f;
    bool vsync = true;      // true = respect what the game asked for; false = force uncapped
    bool outlineDefault = false; // F3 starts on/off
};
static NEConfig g_cfg;
static bool JFindNumber(const std::string& text, const char* key, double& out) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = text.find(k);
    if (p == std::string::npos) return false;
    p = text.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < text.size() && isspace((unsigned char)text[p])) p++;
    if (text.compare(p, 4, "true") == 0) { out = 1.0; return true; }
    if (text.compare(p, 5, "false") == 0) { out = 0.0; return true; }
    char* end = nullptr;
    double v = strtod(text.c_str() + p, &end);
    if (end == text.c_str() + p) return false;
    out = v; return true;
}
static void LoadConfig() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    std::ifstream f("nativeengine_config.json");
    if (!f) { Log("[ne] no nativeengine_config.json, using defaults\n"); return; }
    std::stringstream ss; ss << f.rdbuf();
    std::string text = ss.str();
    double v;
    if (JFindNumber(text, "msaa", v)) g_cfg.msaa = (int)v;
    if (JFindNumber(text, "fog", v)) g_cfg.fog = v != 0;
    if (JFindNumber(text, "ao", v)) g_cfg.ao = v != 0;
    if (JFindNumber(text, "ao_radius", v)) g_cfg.aoRadius = (float)v;
    if (JFindNumber(text, "ao_strength", v)) g_cfg.aoStrength = (float)v;
    if (JFindNumber(text, "vsync", v)) g_cfg.vsync = v != 0;
    if (JFindNumber(text, "outline_default", v)) g_cfg.outlineDefault = v != 0;
    Log("[ne] config loaded: msaa=%d fog=%d ao=%d ao_radius=%d/1000 ao_strength=%d/1000 vsync=%d outline=%d\n",
        g_cfg.msaa, (int)g_cfg.fog, (int)g_cfg.ao, (int)(g_cfg.aoRadius*1000), (int)(g_cfg.aoStrength*1000),
        (int)g_cfg.vsync, (int)g_cfg.outlineDefault);
}

void NE_FogState(float* fog4, float* color4);

LONG GuardCalls();          // guards.cpp: how many times per frame the game enters the guards
LONG PoolDestroySkipped();  // guards.cpp: pool elements that were garbage and we refused to destroy
LONG DequeRescued();        // guards.cpp: render target pops served from the backbuffer instead of garbage

// ---- COM base: refcount + permissive QI ----
template <class I> struct Unk : public I {
    LONG ref = 1;
    STDMETHOD(QueryInterface)(REFIID, void** ppv) { *ppv = this; this->AddRef(); return S_OK; }
    STDMETHOD_(ULONG, AddRef)() { return InterlockedIncrement(&ref); }
    // We NEVER destroy: the game over-Releases objects it keeps using -> the later
    // AddRef read a garbage vtable (it was 2 = the refcount) and crashed. We prefer
    // leaking over a use-after-free.
    STDMETHOD_(ULONG, Release)() { LONG r = InterlockedDecrement(&ref); if (r < 1) { ref = 1; r = 1; } return r; }
};

struct NDevice; // fwd
static IDirect3DDevice9* g_dev9 = nullptr;

// The surfaces (NSurface/NTexSurface) carry their RTV/DSV for render-to-texture.
// It is queried through QueryInterface with this private GUID (it does not addref).
struct NE_RTView { ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr; UINT w = 0, h = 0; ID3D11Texture2D* tex = nullptr; UINT sub = 0; };
struct NTexture;
static void NE_TexLevelFilled(NTexture* t, UINT lvl); // defined after NTexture
// The light ramp last seen on stage 1 (see SetTexture). Referenced, so it survives the
// texture being destroyed on a map change.
static ID3D11ShaderResourceView* g_rampSRV = nullptr;
static const GUID IID_NE_RTView = { 0x9e0a1b2c, 0x3d4e, 0x5f60, { 0x71,0x82,0x93,0xa4,0xb5,0xc6,0xd7,0xe8 } };
static NE_RTView* NE_QueryRT(IDirect3DSurface9* s) { if (!s) return nullptr; NE_RTView* v = nullptr; return SUCCEEDED(s->QueryInterface(IID_NE_RTView, (void**)&v)) ? v : nullptr; }

// D3DRS_DEPTHBIAS / SLOPESCALEDEPTHBIAS: the game uses them to lift the decals just
// slightly off the floor. Without implementing them they end up COPLANAR and the GPU
// alternates between the two -> stripes (z-fighting), which also shift with the angle.
// D3D9 uses a 0..1 float over the depth range; D3D11 uses integer units of the depth
// buffer (24 bits) -> we multiply by 2^24.
struct RsEntry { float bias, slope; DWORD cull, fill; ID3D11RasterizerState* rs; };
static RsEntry g_rsCache[64]; static int g_rsCount = 0;
static ID3D11RasterizerState* GetRasterizer(float bias, float slope, DWORD cull, DWORD fill); // defined after Gpu g

// SCALING blit for StretchRect (D3D9 scales; CopySubresourceRegion does not).
// Fullscreen triangle generated in the VS from SV_VertexID: no vertex buffer needed.
static ID3D11VertexShader* g_blitVS = nullptr;
static ID3D11PixelShader* g_blitPS = nullptr;
static ID3D11SamplerState* g_blitSamp = nullptr;
static const char* kBlitHLSL = R"(
Texture2D t : register(t0); SamplerState s : register(s0);
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
V VSb(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.uv = c; o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1); return o;
}
float4 PSb(V i) : SV_Target { return t.Sample(s, i.uv); }
)";
static bool BuildBlit(ID3D11Device* dev) {
    if (g_blitVS) return true;
    ID3DBlob* vb = nullptr; ID3DBlob* pb = nullptr; ID3DBlob* er = nullptr;
    if (FAILED(D3DCompile(kBlitHLSL, strlen(kBlitHLSL), "blit", nullptr, nullptr, "VSb", "vs_4_0", 0, 0, &vb, &er))) { if (er) er->Release(); return false; }
    if (FAILED(D3DCompile(kBlitHLSL, strlen(kBlitHLSL), "blit", nullptr, nullptr, "PSb", "ps_4_0", 0, 0, &pb, &er))) { vb->Release(); if (er) er->Release(); return false; }
    dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &g_blitVS);
    dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &g_blitPS);
    vb->Release(); pb->Release(); if (er) er->Release();
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &g_blitSamp);
    return g_blitVS && g_blitPS;
}

// ---- SSAO ----
// Cheap screen-space AO with no normal buffer: sample a small ring of neighbours
// around each pixel, linearize their depth the same way, and darken the centre when a
// neighbour is meaningfully closer to the camera (a corner/crease). Not physically
// accurate (no normal-oriented hemisphere), but a corner or contact point reads the
// same either way, and it costs one depth-only pass with no G-buffer to build.
// Two pixel shader variants because a non-multisampled depth texture cannot be bound
// as Texture2DMS (and vice versa): which one runs is picked at draw time from
// g.msaaSamples, not baked into one shader.


// ---- Mip generation for COMPRESSED textures (BC1/BC3) ----
// D3DX only fills level 0 (verified by log). Without mips the floor shimmers at
// grazing angles. D3D11's GenerateMips does not support BC, so: we decode the level,
// halve it and re-encode. The texture stays compressed (same RAM).
static void BcUnpack565(unsigned c, int& r, int& g, int& b) {
    r = (int)(((c >> 11) & 31) * 255 / 31); g = (int)(((c >> 5) & 63) * 255 / 63); b = (int)((c & 31) * 255 / 31);
}
static void BcDecodeBlock(const BYTE* blk, bool bc1, BYTE* out /*16 px RGBA*/) {
    const BYTE* col = bc1 ? blk : blk + 8;
    unsigned c0 = col[0] | (col[1] << 8), c1 = col[2] | (col[3] << 8);
    unsigned idx = col[4] | (col[5] << 8) | (col[6] << 16) | ((unsigned)col[7] << 24);
    int r[4], g[4], b[4];
    BcUnpack565(c0, r[0], g[0], b[0]); BcUnpack565(c1, r[1], g[1], b[1]);
    if (!bc1 || c0 > c1) {
        r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
        r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
    } else {
        r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
        r[3] = g[3] = b[3] = 0;
    }
    BYTE a[16]; for (int i = 0; i < 16; ++i) a[i] = 255;
    if (!bc1) { // BC3: alpha with 2 endpoints + 3 bits per pixel
        int a0 = blk[0], a1 = blk[1]; int t[8]; t[0] = a0; t[1] = a1;
        if (a0 > a1) for (int i = 1; i < 7; ++i) t[i + 1] = ((7 - i) * a0 + i * a1) / 7;
        else { for (int i = 1; i < 5; ++i) t[i + 1] = ((5 - i) * a0 + i * a1) / 5; t[6] = 0; t[7] = 255; }
        unsigned long long bits = 0; for (int i = 0; i < 6; ++i) bits |= (unsigned long long)blk[2 + i] << (8 * i);
        for (int i = 0; i < 16; ++i) a[i] = (BYTE)t[(bits >> (3 * i)) & 7];
    }
    for (int i = 0; i < 16; ++i) {
        int s = (idx >> (2 * i)) & 3;
        out[i * 4 + 0] = (BYTE)r[s]; out[i * 4 + 1] = (BYTE)g[s]; out[i * 4 + 2] = (BYTE)b[s]; out[i * 4 + 3] = a[i];
    }
}
static unsigned BcPack565(int r, int g, int b) { return ((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255); }
static void BcEncodeBlock(const BYTE* px /*16 px RGBA*/, bool bc1, BYTE* out) {
    BYTE* col = bc1 ? out : out + 8;
    int lo[3] = { 255,255,255 }, hi[3] = { 0,0,0 }, aLo = 255, aHi = 0;
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 3; ++c) { int v = px[i * 4 + c]; if (v < lo[c]) lo[c] = v; if (v > hi[c]) hi[c] = v; }
        int a = px[i * 4 + 3]; if (a < aLo) aLo = a; if (a > aHi) aHi = a;
    }
    unsigned c0 = BcPack565(hi[0], hi[1], hi[2]), c1 = BcPack565(lo[0], lo[1], lo[2]);
    if (c0 < c1) { unsigned t = c0; c0 = c1; c1 = t; int s; for (int c = 0; c < 3; ++c) { s = lo[c]; lo[c] = hi[c]; hi[c] = s; } }
    if (c0 == c1) c1 = c0 ? c0 - 1 : 0; // avoid falling into punchthrough mode by accident
    col[0] = (BYTE)(c0 & 0xFF); col[1] = (BYTE)(c0 >> 8); col[2] = (BYTE)(c1 & 0xFF); col[3] = (BYTE)(c1 >> 8);
    int pr[4], pg[4], pb[4]; BcUnpack565(c0, pr[0], pg[0], pb[0]); BcUnpack565(c1, pr[1], pg[1], pb[1]);
    pr[2] = (2 * pr[0] + pr[1]) / 3; pg[2] = (2 * pg[0] + pg[1]) / 3; pb[2] = (2 * pb[0] + pb[1]) / 3;
    pr[3] = (pr[0] + 2 * pr[1]) / 3; pg[3] = (pg[0] + 2 * pg[1]) / 3; pb[3] = (pb[0] + 2 * pb[1]) / 3;
    unsigned idx = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bd = 1 << 30;
        for (int s = 0; s < 4; ++s) {
            int dr = px[i * 4] - pr[s], dg = px[i * 4 + 1] - pg[s], db = px[i * 4 + 2] - pb[s];
            int d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = s; }
        }
        idx |= (unsigned)best << (2 * i);
    }
    col[4] = (BYTE)(idx & 0xFF); col[5] = (BYTE)((idx >> 8) & 0xFF); col[6] = (BYTE)((idx >> 16) & 0xFF); col[7] = (BYTE)(idx >> 24);
    if (!bc1) { // BC3: alpha
        out[0] = (BYTE)aHi; out[1] = (BYTE)aLo;
        int t[8]; t[0] = aHi; t[1] = aLo;
        if (aHi > aLo) for (int i = 1; i < 7; ++i) t[i + 1] = ((7 - i) * aHi + i * aLo) / 7;
        else for (int i = 0; i < 6; ++i) t[i + 2] = 0;
        unsigned long long bits = 0;
        for (int i = 0; i < 16; ++i) {
            int best = 0, bd = 1 << 30;
            for (int s = 0; s < 8; ++s) { int d = px[i * 4 + 3] - t[s]; d = d < 0 ? -d : d; if (d < bd) { bd = d; best = s; } }
            bits |= (unsigned long long)best << (3 * i);
        }
        for (int i = 0; i < 6; ++i) out[2 + i] = (BYTE)((bits >> (8 * i)) & 0xFF);
    }
}

// Emergency buffer: if a calloc fails (RAM full), NEVER return NULL from LockRect.
// The game DETECTS the failure, logs "CFont FreeType::WriteT" and does the memset
// ANYWAY with a NULL base -> writes at (0 + pitch*row) -> AV at 0x28C6.
static BYTE* Scratch() {
    static BYTE* s = nullptr;
    if (!s) s = (BYTE*)calloc(4 * 1024 * 1024, 1);
    return s;
}

// The ID3D11DeviceContext is NOT thread-safe and the game loads textures/buffers from
// loading threads (that is why some characters showed their texture and others did not).
// Initialized during the DLL's static construction (single-threaded, before the game's
// threads start): initializing it lazily had a race that hung the process.
struct CsHolder { CRITICAL_SECTION cs; CsHolder() { InitializeCriticalSection(&cs); } };
static CsHolder g_csHolder;
struct CtxLock {
    CtxLock() { EnterCriticalSection(&g_csHolder.cs); }
    ~CtxLock() { LeaveCriticalSection(&g_csHolder.cs); }
};

// ---- backend global state (shared D3D11 device) ----
struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    bool tearingSupported = false;
    ID3D11RenderTargetView* rtv = nullptr;      // backbuffer
    ID3D11RenderTargetView* curRTV = nullptr;   // CURRENT render target (backbuffer or texture)
    ID3D11DepthStencilView* curDSV = nullptr;   // CURRENT depth
    ID3D11VertexShader* vsDefault = nullptr;
    ID3DBlob* vsDefaultBlob = nullptr;
    ID3D11PixelShader* psDefault = nullptr;
    ID3D11PixelShader* psSolid = nullptr; // TEST: solid green
    ID3D11PixelShader* psFF1 = nullptr;   // fixed-function for passes with PixelShader = null
    ID3D11InputLayout* ilXYZRHW_DIF_T1 = nullptr;
    ID3D11Buffer* cb = nullptr;      // constants of the default shader
    ID3D11Buffer* upVB = nullptr;    // dynamic buffer for DrawPrimitiveUP
    UINT upVBsize = 0;
    ID3D11Buffer* upIB = nullptr;    // dynamic index buffer for DrawIndexedPrimitiveUP
    UINT upIBsize = 0;
    ID3D11SamplerState* samp = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11RasterizerState* rsNoCull = nullptr;
    ID3D11Texture2D* depthTex = nullptr; ID3D11DepthStencilView* dsv = nullptr;
    ID3D11ShaderResourceView* depthSRV = nullptr; // same texture, read by the SSAO pass
    ID3D11DepthStencilState* dsWrite = nullptr, * dsNoWrite = nullptr, * dsOff = nullptr;
    ID3D11Buffer* fogCB = nullptr;   // PER-DRAW fog (b1), not per BeginPass
    ID3D11InfoQueue* iq = nullptr;
    UINT bbW = 0, bbH = 0;
    // Enemy/character outline (F3 toggles it): a single-channel mask, backbuffer-sized,
    // that every skinned draw also writes a white silhouette into (same depth test as
    // the real draw, so it is naturally occluded by walls -- no extra logic needed for
    // "hides behind walls"). Composited onto the backbuffer in Present() as a colored
    // ring around the mask's edges, then cleared for the next frame.
    ID3D11Texture2D* outlineTex = nullptr;
    ID3D11RenderTargetView* outlineRTV = nullptr;
    ID3D11ShaderResourceView* outlineSRV = nullptr;

    // MSAA: g.rtv/the swapchain buffer stays single-sample (FLIP_DISCARD requires it --
    // DXGI does not allow a multisampled swapchain backbuffer). The game's own draws go
    // to g.msaaRTV instead (same role g.rtv used to play for "curRTV == the main scene"),
    // and Present() resolves it into the real backbuffer, in bbTex, right before
    // compositing the outline and presenting. bbTex is the raw texture behind g.rtv,
    // kept alive as the resolve destination -- the old code released it immediately
    // after creating the view, which was fine when nothing needed the texture itself.
    ID3D11Texture2D* bbTex = nullptr;
    ID3D11Texture2D* msaaTex = nullptr;
    ID3D11RenderTargetView* msaaRTV = nullptr;
    UINT msaaSamples = 1;
} g;

static float g_aoProj33 = 1.f, g_aoProj43 = 1.f; // set from the game's real projection matrix each frame
static ID3D11PixelShader* g_aoPS_MS = nullptr;   // depth texture is multisampled
static ID3D11PixelShader* g_aoPS_1x = nullptr;   // depth texture is single-sample
static ID3D11Buffer* g_aoCB = nullptr;
static ID3D11BlendState* g_aoBlend = nullptr;    // dest *= src.rgb (AO factor)
static const char* kAOCommonHLSL = R"(
cbuffer AOCB : register(b0) { float2 texel; float proj33; float proj43; float radius; float strength; float3 pad0; };
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
static const float2 kTaps[8] = {
    float2( 1, 0), float2(-1, 0), float2( 0, 1), float2( 0,-1),
    float2( 0.707, 0.707), float2(-0.707, 0.707), float2( 0.707,-0.707), float2(-0.707,-0.707)
};
float LinDepth(float z) { return proj43 / max(z - proj33, 1e-5); }
float AOFromCenter(float centerLin, float2 uv0) {
    float occ = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        float2 uv = uv0 + kTaps[i] * texel * radius;
)";
static const char* kAOSampleMS = "        float z = DEPTHTEX.Load(int2(uv * SCREENSIZE), 0).r;\n";
static const char* kAOSample1x = "        float z = DEPTHTEX.Load(int3(uv * SCREENSIZE, 0)).r;\n";
static const char* kAOTailHLSL = R"(
        float d = LinDepth(z);
        float diff = centerLin - d; // positive: neighbour is closer to camera
        occ += saturate(diff / max(centerLin * 0.05, 1.0)) * saturate(1.0 - abs(diff) / (centerLin * 0.3 + 1.0));
    }
    return saturate(1.0 - (occ / 8.0) * strength);
}
float4 PSao(V i) : SV_Target {
    float cz = CENTERLOAD;
    float centerLin = LinDepth(cz);
    float ao = AOFromCenter(centerLin, i.uv);
    return float4(ao, ao, ao, 1);
}
)";
static bool BuildAO(ID3D11Device* dev) {
    if (g_aoPS_MS || g_aoPS_1x) return true;
    auto compile = [&](bool ms) -> ID3D11PixelShader* {
        std::string src = std::string(kAOCommonHLSL) + (ms ? kAOSampleMS : kAOSample1x) + kAOTailHLSL;
        // ps_4_0 requires the sample count baked into the type (Texture2DMS<float,N>);
        // it can't be generic. g.msaaSamples is fixed for the process's lifetime, so
        // this only ever compiles the one variant that actually matches the real
        // depth texture.
        char msDecl[64]; wsprintfA(msDecl, "Texture2DMS<float,%u> DEPTHTEX : register(t0);\n", g.msaaSamples);
        std::string decl = ms ? std::string(msDecl) : "Texture2D<float> DEPTHTEX : register(t0);\n";
        // SCREENSIZE has to be real pixel dimensions for Load's integer coords, not the
        // 0..1 uv this shader otherwise works in -- passed as part of the cbuffer instead
        // of a macro so both variants share the exact same source apart from the Load line.
        std::string full = "cbuffer AOSize : register(b1) { float2 SCREENSIZE; float2 pad1; };\n" + decl + src;
        size_t p = full.find("CENTERLOAD");
        std::string centerLoad = ms ? "DEPTHTEX.Load(int2(i.uv * SCREENSIZE), 0).r" : "DEPTHTEX.Load(int3(i.uv * SCREENSIZE, 0)).r";
        full.replace(p, strlen("CENTERLOAD"), centerLoad);
        ID3DBlob* pb = nullptr; ID3DBlob* er = nullptr;
        // Texture2DMS access from a pixel shader needs SM4.1+; ps_4_0 (used everywhere
        // else in this file) rejects it outright. ps_5_0 is supported on any real D3D11
        // device, so just use it for both AO variants rather than juggling two profiles.
        HRESULT hr = D3DCompile(full.c_str(), full.size(), "ao", nullptr, nullptr, "PSao", "ps_5_0", 0, 0, &pb, &er);
        if (FAILED(hr)) { if (er) { Log("[ao] compile fail: %.400s\n", (char*)er->GetBufferPointer()); er->Release(); } return nullptr; }
        ID3D11PixelShader* ps = nullptr;
        dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &ps);
        pb->Release(); if (er) er->Release();
        return ps;
    };
    g_aoPS_MS = compile(true);
    g_aoPS_1x = compile(false);
    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = 32; cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cbd, nullptr, &g_aoCB);
    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &g_aoBlend);
    return (g_aoPS_MS || g_aoPS_1x) && g_aoCB && g_aoBlend;
}
static void DrawSSAO() {
    LoadConfig();
    if (!g_cfg.ao || !g.ctx || !g.dev || !g.depthSRV || !g.rtv) return;
    if (!BuildBlit(g.dev) || !BuildAO(g.dev)) return;
    ID3D11PixelShader* ps = g.msaaSamples > 1 ? g_aoPS_MS : g_aoPS_1x;
    if (!ps || !g_aoCB || !g_aoBlend) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(g.ctx->Map(g_aoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float data[8] = { 1.0f / g.bbW, 1.0f / g.bbH, g_aoProj33, g_aoProj43, g_cfg.aoRadius, g_cfg.aoStrength, 0, 0 };
        memcpy(m.pData, data, sizeof(data)); g.ctx->Unmap(g_aoCB, 0);
    }
    ID3D11Buffer* sizeCB = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = 16; cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    static ID3D11Buffer* sizeCBcache = nullptr;
    if (!sizeCBcache) g.dev->CreateBuffer(&cbd, nullptr, &sizeCBcache);
    sizeCB = sizeCBcache;
    if (sizeCB && SUCCEEDED(g.ctx->Map(sizeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float sz[4] = { (float)g.bbW, (float)g.bbH, 0, 0 };
        memcpy(m.pData, sz, sizeof(sz)); g.ctx->Unmap(sizeCB, 0);
    }
    ID3D11RenderTargetView* rtv = g.rtv;
    g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{}; vp.Width = (float)g.bbW; vp.Height = (float)g.bbH; vp.MaxDepth = 1.f;
    g.ctx->RSSetViewports(1, &vp);
    g.ctx->IASetInputLayout(nullptr);
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->VSSetShader(g_blitVS, nullptr, 0);
    g.ctx->PSSetShader(ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = { g_aoCB, sizeCB };
    g.ctx->PSSetConstantBuffers(0, 2, cbs);
    g.ctx->PSSetShaderResources(0, 1, &g.depthSRV);
    g.ctx->OMSetDepthStencilState(g.dsOff, 0);
    float bf[4] = { 0,0,0,0 };
    g.ctx->OMSetBlendState(g_aoBlend, bf, 0xffffffff);
    g.ctx->Draw(3, 0);
    g.ctx->OMSetBlendState(nullptr, bf, 0xffffffff);
    ID3D11ShaderResourceView* nul = nullptr; g.ctx->PSSetShaderResources(0, 1, &nul);
}

static ID3D11RasterizerState* GetRasterizer(float bias, float slope, DWORD cull, DWORD fill) {
    for (int i = 0; i < g_rsCount; ++i)
        if (g_rsCache[i].bias == bias && g_rsCache[i].slope == slope && g_rsCache[i].cull == cull && g_rsCache[i].fill == fill)
            return g_rsCache[i].rs;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = (fill == D3DFILL_WIREFRAME) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    // D3D9 culls by winding with front = CW (FrontCounterClockwise stays FALSE).
    // It was pinned to CULL_NONE: the back faces were drawn and z-fought with the
    // front ones, hence the stripes that shifted with the camera angle.
    rd.CullMode = (cull == D3DCULL_CW) ? D3D11_CULL_FRONT : (cull == D3DCULL_CCW) ? D3D11_CULL_BACK : D3D11_CULL_NONE;
    rd.DepthClipEnable = FALSE;
    rd.DepthBias = (INT)(bias * 16777216.0f); // 2^24 = D24 precision
    rd.SlopeScaledDepthBias = slope;
    ID3D11RasterizerState* rs = nullptr;
    if (FAILED(g.dev->CreateRasterizerState(&rd, &rs)) || !rs) return g.rsNoCull;
    if (g_rsCount < 64) g_rsCache[g_rsCount++] = { bias, slope, cull, fill, rs };
    return rs;
}

// Per-stage samplers: the engine sets ADDRESSU/V/W and MIN/MAG/MIPFILTER through
// D3DSAMP_*. We cache by (addressU, addressV, addressW, linear filter).
struct SampEntry { DWORD key; ID3D11SamplerState* s; };
static SampEntry g_sampCache[64]; static int g_sampCount = 0;
static D3D11_TEXTURE_ADDRESS_MODE AddrOf(DWORD a) {
    switch (a) {
        case D3DTADDRESS_MIRROR: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case D3DTADDRESS_CLAMP: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case D3DTADDRESS_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
        case D3DTADDRESS_MIRRORONCE: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}
static ID3D11SamplerState* GetSampler(DWORD u, DWORD v, DWORD w, DWORD minF, DWORD magF, DWORD mipF) {
    bool point = (minF == D3DTEXF_POINT || magF == D3DTEXF_POINT);
    DWORD key = (u & 7) | ((v & 7) << 3) | ((w & 7) << 6) | ((DWORD)point << 9);
    for (int i = 0; i < g_sampCount; ++i) if (g_sampCache[i].key == key) return g_sampCache[i].s;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 16; sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.AddressU = AddrOf(u); sd.AddressV = AddrOf(v); sd.AddressW = AddrOf(w);
    ID3D11SamplerState* s = nullptr;
    if (FAILED(g.dev->CreateSamplerState(&sd, &s)) || !s) return g.samp;
    if (g_sampCount < 64) g_sampCache[g_sampCount++] = { key, s };
    return s;
}

// stageC[i] = (COLOROP, COLORARG1, COLORARG2, thereIsATextureInTheStage)
// stageA[i] = (ALPHAOP, ALPHAARG1, ALPHAARG2, isA8)
struct CBData { float world[16]; float view[16]; float proj[16]; float vpW, vpH, hasTex, isRHW; float hasCol, pad0, pad1, pad2;
                float stageC[4][4]; float stageA[4][4]; float tfactor[4]; float fog[4]; float fogColor[4]; };

// Render states declared by the .fx pass currently bound. They override the device
// state for that pass's draws only, and are dropped with the program, so they never
// leak into the device and never fight with what the game sets in between.
static DWORD g_passState[16], g_passValue[16];
static int g_passStateCount = 0;

// ---- active program (the effect sets it before each draw) + input layouts ----
struct Program { ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; const void* vsbc = nullptr; SIZE_T vslen = 0; bool active = false; bool isSkinned = false; } g_prog;

// ---- character outline (F3) ----
// Everything Gpu/BuildBlit/g_prog-related this needs (g, g_blitVS, g_prog...) is
// already declared above this point in the file; earlier placements of this block
// (right by BuildBlit, near the top) predate all three and do not compile.
static bool g_outlineEnabled = false;
static ID3D11PixelShader* g_maskWhitePS = nullptr;
static ID3D11PixelShader* g_outlinePS = nullptr;
static ID3D11BlendState* g_outlineBlend = nullptr;
static ID3D11Buffer* g_outlineCB = nullptr;
static const char* kMaskHLSL =
    "float4 PSMask(float4 p : SV_Position) : SV_Target { return float4(1,1,1,1); }";
// Ring test: the composite quad checks 8 neighbors at `thickness` pixels; a fragment
// outside the silhouette (mask<0.5) with any filled neighbor is the border -> red.
// Inside the silhouette this outputs nothing -- the real character was already drawn,
// correctly lit, straight into the backbuffer earlier in the frame; this only ADDS
// the ring around it, it never repaints the character.
static const char* kOutlineCompositeHLSL = R"(
Texture2D mask : register(t0); SamplerState samp : register(s0);
cbuffer CB : register(b0) { float2 texel; float thickness; float pad0; }
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
float4 PSOutlineComposite(V i) : SV_Target {
    if (mask.Sample(samp, i.uv).r > 0.5) return float4(0,0,0,0);
    float m = 0;
    [unroll] for (int k = 0; k < 8; k++) {
        float ang = k * 0.7853981634;
        float2 o = float2(cos(ang), sin(ang)) * thickness * texel;
        m = max(m, mask.Sample(samp, i.uv + o).r);
    }
    return (m > 0.5) ? float4(1,0,0,1) : float4(0,0,0,0);
}
)";
static bool BuildOutlinePipeline(ID3D11Device* dev) {
    if (g_outlinePS) return true;
    ID3DBlob* mb = nullptr; ID3DBlob* ob = nullptr; ID3DBlob* er = nullptr;
    if (FAILED(D3DCompile(kMaskHLSL, strlen(kMaskHLSL), "mask", nullptr, nullptr, "PSMask", "ps_4_0", 0, 0, &mb, &er))) {
        if (er) { Log("[outline] mask PS compile fail: %s\n", (char*)er->GetBufferPointer()); er->Release(); }
        return false;
    }
    dev->CreatePixelShader(mb->GetBufferPointer(), mb->GetBufferSize(), nullptr, &g_maskWhitePS);
    mb->Release();
    if (FAILED(D3DCompile(kOutlineCompositeHLSL, strlen(kOutlineCompositeHLSL), "outline", nullptr, nullptr,
                          "PSOutlineComposite", "ps_4_0", 0, 0, &ob, &er))) {
        if (er) { Log("[outline] composite PS compile fail: %s\n", (char*)er->GetBufferPointer()); er->Release(); }
        return false;
    }
    dev->CreatePixelShader(ob->GetBufferPointer(), ob->GetBufferSize(), nullptr, &g_outlinePS);
    ob->Release();
    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, &g_outlineBlend);
    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = 16; cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cbd, nullptr, &g_outlineCB);
    return g_outlinePS && g_maskWhitePS;
}
// (Re)creates the mask target at the current backbuffer size. Called on device init
// and on every Reset() that changes resolution, right alongside g.dsv.
// Picks the highest of {8,4,2} the device actually supports for a BGRA8 render target
// at that sample count, falling back to msaaRTV == g.rtv (no MSAA) if even 2x reports no
// valid quality level -- seen on some software/RDP adapters, never on real hardware, but
// cheap to guard rather than assume.
static inline ID3D11RenderTargetView* MainSceneRTV() { return g.msaaSamples > 1 ? g.msaaRTV : g.rtv; }
static void CreateMSAATargets(UINT w, UINT h) {
    if (g.msaaRTV) { g.msaaRTV->Release(); g.msaaRTV = nullptr; }
    if (g.msaaTex) { g.msaaTex->Release(); g.msaaTex = nullptr; }
    if (!g.dev || !w || !h) { g.msaaSamples = 1; return; }

    // "msaa" in nativeengine_config.json overrides the auto-picked sample count:
    // -1/absent = auto (highest supported), 0/1 = off, 2/4/8 = forced.
    LoadConfig();
    int override_ = g_cfg.msaa;
    if (override_ == 0 || override_ == 1) { g.msaaSamples = 1; Log("[ne] MSAA forced off (config)\n"); return; }

    UINT samples = 1, quality = 0;
    UINT candidates3[3] = { 8u, 4u, 2u };
    UINT candidatesOverride[1] = { (UINT)override_ };
    UINT* candList = override_ > 1 ? candidatesOverride : candidates3;
    int candCount = override_ > 1 ? 1 : 3;
    for (int ci = 0; ci < candCount; ++ci) {
        UINT candidate = candList[ci];
        UINT q = 0;
        if (SUCCEEDED(g.dev->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, candidate, &q)) && q > 0) {
            samples = candidate; quality = 0; break;
        }
    }
    g.msaaSamples = samples;
    if (samples <= 1) { Log("[ne] MSAA not supported for this format, running without it\n"); return; }

    D3D11_TEXTURE2D_DESC dd{}; dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; dd.SampleDesc.Count = samples; dd.SampleDesc.Quality = quality;
    dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (SUCCEEDED(g.dev->CreateTexture2D(&dd, nullptr, &g.msaaTex)) && g.msaaTex) {
        g.dev->CreateRenderTargetView(g.msaaTex, nullptr, &g.msaaRTV);
        Log("[ne] MSAA %ux at %ux%u\n", samples, w, h);
    } else {
        g.msaaSamples = 1;
    }
}

// The scene depth used to be D3D11_BIND_DEPTH_STENCIL only; SSAO needs to read it back
// as a texture, which a depth format cannot bind as both at once -- has to be a
// TYPELESS resource with two views: a DSV interpreting it as a real depth format for
// the normal draw path, and an SRV interpreting the same bits as a plain color format
// for reading. Depends on g.msaaSamples, so call this AFTER CreateMSAATargets().
// Falls back to a depth-only buffer (no SRV, the format this always used before SSAO)
// whenever the combined DEPTH_STENCIL|SHADER_RESOURCE typeless format the SSAO path
// needs doesn't create cleanly. That combination failing to create at all -- not just
// the view, the TEXTURE ITSELF -- on some GPU/driver left the whole scene with zero
// depth buffer, not just AO disabled: every draw assuming g.dsv exists broke at once,
// which is what actually crashed and corrupted UI layout on a machine this wasn't
// tested on, nothing about AO specifically. AO simply won't run without a depthSRV
// (DrawSSAO already checks for that), so falling back here is a silent, safe downgrade.
static void CreateSceneDepth(UINT w, UINT h) {
    if (g.dsv) { g.dsv->Release(); g.dsv = nullptr; }
    if (g.depthSRV) { g.depthSRV->Release(); g.depthSRV = nullptr; }
    if (g.depthTex) { g.depthTex->Release(); g.depthTex = nullptr; }

    D3D11_TEXTURE2D_DESC dd{}; dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_R24G8_TYPELESS; dd.SampleDesc.Count = g.msaaSamples; dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    bool withSRV = SUCCEEDED(g.dev->CreateTexture2D(&dd, nullptr, &g.depthTex)) && g.depthTex;

    if (!withSRV) {
        Log("[ne] scene depth: combined depth+SRV format failed, falling back to depth-only (no AO)\n");
        D3D11_TEXTURE2D_DESC dd2{}; dd2.Width = w; dd2.Height = h; dd2.MipLevels = 1; dd2.ArraySize = 1;
        dd2.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd2.SampleDesc.Count = g.msaaSamples; dd2.Usage = D3D11_USAGE_DEFAULT;
        dd2.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(g.dev->CreateTexture2D(&dd2, nullptr, &g.depthTex)) || !g.depthTex) {
            Log("[ne] scene depth tex FAILED entirely\n"); return;
        }
        g.dev->CreateDepthStencilView(g.depthTex, nullptr, &g.dsv);
        Log("[ne] scene depth: depth-only fallback ok, samples=%u\n", g.msaaSamples);
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{}; dvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dvd.ViewDimension = g.msaaSamples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    HRESULT hrDsv = g.dev->CreateDepthStencilView(g.depthTex, &dvd, &g.dsv);
    D3D11_SHADER_RESOURCE_VIEW_DESC svd{}; svd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    if (g.msaaSamples > 1) { svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS; }
    else { svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; svd.Texture2D.MipLevels = 1; }
    HRESULT hrSrv = g.dev->CreateShaderResourceView(g.depthTex, &svd, &g.depthSRV);
    if (FAILED(hrDsv) || !g.dsv) {
        // The texture itself is fine but the DSV specifically isn't -- same safety net,
        // rebuild depth-only from scratch rather than leave a half-working state.
        Log("[ne] scene depth: DSV creation failed (0x%08X), falling back to depth-only\n", hrDsv);
        if (g.depthSRV) { g.depthSRV->Release(); g.depthSRV = nullptr; }
        if (g.depthTex) { g.depthTex->Release(); g.depthTex = nullptr; }
        D3D11_TEXTURE2D_DESC dd2{}; dd2.Width = w; dd2.Height = h; dd2.MipLevels = 1; dd2.ArraySize = 1;
        dd2.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd2.SampleDesc.Count = g.msaaSamples; dd2.Usage = D3D11_USAGE_DEFAULT;
        dd2.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (SUCCEEDED(g.dev->CreateTexture2D(&dd2, nullptr, &g.depthTex)) && g.depthTex)
            g.dev->CreateDepthStencilView(g.depthTex, nullptr, &g.dsv);
        return;
    }
    Log("[ne] scene depth: dsv=0x%08X srv=0x%08X samples=%u\n", hrDsv, hrSrv, g.msaaSamples);
}

static void CreateOutlineTarget(UINT w, UINT h) {
    if (g.outlineRTV) { g.outlineRTV->Release(); g.outlineRTV = nullptr; }
    if (g.outlineSRV) { g.outlineSRV->Release(); g.outlineSRV = nullptr; }
    if (g.outlineTex) { g.outlineTex->Release(); g.outlineTex = nullptr; }
    if (!g.dev || !w || !h) return;
    D3D11_TEXTURE2D_DESC dd{}; dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_R8_UNORM; dd.SampleDesc.Count = 1; dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(g.dev->CreateTexture2D(&dd, nullptr, &g.outlineTex)) && g.outlineTex) {
        g.dev->CreateRenderTargetView(g.outlineTex, nullptr, &g.outlineRTV);
        g.dev->CreateShaderResourceView(g.outlineTex, nullptr, &g.outlineSRV);
    }
}
// Same rule NDevice::EffDSV() uses (a private member there, so re-stated here for a
// free function): only the backbuffer's own depth view matches g.dsv when a non-
// backbuffer target is bound -- using it against an offscreen RT of a different size
// is what burned the glow buffers white, elsewhere in this file.
static ID3D11DepthStencilView* OutlineEffDSV() {
    return (g.curDSV == g.dsv && g.curRTV != MainSceneRTV()) ? nullptr : g.curDSV;
}
// Re-draws the just-issued geometry into the mask, solid white, testing (not writing)
// against the SAME depth the real draw just wrote -- LEQUAL means the identical depth
// value passes, so the silhouette only appears where the character is actually visible
// this frame; whatever already occludes it (walls, other players) blocks it exactly
// the same way it blocks the real draw. Restores PS/render targets afterward so
// nothing else notices this ran.
static void DrawOutlineMaskCopy(bool indexed, UINT count, UINT startOrIdx, INT baseV) {
    if (!g_outlineEnabled || !g_prog.isSkinned || !g.outlineRTV || !g.ctx) return;
    if (!BuildOutlinePipeline(g.dev)) return;
    ID3D11RenderTargetView* rtv = g.outlineRTV;
    g.ctx->OMSetRenderTargets(1, &rtv, OutlineEffDSV());
    g.ctx->OMSetDepthStencilState(g.dsNoWrite, 0);
    float bf[4] = { 0,0,0,0 };
    g.ctx->OMSetBlendState(nullptr, bf, 0xffffffff);
    g.ctx->PSSetShader(g_maskWhitePS, nullptr, 0);
    if (indexed) g.ctx->DrawIndexed(count, startOrIdx, baseV);
    else g.ctx->Draw(count, startOrIdx);
    g.ctx->PSSetShader(g_prog.ps, nullptr, 0);
    g.ctx->OMSetRenderTargets(1, &g.curRTV, OutlineEffDSV());
}
// Composites the accumulated mask onto the real backbuffer (colored ring at the
// silhouette edges) and clears the mask for the next frame. Called once per frame from
// Present(), right before the actual swapchain Present -- by then the whole scene is
// already sitting in g.rtv, which is exactly what needs to be composited over.
static void CompositeOutline() {
    if (!g_outlineEnabled || !g.outlineSRV || !g.rtv || !g.ctx) return;
    if (!BuildBlit(g.dev) || !BuildOutlinePipeline(g.dev)) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(g.ctx->Map(g_outlineCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float cb[4] = { g.bbW ? 1.0f / g.bbW : 0.f, g.bbH ? 1.0f / g.bbH : 0.f, 3.0f, 0.f };
        memcpy(m.pData, cb, sizeof(cb));
        g.ctx->Unmap(g_outlineCB, 0);
    }
    g.ctx->OMSetRenderTargets(1, &g.rtv, nullptr);
    D3D11_VIEWPORT vp{}; vp.Width = (float)g.bbW; vp.Height = (float)g.bbH; vp.MaxDepth = 1.f;
    g.ctx->RSSetViewports(1, &vp);
    g.ctx->IASetInputLayout(nullptr);
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->VSSetShader(g_blitVS, nullptr, 0);
    g.ctx->PSSetShader(g_outlinePS, nullptr, 0);
    g.ctx->PSSetShaderResources(0, 1, &g.outlineSRV);
    g.ctx->PSSetSamplers(0, 1, &g_blitSamp);
    g.ctx->PSSetConstantBuffers(0, 1, &g_outlineCB);
    float bf[4] = { 0,0,0,0 };
    g.ctx->OMSetBlendState(g_outlineBlend, bf, 0xffffffff);
    g.ctx->OMSetDepthStencilState(g.dsOff, 0);
    g.ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr; g.ctx->PSSetShaderResources(0, 1, &nul);
    float clr[4] = { 0,0,0,0 };
    g.ctx->ClearRenderTargetView(g.outlineRTV, clr);
    g.ctx->OMSetRenderTargets(1, &g.curRTV, OutlineEffDSV());
}

// diagnostic counters
static ID3D11Texture2D* g_sceneTex = nullptr;
static ID3D11ShaderResourceView* g_sceneSRV = nullptr;
// SCN weapon effects render into a 256x256 target that the client clears white.
// The UI uses the same size through the fixed-function path, so only the first
// programmable draw after binding that target may neutralize the clear alpha.
static bool g_effectTargetNeedsAlphaClear = false;
static LONG dbg_IUP = 0;
static float g_fogMap[5] = {};
static volatile LONG g_fogMapValid = 0;
// draws the game asked for and we did NOT execute, by reason
static LONG dbg_skipVS = 0, dbg_skipFVF = 0, dbg_skipBuf = 0, dbg_skipProg = 0;
// The per-map FullSceneGlow weights, in thousandths. CBgInfo_ParseRendererSection
// (0x011948A0) reads FullSceneGlow{,Org,Peri}ColorRev out of the map's bginfo and
// CMapRenderSettings_Apply (0x011A2A10) publishes them to these globals on map load.
// Out of line because Present holds objects with destructors and MSVC will not accept
// __try in a function that needs unwinding.
// ClearView (rectangle clears) lives on ID3D11DeviceContext1. Queried once and cached;
// null on a runtime that does not have it, in which case Clear falls back to wiping the
// whole target as before.
static ID3D11DeviceContext1* Ctx1() {
    static ID3D11DeviceContext1* c1 = nullptr;
    static bool tried = false;
    if (!tried) { tried = true; if (g.ctx) g.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1); }
    return c1;
}

static void ReadGlowWeights(int out[3]) {
    const float* g = (const float*)S4(0x0256F790);
    __try { for (int i = 0; i < 3; ++i) out[i] = (int)(g[i] * 1000.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = -1; out[1] = -1; out[2] = -1; }
}

static LONG dbg_DIP = 0, dbg_DP = 0, dbg_UP = 0, dbg_progActive = 0, dbg_progOK = 0, dbg_noStream = 0, dbg_noDecl = 0, dbg_noIL = 0, dbg_Clear = 0, dbg_SetRT = 0, dbg_texUnlock = 0, dbg_updateTex = 0, dbg_fvfDrew = 0, dbg_fvfSkip = 0, dbg_sceneCopy = 0;
// Measurement: how much of the frame goes INSIDE our backend vs the rest (the game).
static LONGLONG t_inBackend = 0, t_lastPresent = 0, t_frameTotal = 0;
static LONGLONG QPC() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
struct Stopwatch { LONGLONG t0; Stopwatch() : t0(QPC()) {} ~Stopwatch() { t_inBackend += QPC() - t0; } };
static DWORD dbg_lastFvf = 0;

// ---- backbuffer dump to BMP (a reliable capture of what we render) ----
static void WriteBMP(const char* path, UINT w, UINT h, const BYTE* bgra, UINT pitch) {
    UINT rowBytes = w * 4, imgSize = rowBytes * h;
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + imgSize;
    ih.biSize = sizeof(ih); ih.biWidth = (LONG)w; ih.biHeight = (LONG)h; ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB; ih.biSizeImage = imgSize;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return; DWORD wr;
    WriteFile(f, &fh, sizeof(fh), &wr, nullptr); WriteFile(f, &ih, sizeof(ih), &wr, nullptr);
    for (int y = (int)h - 1; y >= 0; --y) WriteFile(f, bgra + (size_t)y * pitch, rowBytes, &wr, nullptr);
    CloseHandle(f);
}
static void DumpBB() {
    if (!g.sc || !g.dev || !g.ctx) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* stg = nullptr;
    if (SUCCEEDED(g.dev->CreateTexture2D(&sd, nullptr, &stg)) && stg) {
        g.ctx->CopyResource(stg, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g.ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) { WriteBMP("ne_frame.bmp", d.Width, d.Height, (BYTE*)m.pData, m.RowPitch); g.ctx->Unmap(stg, 0); }
        stg->Release();
    }
    bb->Release();
}

// ---- blend states (D3DRS_ALPHABLENDENABLE/SRCBLEND/DESTBLEND -> D3D11) ----
static D3D11_BLEND MapBlend(DWORD b) {
    switch (b) {
        case 1: return D3D11_BLEND_ZERO; case 2: return D3D11_BLEND_ONE;
        case 3: return D3D11_BLEND_SRC_COLOR; case 4: return D3D11_BLEND_INV_SRC_COLOR;
        case 5: return D3D11_BLEND_SRC_ALPHA; case 6: return D3D11_BLEND_INV_SRC_ALPHA;
        case 7: return D3D11_BLEND_DEST_ALPHA; case 8: return D3D11_BLEND_INV_DEST_ALPHA;
        case 9: return D3D11_BLEND_DEST_COLOR; case 10: return D3D11_BLEND_INV_DEST_COLOR;
        case 11: return D3D11_BLEND_SRC_ALPHA_SAT; default: return D3D11_BLEND_ONE;
    }
}
struct BlendCache { DWORD s, d, op; ID3D11BlendState* bs; };
static BlendCache g_blendCache[256]; static int g_blendCount = 0;
// D3DRS_BLENDOP (171). It was pinned to ADD, so every blend the game asked to SUBTRACT,
// REVSUBTRACT, MIN or MAX was added instead. That is what turned the weapon charge glow
// into a white block: the game renders it into a 256x256 buffer it clears to WHITE and
// composites that buffer with a non-additive op, where white is a no-op. Forced to ADD,
// white plus the scene saturates and you get an opaque square.
static D3D11_BLEND_OP MapBlendOp(DWORD o) {
    switch (o) {
        case 2:  return D3D11_BLEND_OP_SUBTRACT;      // D3DBLENDOP_SUBTRACT
        case 3:  return D3D11_BLEND_OP_REV_SUBTRACT;  // D3DBLENDOP_REVSUBTRACT
        case 4:  return D3D11_BLEND_OP_MIN;           // D3DBLENDOP_MIN
        case 5:  return D3D11_BLEND_OP_MAX;           // D3DBLENDOP_MAX
        default: return D3D11_BLEND_OP_ADD;           // D3DBLENDOP_ADD (1) and unset
    }
}
// D3D11 does not accept COLOR factors on the alpha channel: they are translated to the equivalent.
static D3D11_BLEND MapBlendAlpha(DWORD b) {
    D3D11_BLEND x = MapBlend(b);
    switch (x) {
        case D3D11_BLEND_SRC_COLOR:      return D3D11_BLEND_SRC_ALPHA;
        case D3D11_BLEND_INV_SRC_COLOR:  return D3D11_BLEND_INV_SRC_ALPHA;
        case D3D11_BLEND_DEST_COLOR:     return D3D11_BLEND_DEST_ALPHA;
        case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
        default: return x;
    }
}
static ID3D11BlendState* GetBlend(DWORD s, DWORD d, DWORD op = 1) {
    if (!s) s = 5; if (!d) d = 6; if (!op) op = 1;
    CtxLock lk; // cache shared between threads
    for (int i = 0; i < g_blendCount; ++i)
        if (g_blendCache[i].s == s && g_blendCache[i].d == d && g_blendCache[i].op == op) return g_blendCache[i].bs;
    // NEVER create an uncacheable state: this used to fall through and build a fresh
    // ID3D11BlendState on every draw, leaking one per draw with nothing releasing them.
    // It was unreachable while the key was just (src,dst) -- adding the blend op multiplied
    // the combinations, the table filled, and memory climbed to 3.5GB in seconds.
    if (g_blendCount >= (int)(sizeof(g_blendCache) / sizeof(g_blendCache[0]))) {
        static LONG once = 0;
        if (InterlockedIncrement(&once) == 1) Log("[ne] blend cache full (%d): reusing\n", g_blendCount);
        for (int i = 0; i < g_blendCount; ++i)
            if (g_blendCache[i].s == s && g_blendCache[i].d == d) return g_blendCache[i].bs;
        return g_blendCache[0].bs;
    }
    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = MapBlend(s); bd.RenderTarget[0].DestBlend = MapBlend(d); bd.RenderTarget[0].BlendOp = MapBlendOp(op);
    // D3D9 WITHOUT D3DRS_SEPARATEALPHABLENDENABLE (the game never turns it on) blends
    // alpha with the SAME factors as color. It was pinned to ONE/INV_SRC_ALPHA: on the
    // additive effects (alphablend2 material) the color added up fine but the alpha
    // came out of a different formula and the quad ended up with a wrong alpha.
    bd.RenderTarget[0].SrcBlendAlpha = MapBlendAlpha(s); bd.RenderTarget[0].DestBlendAlpha = MapBlendAlpha(d); bd.RenderTarget[0].BlendOpAlpha = MapBlendOp(op);
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* bs = nullptr; if (g.dev) g.dev->CreateBlendState(&bd, &bs);
    g_blendCache[g_blendCount++] = { s, d, op, bs };
    return bs;
}

static const char* DeclUsageName(BYTE u) {
    switch (u) {
        case D3DDECLUSAGE_POSITION: return "POSITION";
        case D3DDECLUSAGE_BLENDWEIGHT: return "BLENDWEIGHT";
        case D3DDECLUSAGE_BLENDINDICES: return "BLENDINDICES";
        case D3DDECLUSAGE_NORMAL: return "NORMAL";
        case D3DDECLUSAGE_PSIZE: return "PSIZE";
        case D3DDECLUSAGE_TEXCOORD: return "TEXCOORD";
        case D3DDECLUSAGE_TANGENT: return "TANGENT";
        case D3DDECLUSAGE_BINORMAL: return "BINORMAL";
        case D3DDECLUSAGE_COLOR: return "COLOR";
        default: return "TEXCOORD";
    }
}
static DXGI_FORMAT DeclType(BYTE t) {
    switch (t) {
        case D3DDECLTYPE_FLOAT1: return DXGI_FORMAT_R32_FLOAT;
        case D3DDECLTYPE_FLOAT2: return DXGI_FORMAT_R32G32_FLOAT;
        case D3DDECLTYPE_FLOAT3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case D3DDECLTYPE_FLOAT4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case D3DDECLTYPE_D3DCOLOR: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case D3DDECLTYPE_UBYTE4: return DXGI_FORMAT_R8G8B8A8_UINT;
        case D3DDECLTYPE_UBYTE4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DDECLTYPE_SHORT2: return DXGI_FORMAT_R16G16_SINT;
        case D3DDECLTYPE_SHORT4: return DXGI_FORMAT_R16G16B16A16_SINT;
        case D3DDECLTYPE_SHORT2N: return DXGI_FORMAT_R16G16_SNORM;
        case D3DDECLTYPE_SHORT4N: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case D3DDECLTYPE_FLOAT16_2: return DXGI_FORMAT_R16G16_FLOAT;
        case D3DDECLTYPE_FLOAT16_4: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
}
struct NVDecl;
static ID3D11InputLayout* BuildInputLayout(struct NVDecl* decl, const void* vsbc, SIZE_T vslen);

// ---- FVF helpers (the common subset used by the UI) ----
static UINT FvfStride(DWORD fvf) {
    UINT s = 0;
    DWORD pos = fvf & D3DFVF_POSITION_MASK;
    if (pos == D3DFVF_XYZRHW) s += 16;
    else { s += 12; int beta = 0; if (pos == D3DFVF_XYZB1) beta = 1; else if (pos == D3DFVF_XYZB2) beta = 2; else if (pos == D3DFVF_XYZB3) beta = 3; else if (pos == D3DFVF_XYZB4) beta = 4; else if (pos == D3DFVF_XYZB5) beta = 5; s += beta * 4; }
    if (fvf & D3DFVF_NORMAL) s += 12;
    if (fvf & D3DFVF_PSIZE) s += 4;
    if (fvf & D3DFVF_DIFFUSE) s += 4;
    if (fvf & D3DFVF_SPECULAR) s += 4;
    UINT tex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < tex; ++i) { UINT code = (fvf >> (16 + i * 2)) & 0x3; s += (code == 0 ? 2 : code == 1 ? 3 : code == 2 ? 4 : 1) * 4; }
    return s;
}

// ---- default shader (FVF path): XYZRHW/XYZ + DIFFUSE + 1 texcoord ----
static const char* kDefaultHLSL = R"(
cbuffer C : register(b0) { float4x4 gWorld; float4x4 gView; float4x4 gProj; float vpW; float vpH; float hasTex; float isRHW;
                           float hasCol; float p0; float p1; float p2;
                           float4 stageC[4]; float4 stageA[4]; float4 gTFactor;
                           float4 gFog; float4 gFogColor; };
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
struct VSIn  { float4 pos : POSITION; float4 col : COLOR0; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; };
VSOut VS(VSIn i) {
    VSOut o;
    if (isRHW > 0.5) {
        // XYZRHW: pos already in screen pixels -> clip space
        o.pos = float4(i.pos.x / (vpW*0.5) - 1.0, 1.0 - i.pos.y / (vpH*0.5), i.pos.z, 1.0);
    } else {
        // XYZ fixed-function: transform by World*View*Proj (SetTransform)
        float4 p = float4(i.pos.xyz, 1.0);
        o.pos = mul(mul(mul(p, gWorld), gView), gProj);
    }
    // If the FVF does NOT carry DIFFUSE we still declare a COLOR0 (the VS requires it)
    // but it points at offset 0 = the POSITION: reading it would give garbage and saturate to white.
    o.col = (hasCol > 0.5) ? i.col : float4(1,1,1,1);
    o.uv0 = i.uv0; o.uv1 = i.uv1; o.uv2 = i.uv2; o.uv3 = i.uv3;
    return o;
}
// D3DTA_*: 0=DIFFUSE 1=CURRENT 2=TEXTURE 3=TFACTOR (the engine only uses these 4,
// see table 0x02570040 in the client).
float4 TexArg(float a, float4 t, float4 diff, float4 cur) {
    if (a < 0.5) return diff;
    if (a < 1.5) return cur;
    if (a < 2.5) return t;
    return gTFactor;
}
// D3DTEXTUREOP: 2=SELECTARG1 3=SELECTARG2 4=MODULATE 5=MODULATE2X 7=ADD
// 8=ADDSIGNED 9=ADDSIGNED2X 10=SUBTRACT (table 0x0257001C; 1=DISABLE is cut earlier)
float4 TexOp(float o, float4 a1, float4 a2) {
    if (o < 2.5) return a1;
    if (o < 3.5) return a2;
    if (o < 4.5) return a1 * a2;
    if (o < 5.5) return saturate(a1 * a2 * 2.0);
    if (o < 7.5) return saturate(a1 + a2);
    if (o < 8.5) return saturate(a1 + a2 - 0.5);
    if (o < 9.5) return saturate((a1 + a2 - 0.5) * 2.0);
    return saturate(a1 - a2);
}
float4 ApplyFog(float4 c, float4 pos, float rhw) {
    if (rhw < 0.5 && gFog.z > 0.5) {
        float d = 1.0 / max(pos.w, 1e-8);
        float f = saturate((gFog.y - d) / max(gFog.y - gFog.x, 0.001));
        c.rgb = lerp(gFogColor.rgb, c.rgb, f);
    }
    return c;
}
float4 PSCore(VSOut i, float rhw) {
    float4 diff = i.col;
    float4 cur = diff;   // at stage 0, CURRENT == DIFFUSE
    [unroll] for (int st = 0; st < 4; ++st) {
        if (stageC[st].x < 1.5) break;   // D3DTOP_DISABLE cuts the cascade
        // Without a texture at stage 0 the draw is simply Gouraud: the vertex color
        // comes out. Returning white for D3DTA_TEXTURE left alpha at 1 and the whole
        // quad looked opaque (the jump square and the railgun one).
        if (st == 0 && stageC[0].w < 0.5) break;
        // A stage without a texture contributes nothing either. The engine leaves the
        // old COLOROP on stage 1 (SELECTARG2 was observed) and without this cut stage 1
        // overwrote what stage 0 computed with DIFFUSE: a washed-out rectangle on top
        // of the effect.
        if (st > 0 && stageC[st].w < 0.5) break;
        float4 t = float4(1,1,1,1);
        if (stageC[st].w > 0.5) {
            if      (st == 0) t = tex0.Sample(s0, i.uv0);
            else if (st == 1) t = tex1.Sample(s1, i.uv1);
            else if (st == 2) t = tex2.Sample(s2, i.uv2);
            else              t = tex3.Sample(s3, i.uv3);
            // A8 is an alpha mask (fonts): in D3D9 the RGB comes out white, not black
            if (stageA[st].w > 0.5) t = float4(1, 1, 1, t.a);
        }
        float3 c = TexOp(stageC[st].x, TexArg(stageC[st].y, t, diff, cur), TexArg(stageC[st].z, t, diff, cur)).rgb;
        float  a = TexOp(stageA[st].x, TexArg(stageA[st].y, t, diff, cur), TexArg(stageA[st].z, t, diff, cur)).a;
        cur = float4(c, a);
    }
    // alpha test (D3DRS_ALPHATESTENABLE/ALPHAFUNC/ALPHAREF): without it the sprites
    // cut out by alpha came out as opaque squares.
    if (p0 > 0.0) {
        if (p1 < 0.5) { if (cur.a <  p0) discard; }   // GREATER / GREATEREQUAL
        else          { if (cur.a >= p0) discard; }   // LESS / LESSEQUAL
    }
    return ApplyFog(cur, i.pos, rhw);
}
float4 PS(VSOut i) : SV_Target { return PSCore(i, isRHW); }
// Variant for the .fx passes that declare PixelShader = null (projected shadow,
// TextureNoise): the vertex shader is supplied by the effect and only outputs
// COLOR0 + TEXCOORD0, so the PS cannot ask for more texcoords than that.
struct VSOut1 { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv0 : TEXCOORD0; };
float4 PS1(VSOut1 i) : SV_Target {
    VSOut o;
    o.pos = i.pos; o.col = i.col;
    o.uv0 = i.uv0; o.uv1 = i.uv0; o.uv2 = i.uv0; o.uv3 = i.uv0;
    return PSCore(o, 1.0);
}
)";

static bool BuildDefaultPipeline() {
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    UINT mf = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
    if (FAILED(D3DCompile(kDefaultHLSL, strlen(kDefaultHLSL), nullptr, nullptr, nullptr,
                          "VS", "vs_5_0", mf, 0, &vsb, &err))) {
        Log("[ne] VS compile fail: %s\n", err ? (char*)err->GetBufferPointer() : "?"); if (err) err->Release(); return false;
    }
    if (FAILED(D3DCompile(kDefaultHLSL, strlen(kDefaultHLSL), nullptr, nullptr, nullptr,
                          "PS", "ps_5_0", mf, 0, &psb, &err))) {
        Log("[ne] PS compile fail: %s\n", err ? (char*)err->GetBufferPointer() : "?"); if (err) err->Release(); return false;
    }
    g.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g.vsDefault);
    g.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g.psDefault);
    g.vsDefaultBlob = vsb; // we use it to build input layouts from the FVF
    psb->Release();
    { ID3DBlob* pb1 = nullptr; ID3DBlob* e1 = nullptr;
      if (SUCCEEDED(D3DCompile(kDefaultHLSL, strlen(kDefaultHLSL), nullptr, nullptr, nullptr, "PS1", "ps_5_0", mf, 0, &pb1, &e1)) && pb1) {
          g.dev->CreatePixelShader(pb1->GetBufferPointer(), pb1->GetBufferSize(), nullptr, &g.psFF1); pb1->Release();
      } else Log("[ne] PS1 compile fail: %s\n", e1 ? (char*)e1->GetBufferPointer() : "?");
      if (e1) e1->Release(); }

    // TEST: solid green PS
    const char* solid = "float4 PS():SV_Target{return float4(0,1,0,1);}";
    ID3DBlob* sb = nullptr; ID3DBlob* se = nullptr;
    if (SUCCEEDED(D3DCompile(solid, strlen(solid), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &sb, &se))) {
        g.dev->CreatePixelShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &g.psSolid); sb->Release();
    }
    if (se) se->Release();

    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sizeof(CBData); cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g.dev->CreateBuffer(&cbd, nullptr, &g.cb);
    // The game toggles FOGENABLE PER OBJECT, but the effect's cbuffer is uploaded only
    // once per BeginPass: in there the enable was stale and the whole pass came out
    // without fog. It goes in its own cbuffer, updated on every draw.
    { D3D11_BUFFER_DESC fd{}; fd.ByteWidth = 32; fd.Usage = D3D11_USAGE_DYNAMIC;
      fd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; fd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      g.dev->CreateBuffer(&fd, nullptr, &g.fogCB); }

    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_ANISOTROPIC; sd.MaxAnisotropy = 16;
    // MaxLOD=0: D3DX only fills mip 0; the higher levels stay empty -> white at
    // grazing angles. Clamping to mip 0 means an empty mip is never sampled. (slight
    // aliasing at distance; the real fix would be generating the mips, but they are DXT/BC).
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    g.dev->CreateSamplerState(&sd, &g.samp);

    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g.dev->CreateBlendState(&bd, &g.blend);

    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = FALSE;
    g.dev->CreateRasterizerState(&rd, &g.rsNoCull);
    g_rsCache[0] = { 0.f, 0.f, D3DCULL_NONE, D3DFILL_SOLID, g.rsNoCull }; g_rsCount = 1;
    (void)&GetRasterizer;

    // depth-stencil states: test+write / test+no-write / off
    D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable = TRUE; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; g.dev->CreateDepthStencilState(&ds, &g.dsWrite);
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; g.dev->CreateDepthStencilState(&ds, &g.dsNoWrite);
    ds.DepthEnable = FALSE; g.dev->CreateDepthStencilState(&ds, &g.dsOff);
    return true;
}

// ================= resources (real D3D11) =================
struct NVBuf : Unk<IDirect3DVertexBuffer9> {
    UINT len; DWORD usage, fvf; D3DPOOL pool; ID3D11Buffer* buf = nullptr; BYTE* shadow = nullptr;
    NVBuf(UINT l, DWORD u, DWORD f, D3DPOOL p) : len(l), usage(u), fvf(f), pool(p) {
        shadow = (BYTE*)calloc((l ? l : 1) + 8192, 1); // slack: the game writes past the end (see textures)
        D3D11_BUFFER_DESC d{}; d.ByteWidth = l ? l : 16; d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g.dev->CreateBuffer(&d, nullptr, &buf);
    }
    ~NVBuf() { if (buf) buf->Release(); free(shadow); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_VERTEXBUFFER; }
    // The engine uses POOLS (CVertexBufferPool_D3D): one big buffer from which it locks
    // a small range per draw. Uploading the whole buffer on every unlock re-sent
    // megabytes per draw and made the driver rename the buffer every time.
    UINT lockOff = 0, lockLen = 0; DWORD lockFlags = 0;
    STDMETHOD(Lock)(UINT off, UINT size, void** ppb, DWORD flags) {
        if (off > len) off = len;
        lockOff = off; lockFlags = flags;
        lockLen = size ? size : (len - off);
        if (lockOff + lockLen > len) lockLen = len - lockOff;
        *ppb = shadow + off; return D3D_OK;
    }
    STDMETHOD(Unlock)() {
        CtxLock lk;
        if (!buf || !lockLen) return D3D_OK;
        // D3DLOCK_DISCARD = the game rewrites everything; otherwise it is a pool append
        // and NO_OVERWRITE lets the driver keep using the rest without syncing.
        bool discard = (lockFlags & D3DLOCK_DISCARD) != 0;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(g.ctx->Map(buf, 0, discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m))) {
            if (discard) memcpy(m.pData, shadow, len);
            else memcpy((BYTE*)m.pData + lockOff, shadow + lockOff, lockLen);
            g.ctx->Unmap(buf, 0);
        }
        lockLen = 0;
        return D3D_OK;
    }
    STDMETHOD(GetDesc)(D3DVERTEXBUFFER_DESC* d) { if (d) { d->Format = D3DFMT_VERTEXDATA; d->Type = D3DRTYPE_VERTEXBUFFER; d->Usage = usage; d->Pool = pool; d->Size = len; d->FVF = fvf; } return D3D_OK; }
};
struct NIBuf : Unk<IDirect3DIndexBuffer9> {
    UINT len; DWORD usage; D3DFORMAT fmt; D3DPOOL pool; ID3D11Buffer* buf = nullptr; BYTE* shadow = nullptr;
    NIBuf(UINT l, DWORD u, D3DFORMAT f, D3DPOOL p) : len(l), usage(u), fmt(f), pool(p) {
        shadow = (BYTE*)calloc((l ? l : 1) + 8192, 1); // slack: the game writes past the end (see textures)
        D3D11_BUFFER_DESC d{}; d.ByteWidth = l ? l : 16; d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_INDEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g.dev->CreateBuffer(&d, nullptr, &buf);
    }
    ~NIBuf() { if (buf) buf->Release(); free(shadow); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_INDEXBUFFER; }
    // same as NVBuf: the engine locks ranges of a pool (CIndexBufferPool_D3D).
    UINT lockOff = 0, lockLen = 0; DWORD lockFlags = 0;
    STDMETHOD(Lock)(UINT off, UINT size, void** ppb, DWORD flags) {
        if (off > len) off = len;
        lockOff = off; lockFlags = flags;
        lockLen = size ? size : (len - off);
        if (lockOff + lockLen > len) lockLen = len - lockOff;
        *ppb = shadow + off; return D3D_OK;
    }
    STDMETHOD(Unlock)() {
        CtxLock lk;
        if (!buf || !lockLen) return D3D_OK;
        bool discard = (lockFlags & D3DLOCK_DISCARD) != 0;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(g.ctx->Map(buf, 0, discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m))) {
            if (discard) memcpy(m.pData, shadow, len);
            else memcpy((BYTE*)m.pData + lockOff, shadow + lockOff, lockLen);
            g.ctx->Unmap(buf, 0);
        }
        lockLen = 0;
        return D3D_OK;
    }
    STDMETHOD(GetDesc)(D3DINDEXBUFFER_DESC* d) { if (d) { d->Format = fmt; d->Type = D3DRTYPE_INDEXBUFFER; d->Usage = usage; d->Pool = pool; d->Size = len; } return D3D_OK; }
};
struct NSurface : Unk<IDirect3DSurface9> {
    // cached (backbuffer/RT/depth): the game over-Releases; if they get deleted the
    // cache keeps a dead pointer -> AddRef on a garbage vtable -> crash.
    STDMETHOD_(ULONG, Release)() { LONG r = InterlockedDecrement(&this->ref); if (r < 1) { this->ref = 1; r = 1; } return r; }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) { if (riid == IID_NE_RTView) { *ppv = &neRT; return S_OK; } *ppv = this; this->AddRef(); return S_OK; }
    NE_RTView neRT;
    ID3D11Texture2D* rtTex = nullptr; // its own texture if it is an RT/DS created by CreateRenderTarget/DepthStencil
    UINT w, h; D3DFORMAT fmt; BYTE* mem; UINT pitch;
    NSurface(UINT ww, UINT hh, D3DFORMAT f) : w(ww), h(hh), fmt(f) { pitch = (w ? w : 1) * 4; mem = (BYTE*)calloc((size_t)pitch * (h ? h : 1) + 8192, 1); neRT.w = w; neRT.h = h; } // fixed slack (RAM)
    ~NSurface() { free(mem); if (neRT.rtv) neRT.rtv->Release(); if (neRT.dsv) neRT.dsv->Release(); if (rtTex) rtTex->Release(); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }
    STDMETHOD(GetContainer)(REFIID, void** ppc) { *ppc = nullptr; return D3D_OK; }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC* d) { if (d) { d->Format = fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = 0; d->Pool = D3DPOOL_DEFAULT; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->MultiSampleQuality = 0; d->Width = w; d->Height = h; } return D3D_OK; }
    STDMETHOD(LockRect)(D3DLOCKED_RECT* lr, const RECT*, DWORD) { if (lr) { lr->Pitch = pitch; lr->pBits = mem; } return D3D_OK; }
    STDMETHOD(UnlockRect)() { return D3D_OK; }
    STDMETHOD(GetDC)(HDC* p) { *p = nullptr; return D3D_OK; }
    STDMETHOD(ReleaseDC)(HDC) { return D3D_OK; }
};
static void SafeUpdateSubresource(ID3D11Texture2D* t, UINT lvl, const void* src, UINT pitch, UINT size);
// surface of a texture level: on Unlock it uploads the data to the D3D11 texture
struct NTexSurface : Unk<IDirect3DSurface9> {
    // we keep them cached and the game sometimes over-Releases -> if one is deleted,
    // the cache keeps a dead pointer and the next AddRef crashes (use-after-free).
    STDMETHOD_(ULONG, Release)() { LONG r = InterlockedDecrement(&this->ref); if (r < 1) { this->ref = 1; r = 1; } return r; }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) { if (riid == IID_NE_RTView) { *ppv = &neRT; return S_OK; } *ppv = this; this->AddRef(); return S_OK; }
    NE_RTView neRT; // rtv/dsv of the parent NTexture (for render-to-texture)
    struct NTexture* owner = nullptr; // parent texture: we tell it which level was filled
    ID3D11Texture2D* tex; UINT level, rowPitch, levelSize; BYTE* data; UINT w, h; D3DFORMAT fmt; int conv;
    NTexSurface(ID3D11Texture2D* t, UINT l, UINT rp, UINT ls, BYTE* d, UINT ww, UINT hh, D3DFORMAT f, int cv) : tex(t), level(l), rowPitch(rp), levelSize(ls), data(d), w(ww), h(hh), fmt(f), conv(cv) {}
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_SURFACE; }
    STDMETHOD(GetContainer)(REFIID, void** ppc) { *ppc = nullptr; return D3D_OK; }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC* d) { if (d) { d->Format = fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = 0; d->Pool = D3DPOOL_DEFAULT; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->MultiSampleQuality = 0; d->Width = w; d->Height = h; } return D3D_OK; }
    STDMETHOD(LockRect)(D3DLOCKED_RECT* lr, const RECT* rect, DWORD) {
        UINT off = 0;
        if (rect && w) { UINT bpp = rowPitch / w; off = (UINT)rect->top * rowPitch + (UINT)rect->left * bpp; }
        BYTE* base = data ? data : Scratch(); // never NULL: the game does memset(NULL+off) anyway
        if (!data) off = 0;
        if (lr) { lr->Pitch = rowPitch; lr->pBits = base + off; }
        return D3D_OK;
    }
    STDMETHOD(UnlockRect)() {
        NE_TexLevelFilled(owner, level); // D3DX fills the mips per surface, not per texture
        if (!tex || !data) return D3D_OK;
        CtxLock lk;
        // through SafeUpdateSubresource like every other upload: this is the path the client
        // really uses (GetSurfaceLevel -> LockRect -> UnlockRect), and it was bypassing both
        // the crash guard and the diagnostics.
        if (conv == 0) { SafeUpdateSubresource(tex, level, data, rowPitch, levelSize); return D3D_OK; }
        std::vector<BYTE> bgra((size_t)w * h * 4);
        for (UINT i = 0; i < w * h; ++i) {
            if (conv == 4) { bgra[i * 4] = data[i * 4]; bgra[i * 4 + 1] = data[i * 4 + 1]; bgra[i * 4 + 2] = data[i * 4 + 2]; bgra[i * 4 + 3] = 255; }
            else if (conv == 3) { bgra[i * 4] = data[i * 3]; bgra[i * 4 + 1] = data[i * 3 + 1]; bgra[i * 4 + 2] = data[i * 3 + 2]; bgra[i * 4 + 3] = 255; }
            else { BYTE L, A; if (conv == 1) { L = data[i]; A = 255; } else { L = data[i * 2]; A = data[i * 2 + 1]; } bgra[i * 4] = L; bgra[i * 4 + 1] = L; bgra[i * 4 + 2] = L; bgra[i * 4 + 3] = A; }
        }
        SafeUpdateSubresource(tex, level, bgra.data(), w * 4, w * 4 * h);
        return D3D_OK;
    }
    STDMETHOD(GetDC)(HDC* p) { *p = nullptr; return D3D_OK; }
    STDMETHOD(ReleaseDC)(HDC) { return D3D_OK; }
};

// conv: 0=direct, 1=L8->BGRA(LLL1), 2=A8L8->BGRA(LLLA)
static void FmtInfo(D3DFORMAT f, DXGI_FORMAT& dx, bool& comp, UINT& blk, UINT& bpp, int& conv) {
    conv = 0;
    switch ((DWORD)f) {
        case 0x31545844: dx = DXGI_FORMAT_BC1_UNORM; comp = true; blk = 8; bpp = 0; return;  // DXT1
        case 0x32545844: dx = DXGI_FORMAT_BC2_UNORM; comp = true; blk = 16; bpp = 0; return; // DXT2
        case 0x33545844: dx = DXGI_FORMAT_BC2_UNORM; comp = true; blk = 16; bpp = 0; return; // DXT3
        case 0x34545844: dx = DXGI_FORMAT_BC3_UNORM; comp = true; blk = 16; bpp = 0; return; // DXT4
        case 0x35545844: dx = DXGI_FORMAT_BC3_UNORM; comp = true; blk = 16; bpp = 0; return; // DXT5
        case D3DFMT_A8R8G8B8: dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 4; return;
        case D3DFMT_X8R8G8B8: dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 4; conv = 4; return; // X = NO alpha -> force 255
        case D3DFMT_A8: dx = DXGI_FORMAT_A8_UNORM; comp = false; blk = 0; bpp = 1; return;
        case D3DFMT_L8: dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 1; conv = 1; return; // gray -> LLL1
        case D3DFMT_A8L8: dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 2; conv = 2; return; // gray+alpha -> LLLA
        case D3DFMT_R8G8B8: dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 3; conv = 3; return; // 24-bit BGR -> BGRA (D3D11 has no 24-bit)
        case D3DFMT_R5G6B5: dx = DXGI_FORMAT_B5G6R5_UNORM; comp = false; blk = 0; bpp = 2; return;
        case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: dx = DXGI_FORMAT_B5G5R5A1_UNORM; comp = false; blk = 0; bpp = 2; return;
        case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: dx = DXGI_FORMAT_B4G4R4A4_UNORM; comp = false; blk = 0; bpp = 2; return;
        default: break;
    }
    // Everything we do not know silently became BGRA at 4 bytes per pixel. If the
    // game actually hands us a different layout (paletted, A8B8G8R8, a FourCC we do
    // not map) the upload runs with the wrong pitch and the texture comes out
    // unusable, with nothing in the log to say so. Report each unknown format once.
    {
        static DWORD seen[16] = {}; static int nseen = 0; bool known = false;
        for (int i = 0; i < nseen; ++i) if (seen[i] == (DWORD)f) { known = true; break; }
        if (!known && nseen < 16) {
            seen[nseen++] = (DWORD)f;
            char cc[5] = { (char)(f & 0xFF), (char)((f >> 8) & 0xFF), (char)((f >> 16) & 0xFF), (char)((f >> 24) & 0xFF), 0 };
            for (int i = 0; i < 4; ++i) if (cc[i] < 32 || cc[i] > 126) cc[i] = '.';
            Log("[ne] UNKNOWN texture format 0x%08X ('%s') -> assuming BGRA 4bpp\n", (DWORD)f, cc);
        }
    }
    dx = DXGI_FORMAT_B8G8R8A8_UNORM; comp = false; blk = 0; bpp = 4;
}
// slack: the game sometimes writes rows at full pitch past the exact size (sub-rect
// locks). Real drivers over-allocate; we do the same.
// FIXED slack: a proportional slack (sz/2) inflated RAM by ~50% and we hit the
// client's ~1.7GB ceiling (it would not let you create a room = OOM).
// How much RAM the CPU-side copies actually hold. The client is 32-bit and dies
// around 1.7GB, so this number decides whether the shadows are worth attacking.
static volatile LONG g_shadowKB = 0;
LONG ShadowKB() { return g_shadowKB; }
static BYTE* TexAlloc(size_t sz) {
    BYTE* p = (BYTE*)calloc(sz + 8192, 1);
    if (p) g_shadowKB += (LONG)((sz + 8192) / 1024);
    return p;
}
static void TexFree(BYTE* p, size_t sz) {
    if (!p || (uintptr_t)p < 0x10000) return;
    free(p);
    g_shadowKB -= (LONG)((sz + 8192) / 1024);
}
// The driver (nvwgf2um) was AVing reading address 1 inside UpdateSubresource when
// entering a channel. Before uploading we check that the source is readable and of
// the size we claim; if not, the upload is skipped instead of killing the client.
static bool SrcReadable(const void* p, size_t n) {
    // MINIMAL on purpose. Walking the range with VirtualQuery sank the fps (24), and
    // querying only the endpoints rejected valid buffers and skipped uploads ->
    // broken UI. All we are after here is the garbage pointer we saw (it was 1);
    // the low range check is enough for that. The __try in SafeUpdateSubresource
    // covers the rest at no per-call cost.
    return p && (uintptr_t)p >= 0x10000 && n != 0;
}
static void SafeUpdateSubresource(ID3D11Texture2D* t, UINT lvl, const void* src, UINT pitch, UINT size) {
    if (!t || !SrcReadable(src, size)) {
        static LONG n = 0; if (InterlockedIncrement(&n) <= 8)
            Log("[ne] UpdateSubresource SKIPPED tex=%p lvl=%u src=%p pitch=%u size=%u\n", t, lvl, src, pitch, size);
        return;
    }
    // A texture we upload as ALL WHITE or ALL ZERO is almost always a load that produced
    // nothing: the surface was created at the right size but never filled with real
    // pixels. That is what the weapon charge effect's 128x128 source texture turned out
    // to be in the RenderDoc capture -- blank white, ShaderRead only, its single write a
    // CPU upload. Sampling a few bytes is cheap (a few hundred uploads per session) and
    // it names the texture instead of leaving it to be found frame by frame.
    if (lvl == 0 && size >= 64) {
        __try {
            // Scan EVERY byte with an early exit. Sampling 16 spread-out points looked cheap
            // and was useless: unrelated textures matched at those offsets and got reported
            // as blank, so the first pass produced a list of false positives.
            const BYTE* b = (const BYTE*)src;
            BYTE first = b[0]; bool uniform = true;
            for (UINT i = 1; i < size; ++i) if (b[i] != first) { uniform = false; break; }
            // Only genuinely uniform uploads are worth a line, and one per distinct texture:
            // the font atlas alone is uploaded blank hundreds of times.
            if (uniform) {
                static ID3D11Texture2D* seen[64]; static LONG nseen = 0;
                bool dup = false;
                for (LONG i = 0; i < nseen && i < 64; ++i) if (seen[i] == t) { dup = true; break; }
                if (!dup && nseen < 64) {
                    seen[nseen++] = t;
                    Log("[tex] VACIA 0x%02X tex=%p pitch=%u size=%u (%ux%u si es BGRA)\n",
                        first, t, pitch, size, pitch / 4, pitch ? size / pitch : 0);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    __try { g.ctx->UpdateSubresource(t, lvl, nullptr, src, pitch, size); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static LONG n = 0; if (InterlockedIncrement(&n) <= 8)
            Log("[ne] UpdateSubresource CRASHED tex=%p lvl=%u src=%p pitch=%u size=%u\n", t, lvl, src, pitch, size);
    }
}
struct NTexture : Unk<IDirect3DTexture9> {
    UINT w, h, levels; DWORD usage; D3DFORMAT fmt; D3DPOOL pool;
    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr; // if it is a RENDERTARGET, the game draws here and samples it later
    UINT filled = 0;      // mip levels the game actually uploaded (D3DX sometimes only fills 0)
    UINT srvLevels = 0;   // levels the current SRV exposes
    DXGI_FORMAT dxfmt; bool comp = false; UINT blk = 0, bpp = 4; int conv = 0; bool isA8 = false;
    // The pointers live INSIDE the object. As a vector they lived in a small heap
    // block sitting right next to the texture buffers the game overruns (which is why
    // TexAlloc reserves 8192 extra), and shadow[0] was seen holding 1: the font atlas
    // stopped uploading and the glyphs disappeared.
    enum { MAXLV = 16 };
    BYTE* shadow[MAXLV] = {};
    std::vector<IDirect3DSurface9*> surfCache; // D3D9 returns THE SAME surface per level (AddRef), not a new one
    UINT LW(UINT l) { UINT x = w >> l; return x ? x : 1; }
    UINT LH(UINT l) { UINT x = h >> l; return x ? x : 1; }
    UINT RowPitch(UINT l) { return comp ? ((LW(l) + 3) / 4) * blk : LW(l) * bpp; }
    UINT LevelSize(UINT l) { return comp ? RowPitch(l) * ((LH(l) + 3) / 4) : RowPitch(l) * LH(l); }
    static UINT FullChain(UINT ww, UINT hh) { UINT n = 1, x = ww > hh ? ww : hh; while (x > 1) { x >>= 1; n++; } return n; }
    NTexture(UINT ww, UINT hh, UINT lv, DWORD u, D3DFORMAT f, D3DPOOL p) : w(ww ? ww : 1), h(hh ? hh : 1), levels(lv ? lv : 1), usage(u), fmt(f), pool(p) {
        FmtInfo(f, dxfmt, comp, blk, bpp, conv); isA8 = (f == D3DFMT_A8);
        // Do NOT create mips the game did not ask for: forcing them broke the UI buttons
        // (the BC re-encode is lossy) and raised RAM by ~120MB. We only generate the
        // levels when the texture ALREADY declares a chain and D3DX only fills 0.
        if (levels > MAXLV) levels = MAXLV;
        for (UINT l = 0; l < levels; ++l) {
            shadow[l] = TexAlloc(LevelSize(l));
            if (!shadow[l]) Log("[ne] TexAlloc FAILED %ux%u lvl=%u size=%u (RAM full)\n", w, h, l, LevelSize(l));
        } // guard page right at the end
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = levels; d.ArraySize = 1;
        d.Format = dxfmt; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // The game copies the backbuffer into the scene texture with StretchRect
        // (CRenderer_D3D::UpdateScreenTexture: GetBackBuffer -> GetSurface -> StretchRect).
        // Writing there needs a render target view; without it the copy never happened
        // and the texture kept the old frame -> the floor reflected the lobby.
        // CTexture_D3D::CreateTexture_ColorTarget (@0x01DE5240) asks for the flag through
        // D3DXCreateTexture(usage=D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT) and our
        // CheckDeviceFormat accepts everything, so the flag arrives intact: honoring it is enough.
        bool isRT = (u & D3DUSAGE_RENDERTARGET) != 0 && !comp;
        bool isDS = (u & D3DUSAGE_DEPTHSTENCIL) != 0;
        if (isRT) d.BindFlags |= D3D11_BIND_RENDER_TARGET;
        if (isDS) { d.BindFlags = D3D11_BIND_DEPTH_STENCIL; d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; }
        if (SUCCEEDED(g.dev->CreateTexture2D(&d, nullptr, &tex)) && tex) {
            if (!isDS) g.dev->CreateShaderResourceView(tex, nullptr, &srv);
            if (isRT) g.dev->CreateRenderTargetView(tex, nullptr, &rtv);
            if (isDS) g.dev->CreateDepthStencilView(tex, nullptr, &dsv);
        }
    }
    ID3D11DepthStencilView* dsv = nullptr;
    ~NTexture() {
        if (rtv) rtv->Release(); if (dsv) dsv->Release(); if (srv) srv->Release(); if (tex) tex->Release();
        for (UINT l = 0; l < levels; ++l) TexFree(shadow[l], LevelSize(l));
    }
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return D3D_OK; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return D3D_OK; }
    STDMETHOD(FreePrivateData)(REFGUID) { return D3D_OK; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return D3DRTYPE_TEXTURE; }
    STDMETHOD_(DWORD, SetLOD)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetLOD)() { return 0; }
    STDMETHOD_(DWORD, GetLevelCount)() { return levels; }
    STDMETHOD(SetAutoGenFilterType)(D3DTEXTUREFILTERTYPE) { return D3D_OK; }
    STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)() { return D3DTEXF_LINEAR; }
    STDMETHOD_(void, GenerateMipSubLevels)() {}
    STDMETHOD(GetLevelDesc)(UINT lvl, D3DSURFACE_DESC* d) { if (d) { d->Format = fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = usage; d->Pool = pool; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->MultiSampleQuality = 0; d->Width = LW(lvl); d->Height = LH(lvl); } return D3D_OK; }
    STDMETHOD(GetSurfaceLevel)(UINT lvl, IDirect3DSurface9** pp) {
        if (lvl >= levels) lvl = 0;
        if (surfCache.size() < levels) surfCache.resize(levels, nullptr);
        if (!surfCache[lvl]) {
            NTexSurface* ns = new NTexSurface(tex, lvl, RowPitch(lvl), LevelSize(lvl), shadow[lvl], LW(lvl), LH(lvl), fmt, conv);
            if (lvl == 0) { ns->neRT.rtv = rtv; ns->neRT.dsv = dsv; } // the game uses level 0 as a render target
            ns->neRT.w = LW(lvl); ns->neRT.h = LH(lvl); ns->neRT.tex = tex; ns->neRT.sub = lvl;
            ns->owner = this; // to report which level was filled (D3DX uploads mips per surface)
            surfCache[lvl] = ns;
        }
        surfCache[lvl]->AddRef(); *pp = surfCache[lvl]; return D3D_OK;
    }
    STDMETHOD(LockRect)(UINT lvl, D3DLOCKED_RECT* lr, const RECT* rect, DWORD) {
        if (lvl >= levels) lvl = 0;
        UINT rp = RowPitch(lvl), off = 0;
        if (rect && !comp) {
            // sub-rect (the fonts lock per glyph). Clamp: the game sometimes asks for
            // an out-of-range rect and the memcpy runs past the buffer -> crash.
            UINT top = rect->top > 0 ? (UINT)rect->top : 0, left = rect->left > 0 ? (UINT)rect->left : 0;
            UINT h = LH(lvl), w2 = LW(lvl);
            if (top >= h) top = h ? h - 1 : 0;
            if (left >= w2) left = w2 ? w2 - 1 : 0;
            off = top * rp + left * bpp;
            // font atlas diagnostics (the nametag crash lives here)
            static LONG fl = 0;
            if (isA8 && InterlockedIncrement(&fl) <= 25)
                Log("[font] lock tex=%ux%u lvl=%u fmt=%d pitch=%u rect=(%d,%d)-(%d,%d) off=%u size=%u\n",
                    w, h, lvl, (int)fmt, rp, (int)rect->left, (int)rect->top, (int)rect->right, (int)rect->bottom, off, LevelSize(lvl));
        }
        BYTE* base = shadow[lvl];
        // shadow[0] was seen holding 1 in the font atlas: the pointer becomes useless,
        // the upload is skipped and the glyphs disappear. If it is not addressable we
        // replace it here so the game writes into good memory.
        if (base && !SrcReadable(base, LevelSize(lvl))) {
            static LONG n = 0; if (InterlockedIncrement(&n) <= 8)
                Log("[ne] unreadable shadow %ux%u lvl=%u ptr=%p -> reallocated\n", w, h, lvl, base);
            TexFree(base, LevelSize(lvl));
            base = shadow[lvl] = TexAlloc(LevelSize(lvl));   // last resort: loses the glyphs already drawn
        }
        if (!base) { base = shadow[lvl] = TexAlloc(LevelSize(lvl)); } // realloc: freed after uploading
        if (!base) { base = Scratch(); off = 0; } // the alloc failed: never NULL (see Scratch)
        if (lr) { lr->Pitch = rp; lr->pBits = base + off; }
        return D3D_OK;
    }
    // The SRV exposes ONLY the levels the game filled. If we expose empty mips the GPU
    // picks them at grazing angles and you see garbage; if we turn them all off
    // (MaxLOD=0) we lose mipmapping and aliasing/shimmering appears. This uses what
    // is actually there.
    // The game (through D3DX) usually uploads ONLY level 0. We generate the rest
    // ourselves: decode BC -> halve -> re-encode. Without this there are no mips and
    // the floor shimmers at grazing angles.
    void GenerateMipsCPU() {
        if (levels < 2 || !tex || !shadow[0]) return;
        bool bc1 = (fmt == D3DFMT_DXT1);
        if (comp && fmt != D3DFMT_DXT1 && fmt != D3DFMT_DXT3 && fmt != D3DFMT_DXT5) return;
        std::vector<BYTE> cur, nxt;
        UINT cw = LW(0), ch = LH(0);
        cur.resize((size_t)cw * ch * 4);
        if (comp) { // decode level 0
            const BYTE* s = shadow[0]; UINT bw = (cw + 3) / 4, bh = (ch + 3) / 4;
            for (UINT by = 0; by < bh; ++by) for (UINT bx = 0; bx < bw; ++bx) {
                BYTE px[64]; BcDecodeBlock(s + ((size_t)by * bw + bx) * blk, bc1, px);
                for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                    UINT gx = bx * 4 + x, gy = by * 4 + y; if (gx >= cw || gy >= ch) continue;
                    memcpy(&cur[((size_t)gy * cw + gx) * 4], px + (y * 4 + x) * 4, 4);
                }
            }
        } else if (bpp == 4) { // BGRA -> RGBA (only to average; we convert back on upload)
            memcpy(cur.data(), shadow[0], (size_t)cw * ch * 4);
        } else return;
        for (UINT lvl = 1; lvl < levels; ++lvl) {
            UINT nw = LW(lvl), nh = LH(lvl);
            nxt.assign((size_t)nw * nh * 4, 0);
            for (UINT y = 0; y < nh; ++y) for (UINT x = 0; x < nw; ++x) { // box 2x2
                UINT x0 = x * 2, y0 = y * 2, x1 = (x0 + 1 < cw) ? x0 + 1 : x0, y1 = (y0 + 1 < ch) ? y0 + 1 : y0;
                for (int c = 0; c < 4; ++c) {
                    int s = cur[((size_t)y0 * cw + x0) * 4 + c] + cur[((size_t)y0 * cw + x1) * 4 + c]
                          + cur[((size_t)y1 * cw + x0) * 4 + c] + cur[((size_t)y1 * cw + x1) * 4 + c];
                    nxt[((size_t)y * nw + x) * 4 + c] = (BYTE)(s / 4);
                }
            }
            if (comp) {
                UINT bw = (nw + 3) / 4, bh = (nh + 3) / 4;
                std::vector<BYTE> enc((size_t)bw * bh * blk, 0);
                for (UINT by = 0; by < bh; ++by) for (UINT bx = 0; bx < bw; ++bx) {
                    BYTE px[64] = { 0 };
                    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                        UINT gx = bx * 4 + x, gy = by * 4 + y;
                        if (gx < nw && gy < nh) memcpy(px + (y * 4 + x) * 4, &nxt[((size_t)gy * nw + gx) * 4], 4);
                    }
                    BcEncodeBlock(px, bc1, &enc[((size_t)by * bw + bx) * blk]);
                }
                SafeUpdateSubresource(tex, lvl, enc.data(), bw * blk, (UINT)enc.size());
            } else {
                SafeUpdateSubresource(tex, lvl, nxt.data(), nw * 4, nw * 4 * nh);
            }
            cur.swap(nxt); cw = nw; ch = nh;
        }
        filled = levels;
        { static LONG n = 0; if (InterlockedIncrement(&n) <= 8) Log("[mip] generated %u levels for %ux%u (comp=%d)\n", levels, w, h, comp ? 1 : 0); }
    }
    void RefreshSRV() {
        if (filled == 1 && levels > 1) GenerateMipsCPU(); // D3DX only uploaded level 0
        UINT want = filled ? filled : 1;
        if (want == srvLevels || !tex) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = dxfmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MostDetailedMip = 0; sd.Texture2D.MipLevels = want;
        ID3D11ShaderResourceView* nv = nullptr;
        if (SUCCEEDED(g.dev->CreateShaderResourceView(tex, &sd, &nv)) && nv) {
            if (srv) srv->Release();
            srv = nv; srvLevels = want;
        }
    }
    STDMETHOD(UnlockRect)(UINT lvl) {
        InterlockedIncrement(&dbg_texUnlock);
        if (lvl >= levels || !tex) return D3D_OK;
        if (lvl + 1 > filled) { filled = lvl + 1; RefreshSRV(); }
        CtxLock lk;
        if (!shadow[lvl]) return D3D_OK;
        if (conv == 0) {
            SafeUpdateSubresource(tex, lvl, shadow[lvl], RowPitch(lvl), LevelSize(lvl));
            // freeing the CPU copy: it doubled ALL the texture memory and with the
            // client's ~1.7GB ceiling the allocations started failing.
            // If the game locks again, LockRect reallocates it.
            // ONLY compressed ones (DXT): they are the bulk of the RAM and are uploaded in one go.
            // The uncompressed ones (e.g. the A8 font atlas) are written INCREMENTALLY
            // glyph by glyph: if we free, the realloc comes back zeroed and the letters are lost.
            // (test) do NOT free the shadow: if the game re-locks, the zeroed realloc
            // left the body texture blank. Correctness before RAM.
            return D3D_OK;
        }
        UINT ww = LW(lvl), hh = LH(lvl); const BYTE* s = shadow[lvl];
        std::vector<BYTE> bgra((size_t)ww * hh * 4);
        for (UINT i = 0; i < ww * hh; ++i) {
            if (conv == 4) { bgra[i * 4] = s[i * 4]; bgra[i * 4 + 1] = s[i * 4 + 1]; bgra[i * 4 + 2] = s[i * 4 + 2]; bgra[i * 4 + 3] = 255; } // X8: no alpha -> opaque
            else if (conv == 3) { bgra[i * 4] = s[i * 3]; bgra[i * 4 + 1] = s[i * 3 + 1]; bgra[i * 4 + 2] = s[i * 3 + 2]; bgra[i * 4 + 3] = 255; }
            else { BYTE L, A; if (conv == 1) { L = s[i]; A = 255; } else { L = s[i * 2]; A = s[i * 2 + 1]; } bgra[i * 4] = L; bgra[i * 4 + 1] = L; bgra[i * 4 + 2] = L; bgra[i * 4 + 3] = A; }
        }
        SafeUpdateSubresource(tex, lvl, bgra.data(), ww * 4, ww * 4 * hh);
        return D3D_OK;
    }
    STDMETHOD(AddDirtyRect)(const RECT*) { return D3D_OK; }
};
static void NE_TexLevelFilled(NTexture* t, UINT lvl) {
    if (!t || lvl + 1 <= t->filled) return;
    t->filled = lvl + 1; t->RefreshSRV();
}
// Almost every map's light ramp is a 256x1 (or 256x2) strip, which is what the ramp
// latch below keys on -- except indoorlight02.dds, which is the SAME kind of ramp
// (a horizontal gradient) but stored as a wasteful 256x256 DXT1 square, so it never
// matched the h<=2 check and this map's lighting kept flickering.
// Can't key on the filename (this D3D9 layer never sees the file path, only the
// created D3D11 texture), so this checks the actual pixel content instead: sample the
// BC1 endpoint colour at a few x-positions across the top/middle/bottom rows -- a real
// horizontal ramp repeats the same row all the way down, while an ordinary 256x256
// lightmap/texture (also seen at stage 1) varies well beyond DXT1's own quantisation
// noise between rows.
static bool NE_LooksLikeSquareRamp(NTexture* n) {
    if (!n || !n->shadow[0] || !n->comp || n->blk != 8 || n->w != 256 || n->h != 256) return false;
    UINT pitch = n->RowPitch(0); // bytes per row of 4x4 blocks
    const BYTE* data = n->shadow[0];
    auto color0At = [&](UINT row, UINT blockCol) -> WORD {
        const BYTE* p = data + (row / 4) * pitch + (size_t)blockCol * 8;
        return (WORD)(p[0] | (p[1] << 8));
    };
    auto close = [](WORD a, WORD b) {
        int dr = (int)((a >> 11) & 0x1F) - (int)((b >> 11) & 0x1F);
        int dg = (int)((a >> 5) & 0x3F) - (int)((b >> 5) & 0x3F);
        int db = (int)(a & 0x1F) - (int)(b & 0x1F);
        return abs(dr) <= 1 && abs(dg) <= 2 && abs(db) <= 1;
    };
    const UINT cols[3] = { 0, 32, 63 };
    for (UINT c : cols) {
        WORD top = color0At(0, c), mid = color0At(128, c), bot = color0At(252, c);
        if (!close(top, mid) || !close(top, bot)) return false;
    }
    return true;
}
struct NVDecl : Unk<IDirect3DVertexDeclaration9> {
    D3DVERTEXELEMENT9 el[32]; UINT n = 0;
    NVDecl(const D3DVERTEXELEMENT9* e) {
        if (e) { while (n < 31 && e[n].Stream != 0xFF) { el[n] = e[n]; n++; } }
    }
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(GetDeclaration)(D3DVERTEXELEMENT9* e, UINT* c) { if (c) *c = n; if (e) memcpy(e, el, n * sizeof(el[0])); return D3D_OK; }
};

// input layout cache keyed by (decl, vs bytecode)
struct ILCacheEntry { void* decl; const void* vs; ID3D11InputLayout* il; };
static ILCacheEntry g_ilCache[256]; static int g_ilCount = 0;

static ID3D11InputLayout* BuildInputLayout(NVDecl* decl, const void* vsbc, SIZE_T vslen) {
    if (!decl || !vsbc || !g.dev) return nullptr;
    CtxLock lk; // same: cache shared between threads
    for (int i = 0; i < g_ilCount; ++i)
        if (g_ilCache[i].decl == decl && g_ilCache[i].vs == vsbc) return g_ilCache[i].il;
    D3D11_INPUT_ELEMENT_DESC ie[32]; UINT k = 0;
    for (UINT i = 0; i < decl->n && k < 32; ++i) {
        const D3DVERTEXELEMENT9& e = decl->el[i];
        ie[k].SemanticName = DeclUsageName(e.Usage);
        ie[k].SemanticIndex = e.UsageIndex;
        ie[k].Format = DeclType(e.Type);
        ie[k].InputSlot = e.Stream;
        ie[k].AlignedByteOffset = e.Offset;
        ie[k].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        ie[k].InstanceDataStepRate = 0;
        k++;
    }
    ID3D11InputLayout* il = nullptr;
    HRESULT hr = g.dev->CreateInputLayout(ie, k, vsbc, vslen, &il);
    if (FAILED(hr)) { Log("[ne] CreateInputLayout FAIL 0x%08X n=%u\n", hr, k); return nullptr; }
    if (g_ilCount < 256) g_ilCache[g_ilCount++] = { decl, vsbc, il };
    return il;
}

// input layout from an FVF (the game uses SetFVF instead of a vertex declaration)
struct FVFCacheEntry { DWORD fvf; const void* vs; ID3D11InputLayout* il; };
static FVFCacheEntry g_fvfCache[256]; static int g_fvfCount = 0;
static ID3D11InputLayout* BuildInputLayoutFVF(DWORD fvf, const void* vsbc, SIZE_T vslen) {
    if (!fvf || !vsbc || !g.dev) return nullptr;
    CtxLock lk; // global cache: without the lock, two loading threads corrupt it (intermittent crash)
    for (int i = 0; i < g_fvfCount; ++i) if (g_fvfCache[i].fvf == fvf && g_fvfCache[i].vs == vsbc) return g_fvfCache[i].il;
    D3D11_INPUT_ELEMENT_DESC e[16]; UINT k = 0, off = 0;
    auto add = [&](const char* sem, UINT idx, DXGI_FORMAT f, UINT sz) {
        e[k] = { sem, idx, f, 0, off, D3D11_INPUT_PER_VERTEX_DATA, 0 }; k++; off += sz;
    };
    DWORD pos = fvf & D3DFVF_POSITION_MASK;
    if (pos == D3DFVF_XYZRHW) add("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 16);
    else {
        add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12);
        int beta = 0;
        if (pos == D3DFVF_XYZB1) beta = 1; else if (pos == D3DFVF_XYZB2) beta = 2;
        else if (pos == D3DFVF_XYZB3) beta = 3; else if (pos == D3DFVF_XYZB4) beta = 4; else if (pos == D3DFVF_XYZB5) beta = 5;
        if (beta > 0) {
            bool lastU = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0;
            int weights = lastU ? beta - 1 : beta;
            if (weights > 0) add("BLENDWEIGHT", 0, weights == 1 ? DXGI_FORMAT_R32_FLOAT : weights == 2 ? DXGI_FORMAT_R32G32_FLOAT : weights == 3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT, weights * 4);
            if (lastU) add("BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 4);
        }
    }
    if (fvf & D3DFVF_NORMAL) add("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12);
    if (fvf & D3DFVF_PSIZE) add("PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 4);
    if (fvf & D3DFVF_DIFFUSE) add("COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 4);
    if (fvf & D3DFVF_SPECULAR) add("COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, 4);
    if (!(fvf & D3DFVF_DIFFUSE)) { // the default VS always reads COLOR0; if the FVF lacks it we declare it anyway (offset 0, ignored)
        e[k] = { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }; k++;
    }
    UINT tc = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    // the default VS declares TEXCOORD0..3 (one set per texture stage); the ones the
    // FVF does not carry are declared anyway pointing at offset 0 and the PS ignores them.
    for (UINT i = tc; i < 4 && k < 16; ++i) { e[k] = { "TEXCOORD", i, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }; k++; }
    for (UINT i = 0; i < tc && k < 16; ++i) {
        // each texcoord can be 1/2/3/4 floats (D3DFVF_TEXCOORDSIZE). The character's
        // bones arrive as 4D TEXCOORDs (InBoneIndex/InBoneWeight).
        UINT code = (fvf >> (16 + i * 2)) & 0x3; // 0=2D 1=3D 2=4D 3=1D
        UINT fl = code == 0 ? 2 : code == 1 ? 3 : code == 2 ? 4 : 1;
        DXGI_FORMAT f = fl == 1 ? DXGI_FORMAT_R32_FLOAT : fl == 2 ? DXGI_FORMAT_R32G32_FLOAT : fl == 3 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        add("TEXCOORD", i, f, fl * 4);
    }
    ID3D11InputLayout* il = nullptr;
    HRESULT hr = g.dev->CreateInputLayout(e, k, vsbc, vslen, &il);
    if (FAILED(hr)) { Log("[ne] IL from FVF 0x%X FAIL 0x%08X\n", fvf, hr); return nullptr; }
    if (g_fvfCount < 256) g_fvfCache[g_fvfCount++] = { fvf, vsbc, il };
    return il;
}
struct NVShader : Unk<IDirect3DVertexShader9> {
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(GetFunction)(void*, UINT* c) { if (c) *c = 0; return D3D_OK; }
};
struct NPShader : Unk<IDirect3DPixelShader9> {
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(GetFunction)(void*, UINT* c) { if (c) *c = 0; return D3D_OK; }
};
struct NStateBlock : Unk<IDirect3DStateBlock9> {
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD(Capture)() { return D3D_OK; }
    STDMETHOD(Apply)() { return D3D_OK; }
};
struct NQuery : Unk<IDirect3DQuery9> {
    D3DQUERYTYPE t; NQuery(D3DQUERYTYPE tt) : t(tt) {}
    STDMETHOD(GetDevice)(IDirect3DDevice9** d) { *d = g_dev9; if (g_dev9) g_dev9->AddRef(); return D3D_OK; }
    STDMETHOD_(D3DQUERYTYPE, GetType)() { return t; }
    STDMETHOD_(DWORD, GetDataSize)() { return sizeof(BOOL); }
    STDMETHOD(Issue)(DWORD) { return D3D_OK; }
    STDMETHOD(GetData)(void* p, DWORD sz, DWORD) { if (p && sz >= sizeof(BOOL)) *(BOOL*)p = TRUE; return S_OK; }
};

// ================= device =================
struct NDevice : Unk<IDirect3DDevice9> {
    IDirect3D9* parent; HWND hwnd;
    // D3D9 state we track
    NVBuf* stream0 = nullptr; UINT stream0Stride = 0, stream0Off = 0;
    NIBuf* indices = nullptr; DWORD fvf = 0;
    NTexture* tex[8] = {};
    NVDecl* curDecl = nullptr;
    void* curVS = nullptr; void* curPS = nullptr; // the game's programmable shaders (through the effect)
    DWORD rs[256] = {};
    // Up to 32 types, not 8: D3DTSS_TEXCOORDINDEX is 11 and
    // D3DTSS_TEXTURETRANSFORMFLAGS is 24, so the old bound dropped the whole texture
    // coordinate transform in silence -- which is how an animated sprite sheet moves
    // its UVs.
    DWORD tss[8][32] = {};
    D3DMATRIX mTex[8] = {};      // D3DTS_TEXTURE0..7
    bool texMatSet[8] = {};
    DWORD ss[8][14] = {};   // sampler states (ADDRESSU/V/W, MAG/MIN/MIPFILTER)
    D3DMATRIX mWorld, mView, mProj; // fixed-function transforms (SetTransform)
    D3DVIEWPORT9 curVP{}; // current viewport (for the character preview inside its box)
    CBData lastDefaultCB{};
    bool defaultCBValid = false;

    UINT syncInterval = 1;   // D3DPRESENT_INTERVAL_IMMEDIATE -> 0 (no vsync)
    void SetSync(D3DPRESENT_PARAMETERS* pp) {
        if (!pp) return;
        syncInterval = (pp->PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE) ? 0u : 1u;
        LoadConfig();
        if (!g_cfg.vsync) syncInterval = 0;
        Log("[ne] PresentationInterval=0x%X -> SyncInterval=%u\n", pp->PresentationInterval, syncInterval);
    }
    NDevice(IDirect3D9* p, HWND h, D3DPRESENT_PARAMETERS* pp) : parent(p), hwnd(h) {
        SetSync(pp);
        if (parent) parent->AddRef();
        g.bbW = pp && pp->BackBufferWidth ? pp->BackBufferWidth : 1024;
        g.bbH = pp && pp->BackBufferHeight ? pp->BackBufferHeight : 768;
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2; sd.BufferDesc.Width = g.bbW; sd.BufferDesc.Height = g.bbH;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        // SHADER_INPUT: StretchRect needs to READ the backbuffer to scale it into the
        // blur/glow texture (D3D9 StretchRect scales; CopySubresourceRegion does not).
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT; sd.OutputWindow = h;
        // With BufferCount=1 and the bitblt model the CPU cannot run ahead of the GPU:
        // every Present serializes against the previous frame and the fps sink even
        // though the scene is ~160 draws. FLIP_DISCARD also avoids the DWM copy.
        sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        // A windowed FLIP_DISCARD swapchain is still vsync-locked by DWM composition
        // even when Present is called with SyncInterval=0: any frame that lands just
        // past the vblank deadline gets pushed to the NEXT one, which is what was
        // seen bouncing between the monitor's refresh rate and half of it (144<->72)
        // instead of scaling smoothly with load. ALLOW_TEARING lets a 0-interval
        // Present go out immediately instead of waiting for the next vblank.
        {
            IDXGIFactory2* f2 = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f2))) && f2) {
                IDXGIFactory5* f5 = nullptr;
                if (SUCCEEDED(f2->QueryInterface(IID_PPV_ARGS(&f5))) && f5) {
                    BOOL allow = FALSE;
                    if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))) && allow) {
                        g.tearingSupported = true;
                        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                    }
                    f5->Release();
                }
                f2->Release();
            }
            Log("[ne] tearing support: %d\n", (int)g.tearingSupported);
        }
        D3D_FEATURE_LEVEL fl;
        // The D3D11 debug layer (D3D11_3SDKLayers.dll) validates every call: it costs
        // performance and it also crashed by itself when entering a channel. It is only
        // turned on by setting NE_D3D11_DEBUG=1 in the environment.
        UINT flags = 0;
        { char e[8] = ""; if (GetEnvironmentVariableA("NE_D3D11_DEBUG", e, sizeof(e)) && e[0] == '1') flags = D3D11_CREATE_DEVICE_DEBUG; }
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &g.sc, &g.dev, &fl, &g.ctx);
        if (FAILED(hr)) {   // FLIP needs Win8+; without it, classic double buffering
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                D3D11_SDK_VERSION, &sd, &g.sc, &g.dev, &fl, &g.ctx);
        }
        if (FAILED(hr) && flags) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &g.sc, &g.dev, &fl, &g.ctx);
        Log("[ne] swapchain effect=%d buffers=%u hr=0x%08X\n", (int)sd.SwapEffect, sd.BufferCount, hr);
        Log("[ne] D3D11 create hr=0x%08X fl=0x%X %ux%u\n", hr, fl, g.bbW, g.bbH);
        if (SUCCEEDED(hr) && g.dev && SUCCEEDED(g.dev->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&g.iq)) && g.iq) Log("[ne] D3D11 debug layer ON\n");
        if (SUCCEEDED(hr) && g.sc) {
            if (SUCCEEDED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&g.bbTex)) && g.bbTex) g.dev->CreateRenderTargetView(g.bbTex, nullptr, &g.rtv);
            CreateMSAATargets(g.bbW, g.bbH);
            CreateSceneDepth(g.bbW, g.bbH);
            g.curRTV = g.msaaSamples > 1 ? g.msaaRTV : g.rtv; g.curDSV = g.dsv;
            CreateOutlineTarget(g.bbW, g.bbH);
            BuildDefaultPipeline();
            // Visible proof that it runs on D3D11: API + feature level + real GPU in
            // the window title (it shows up in any screenshot/video).
            if (hwnd) {
                char gpu[128] = "GPU";
                IDXGIDevice* dxgiDev = nullptr;
                if (SUCCEEDED(g.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) && dxgiDev) {
                    IDXGIAdapter* ad = nullptr;
                    if (SUCCEEDED(dxgiDev->GetAdapter(&ad)) && ad) {
                        DXGI_ADAPTER_DESC ades{};
                        if (SUCCEEDED(ad->GetDesc(&ades))) WideCharToMultiByte(CP_ACP, 0, ades.Description, -1, gpu, sizeof(gpu), nullptr, nullptr);
                        ad->Release();
                    }
                    dxgiDev->Release();
                }
                char title[256];
                wsprintfA(title, "S4 Client  |  Direct3D 11 (FL %d_%d) NativeEngine  |  %s", (fl >> 12) & 0xF, (fl >> 8) & 0xF, gpu);
                SetWindowTextA(hwnd, title);
                Log("[ne] %s\n", title);
            }
        }
        rs[D3DRS_ZENABLE] = 1; rs[D3DRS_ZWRITEENABLE] = 1; // defaults D3D9
        // D3D9 texture stage defaults: stage 0 modulates texture by diffuse and takes
        // alpha from the texture; from stage 1 onwards they are disabled.
        for (int s = 0; s < 8; ++s) {
            tss[s][D3DTSS_COLOROP] = (s == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
            tss[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE; tss[s][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
            tss[s][D3DTSS_ALPHAOP] = (s == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
            tss[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; tss[s][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
        }
        ZeroMemory(&mWorld, sizeof(mWorld)); mWorld._11 = mWorld._22 = mWorld._33 = mWorld._44 = 1.f; mView = mProj = mWorld;
        curVP.X = 0; curVP.Y = 0; curVP.Width = g.bbW; curVP.Height = g.bbH; curVP.MinZ = 0.f; curVP.MaxZ = 1.f;
        g_dev9 = this;
    }
    ~NDevice() { if (parent) parent->Release(); }

    void ApplyDefaultState() {
        CtxLock lk;
        // Reverted: routing this through EffDSV() to fix a cosmetic white edge on
        // character/UI silhouettes (RTV/DSV sample-count mismatch under MSAA) instead
        // caused the whole character to render solid white -- EffDSV() nulls the depth
        // here in a case this fixed-function path actually needs it for correct
        // multi-pass ordering. Back to raw g.curDSV until a fix is found that does not
        // regress this; the small edge artifact is far less bad than a fully white model.
        g.ctx->OMSetRenderTargets(1, &g.curRTV, g.curDSV);
        // Depth was hardcoded off here for EVERY fixed-function draw since the very
        // first commit -- fine for RHW quads (pre-transformed screen space, no
        // meaningful depth), but this path is also how world-space effects draw
        // (DrawPrimitiveUP/DrawIndexedPrimitiveUP: weapon trails, muzzle flashes,
        // particles), and disabling their depth test unconditionally means they never
        // compete with real scene depth -- they draw on top of everything (a weapon
        // effect appearing pinned to the character instead of at the gun) and are never
        // occluded by geometry between the camera and where they actually are (visible
        // through walls). RHW draws still force depth off; real XYZ draws now respect
        // ZENABLE/ZWRITEENABLE like the programmable path (BeginProgDraw) already does.
        bool isRHWfvf = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
        ID3D11DepthStencilState* fvfDss = isRHWfvf ? g.dsOff :
            (!rs[D3DRS_ZENABLE] ? g.dsOff : (rs[D3DRS_ZWRITEENABLE] ? g.dsWrite : g.dsNoWrite));
        g.ctx->OMSetDepthStencilState(fvfDss, 0);
        ApplyViewport();
        { float bs, sl; memcpy(&bs, &rs[D3DRS_DEPTHBIAS], 4); memcpy(&sl, &rs[D3DRS_SLOPESCALEDEPTHBIAS], 4); g.ctx->RSSetState(GetRasterizer(bs, sl, rs[D3DRS_CULLMODE], rs[D3DRS_FILLMODE])); }
        g.ctx->VSSetShader(g.vsDefault, nullptr, 0);
        g.ctx->PSSetShader(g.psDefault, nullptr, 0);
        float bf[4] = { 0,0,0,0 };
        if (rs[D3DRS_ALPHABLENDENABLE]) g.ctx->OMSetBlendState(GetBlend(rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND], rs[D3DRS_BLENDOP]), bf, 0xffffffff);
        else g.ctx->OMSetBlendState(nullptr, bf, 0xffffffff);
        g.ctx->PSSetSamplers(0, 1, &g.samp);
        // "White" TEST: if forcing opaque makes it disappear, there is an ADDITIVE pass on top.
        CBData cb{};
        memcpy(cb.world, &mWorld, 64); memcpy(cb.view, &mView, 64); memcpy(cb.proj, &mProj, 64);
        // XYZRHW coords are VIEWPORT-relative. On the BACKBUFFER the UI relies on the
        // full screen size (curVP can be left stale from an offscreen pass, so trusting
        // it there breaks the UI). Only when rendering into an OFFSCREEN target (the
        // 256x256 weapon-glow buffer) does the backbuffer size mis-map the RHW quad into
        // a corner and leave the white clear -> the white square. Viewport-based vpW
        // broke the UI (curVP goes stale on offscreen->backbuffer), so left as-is; the
        // real fix is on the program path, not here. See QUEUE.md #1.
        cb.vpW = (float)g.bbW; cb.vpH = (float)g.bbH;
        cb.hasTex = tex[0] ? (tex[0]->isA8 ? 2.f : 1.f) : 0.f;
        cb.isRHW = ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) ? 1.f : 0.f;
        cb.hasCol = (fvf & D3DFVF_DIFFUSE) ? 1.f : 0.f;
        for (int s = 0; s < 4; ++s) {
            cb.stageC[s][0] = (float)tss[s][D3DTSS_COLOROP];
            cb.stageC[s][1] = (float)tss[s][D3DTSS_COLORARG1];
            cb.stageC[s][2] = (float)tss[s][D3DTSS_COLORARG2];
            cb.stageC[s][3] = tex[s] ? 1.f : 0.f;
            cb.stageA[s][0] = (float)tss[s][D3DTSS_ALPHAOP];
            cb.stageA[s][1] = (float)tss[s][D3DTSS_ALPHAARG1];
            cb.stageA[s][2] = (float)tss[s][D3DTSS_ALPHAARG2];
            cb.stageA[s][3] = (tex[s] && tex[s]->isA8) ? 1.f : 0.f;
        }
        DWORD af = rs[D3DRS_ALPHAFUNC];
        cb.pad0 = rs[D3DRS_ALPHATESTENABLE] ? (rs[D3DRS_ALPHAREF] & 0xFF) / 255.f : 0.f;
        cb.pad1 = (af == D3DCMP_LESS || af == D3DCMP_LESSEQUAL) ? 1.f : 0.f;
        DWORD tf = rs[D3DRS_TEXTUREFACTOR];
        cb.tfactor[0] = ((tf >> 16) & 0xFF) / 255.f; cb.tfactor[1] = ((tf >> 8) & 0xFF) / 255.f;
        cb.tfactor[2] = (tf & 0xFF) / 255.f;         cb.tfactor[3] = ((tf >> 24) & 0xFF) / 255.f;
        if (!defaultCBValid || memcmp(&lastDefaultCB, &cb, sizeof(cb)) != 0) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(g.ctx->Map(g.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                memcpy(m.pData, &cb, sizeof(cb)); g.ctx->Unmap(g.cb, 0);
                lastDefaultCB = cb; defaultCBValid = true;
            }
        }
        g.ctx->VSSetConstantBuffers(0, 1, &g.cb);
        g.ctx->PSSetConstantBuffers(0, 1, &g.cb);
        // A draw with texcoords and NO texture comes out as flat color (white if the
        // vertex color is white): that is exactly the shape of the jump square.
        //
        // Keyed by the state combination, NOT by a call counter. With a counter the
        // 20 slots were spent during startup and the draws we actually care about
        // (weapon trails, the jump wave) never got logged, which is why this went
        // three rounds without a diagnosis.
        if ((fvf & D3DFVF_TEXCOUNT_MASK) && (!tex[0] || !tex[0]->srv)) {
            static DWORD seen[64] = {}; static int nseen = 0;
            DWORD key = fvf ^ (tss[0][D3DTSS_COLOROP] << 8) ^ (tss[0][D3DTSS_COLORARG1] << 12)
                      ^ (tss[0][D3DTSS_ALPHAOP] << 16) ^ (tss[0][D3DTSS_ALPHAARG1] << 20)
                      ^ (rs[D3DRS_SRCBLEND] << 24) ^ (rs[D3DRS_DESTBLEND] << 28);
            bool known = false;
            for (int i = 0; i < nseen; ++i) if (seen[i] == key) { known = true; break; }
            if (!known && nseen < 64) {
                seen[nseen++] = key;
                Log("[flat] fvf=0x%X tex0=%p aop=%lu aa1=%lu aa2=%lu atest=%lu ref=%lu\n",
                    fvf, tex[0], tss[0][D3DTSS_ALPHAOP], tss[0][D3DTSS_ALPHAARG1],
                    tss[0][D3DTSS_ALPHAARG2], rs[D3DRS_ALPHATESTENABLE], rs[D3DRS_ALPHAREF]);
                Log("[flat]   blend=%lu src=%lu dst=%lu cop=%lu ca1=%lu ca2=%lu\n",
                    rs[D3DRS_ALPHABLENDENABLE],
                    rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND],
                    tss[0][D3DTSS_COLOROP], tss[0][D3DTSS_COLORARG1], tss[0][D3DTSS_COLORARG2]);
            }
        }
        ID3D11ShaderResourceView* srvs[4] = { tex[0] ? tex[0]->srv : nullptr, tex[1] ? tex[1]->srv : nullptr,
                                              tex[2] ? tex[2]->srv : nullptr, tex[3] ? tex[3]->srv : nullptr };
        g.ctx->PSSetShaderResources(0, 4, srvs);
        ID3D11SamplerState* samps[4];
        for (int s = 0; s < 4; ++s)
            samps[s] = GetSampler(ss[s][D3DSAMP_ADDRESSU], ss[s][D3DSAMP_ADDRESSV], ss[s][D3DSAMP_ADDRESSW],
                                  ss[s][D3DSAMP_MINFILTER], ss[s][D3DSAMP_MAGFILTER], ss[s][D3DSAMP_MIPFILTER]);
        g.ctx->PSSetSamplers(0, 4, samps);
    }
    static D3D11_PRIMITIVE_TOPOLOGY Topo(D3DPRIMITIVETYPE p) {
        switch (p) {
            case D3DPT_TRIANGLELIST: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case D3DPT_TRIANGLESTRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case D3DPT_LINELIST: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case D3DPT_LINESTRIP: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case D3DPT_POINTLIST: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }
    static UINT PrimCount(D3DPRIMITIVETYPE p, UINT c) {
        switch (p) {
            case D3DPT_TRIANGLELIST: return c * 3;
            case D3DPT_TRIANGLESTRIP: return c + 2;
            case D3DPT_LINELIST: return c * 2;
            case D3DPT_LINESTRIP: return c + 1;
            case D3DPT_POINTLIST: return c;
            default: return c * 3;
        }
    }
    // FVF path (fixed-function/UI): any FVF with a position (XYZ or XYZRHW).
    bool CanDrawFVF() { return curVS == nullptr && (fvf & D3DFVF_POSITION_MASK) != 0; }

    // --- present / clear ---
    STDMETHOD(TestCooperativeLevel)() { return D3D_OK; }
    STDMETHOD(BeginScene)() { return D3D_OK; }
    STDMETHOD(EndScene)() { return D3D_OK; }
    STDMETHOD(Clear)(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR c, float z, DWORD stencil) {
        InterlockedIncrement(&dbg_Clear);
        ID3D11RenderTargetView* rt = g.curRTV ? g.curRTV : g.rtv;
        // What the GAME actually asks for, per distinct target size. Everything measured so
        // far was read off our own D3D11 call in the capture, which cannot tell an argument
        // we decoded wrong from one the game really passed.
        if (rt && rt != MainSceneRTV()) {
            static LONG n = 0;
            if (InterlockedIncrement(&n) <= 10)
                Log("[clr] target OFFSCREEN vp=%ux%u flags=0x%X color=0x%08X (a=%u r=%u g=%u b=%u) rects=%u\n",
                    curVP.Width, curVP.Height, flags, c,
                    (c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, count);
        }
        if (g.ctx && rt && (flags & D3DCLEAR_TARGET)) {
            float col[4] = { ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, ((c >> 24) & 0xFF) / 255.f };
            // D3D9 clears ONLY the rectangles the game passes; the whole surface is cleared
            // just when the list is empty. We ignored the list and always wiped everything,
            // which is what painted the white square on the weapon charge effects: the game
            // renders that effect into a 256x256 target and clears a small rect of it to
            // white, and we whitened all 256x256. The result is then blended additively over
            // the screen (SrcAlpha/One), so white + scene saturates to a white block.
            // Verified in a RenderDoc capture: the effect's own geometry only covers
            // NDC x -0.07..0.05, y -0.73..-0.63 of that target -- a small patch -- while the
            // rest was the clear colour.
            if (count && rects && Ctx1()) {
                std::vector<D3D11_RECT> r; r.reserve(count);
                for (DWORD i = 0; i < count; ++i)
                    r.push_back(D3D11_RECT{ (LONG)rects[i].x1, (LONG)rects[i].y1, (LONG)rects[i].x2, (LONG)rects[i].y2 });
                Ctx1()->ClearView(rt, col, r.data(), (UINT)r.size());
                static LONG once = 0;
                if (InterlockedIncrement(&once) == 1)
                    Log("[ne] Clear con %u rect(s): (%d,%d)-(%d,%d) color=%08X\n", count,
                        rects[0].x1, rects[0].y1, rects[0].x2, rects[0].y2, c);
            } else {
                g.ctx->ClearRenderTargetView(rt, col);
            }
        }
        // the CURRENT depth, not always the screen one: with a render target pushed, the
        // engine expects its own depth to be the one cleared.
        ID3D11DepthStencilView* ds = g.curDSV ? g.curDSV : g.dsv;
        if (g.ctx && ds && (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))) {
            UINT df = ((flags & D3DCLEAR_ZBUFFER) ? D3D11_CLEAR_DEPTH : 0) | ((flags & D3DCLEAR_STENCIL) ? D3D11_CLEAR_STENCIL : 0);
            g.ctx->ClearDepthStencilView(ds, df, z, (UINT8)stencil);
        }
        return D3D_OK;
    }
    STDMETHOD(Present)(const RECT*, const RECT*, HWND, const RGNDATA*) {
        CtxLock lk;
        {
            static bool initDone = false;
            if (!initDone) { LoadConfig(); g_outlineEnabled = g_cfg.outlineDefault; initDone = true; }
            static bool prevF3 = false;
            bool nowF3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
            if (nowF3 && !prevF3) { g_outlineEnabled = !g_outlineEnabled; Log("[outline] %s\n", g_outlineEnabled ? "ON" : "OFF"); }
            prevF3 = nowF3;
        }
        // The game's frame lives in the multisampled target (g.msaaRTV) all through the
        // frame; resolve it into the real, single-sample backbuffer (g.bbTex, behind
        // g.rtv) here, once, before anything reads g.rtv as "the finished frame" --
        // CompositeOutline() does exactly that.
        if (g.ctx && g.msaaSamples > 1 && g.msaaTex && g.bbTex)
            g.ctx->ResolveSubresource(g.bbTex, 0, g.msaaTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        g_aoProj33 = mProj._33; g_aoProj43 = mProj._43;
        DrawSSAO();
        CompositeOutline();
        static LONG f = 0; LONG ff = InterlockedIncrement(&f);
        { LONGLONG now = QPC(); if (t_lastPresent) t_frameTotal += now - t_lastPresent; t_lastPresent = now; }
        if ((ff % 120) == 1) {
            Log("[ne] frame=%ld progOK=%ld DIP=%ld DP=%ld UP=%ld IUP=%ld fvfDrew=%ld fvfSkip=%ld lastFvf=0x%X SetRT=%ld sceneCopy=%ld texUp=%ld\n",
                ff, dbg_progOK, dbg_DIP, dbg_DP, dbg_UP, dbg_IUP, dbg_fvfDrew, dbg_fvfSkip, dbg_lastFvf, dbg_SetRT, dbg_sceneCopy, dbg_texUnlock);
            // 1:1 gap: draws the game issued that we never executed, and why.
            LONG lost = dbg_skipVS + dbg_skipFVF + dbg_skipBuf + dbg_skipProg + dbg_fvfSkip;
            if (lost) Log("[lost] draws no ejecutados=%ld (vs=%ld fvf=%ld buf=%ld prog=%ld up=%ld)\n",
                lost, dbg_skipVS, dbg_skipFVF, dbg_skipBuf, dbg_skipProg, dbg_fvfSkip);
        }
        // frame ms and how much of it goes inside the backend. Averaged over 120 frames:
        // the accumulator is reset WITH the log, not on every frame (that is why the us
        // came out divided by 120, and the log file was opened on every Present).
        if ((ff % 120) == 1) {
            LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
            // Log() uses wvsprintfA and does NOT support %f: everything in integers (microseconds).
            LONGLONG usFrame = t_frameTotal * 1000000 / fq.QuadPart / 120;
            LONGLONG usBack  = t_inBackend  * 1000000 / fq.QuadPart / 120;
            static LONG lastGuard = 0; LONG gc = GuardCalls();
            if (usFrame > 0) Log("[perf] frame=%uus (%u fps) backend=%uus (%u%%) guards=%u/frame poolSkip=%d shadow=%dMB rtPop=%d\n",
                (unsigned)usFrame, (unsigned)(1000000 / usFrame), (unsigned)usBack,
                (unsigned)(100 * usBack / usFrame), (unsigned)((gc - lastGuard) / 120),
                (int)PoolDestroySkipped(), (int)(g_shadowKB / 1024), (int)DequeRescued());
            lastGuard = gc;
            t_frameTotal = 0; t_inBackend = 0;
        }
        // [map] -- why the weapon charge effects render on some maps and not others.
        //
        // The refraction effect these weapons use only paints the SCENE texture
        // (tex2Dproj(SceneMapSampler,...)), and that texture is refreshed by
        // CRenderer_D3D::UpdateScreenTexture -- which the engine only calls inside the
        // glow/haze/fullscene passes. Those passes are skipped WHOLESALE when the map has
        // no objects in their lists (CRenderScene_RenderGlowPass @0x01CE2E70 bails on an
        // empty renderer+0x194/0x198), so on a map with no glow geometry nobody refreshes
        // it and our sampler falls back to white -> the white square.
        //
        // The three weights below are per-map: CBgInfo_ParseRendererSection reads
        // FullSceneGlow{,Org,Peri}ColorRev from the map's bginfo and
        // CMapRenderSettings_Apply publishes them here when the map loads. They are the
        // cheapest per-map fingerprint we can read without hooking anything.
        // dbg_sceneCopy counts our StretchRect into the 512x512 target, i.e. how many
        // times the scene texture was actually refreshed.
        if ((ff % 120) == 2) {
            static int lastKey = -1; static LONG lastCopy = 0;
            int mil[3]; ReadGlowWeights(mil);
            LONG copies = dbg_sceneCopy - lastCopy; lastCopy = dbg_sceneCopy;
            int key = mil[0] * 31 + mil[1] * 7 + mil[2];
            // log on every map change, and every 120 frames while the scene texture is dead
            if (key != lastKey || copies == 0) {
                Log("[map] glow ColorRev=%d/1000 Org=%d/1000 Peri=%d/1000 | sceneTexUpdates=%d/120frames%s\n",
                    mil[0], mil[1], mil[2], (int)copies,
                    copies == 0 ? "  <-- SCENE TEXTURE NEVER REFRESHED: effects that sample it fall back to white" : "");
                lastKey = key;
            }
        }
        // Dumping the backbuffer to BMP creates a fullscreen staging texture and maps it
        // for reading: that syncs with the GPU and writes 8MB to disk every second.
        // It is debug only, gated behind NE_DUMP_BB=1.
        static int dumpBB = -1;
        if (dumpBB < 0) { char b[8] = ""; dumpBB = (GetEnvironmentVariableA("NE_DUMP_BB", b, sizeof(b)) && b[0] == '1') ? 1 : 0; }
        if (dumpBB && (ff % 60) == 30) DumpBB();
        if (g.iq) {
            UINT64 nm = g.iq->GetNumStoredMessages(); static LONG logged = 0;
            for (UINT64 i = 0; i < nm && logged < 80; ++i) {
                SIZE_T len = 0; g.iq->GetMessageW(i, nullptr, &len);
                if (len && len < 4000) { char buf[4000]; D3D11_MESSAGE* m = (D3D11_MESSAGE*)buf;
                    if (SUCCEEDED(g.iq->GetMessageW(i, m, &len)) && m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING && m->pDescription) {
                        char d[600]; UINT dl = (UINT)m->DescriptionByteLength; if (dl > 599) dl = 599; memcpy(d, m->pDescription, dl); d[dl] = 0;
                        InterlockedIncrement(&logged); Log("[dbg] sev%d %s\n", m->Severity, d);
                    } }
            }
            g.iq->ClearStoredMessages();
        }
        // The client picks the interval in D3DPRESENT_PARAMETERS.PresentationInterval.
        // It was pinned to 1 (hard vsync): with the fps cap unlocked it still stayed at 60.
        if (g.sc) {
            UINT presentFlags = (syncInterval == 0 && g.tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
            g.sc->Present(syncInterval, presentFlags);
        }
        return D3D_OK;
    }
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pp) {
        SetSync(pp);
        if (!pp || !g.sc || !g.dev) return D3D_OK;
        UINT nw = pp->BackBufferWidth ? pp->BackBufferWidth : g.bbW;
        UINT nh = pp->BackBufferHeight ? pp->BackBufferHeight : g.bbH;
        if (nw == g.bbW && nh == g.bbH) return D3D_OK;
        g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (g.rtv) { g.rtv->Release(); g.rtv = nullptr; }
        if (g.bbTex) { g.bbTex->Release(); g.bbTex = nullptr; }
        if (g.msaaRTV) { g.msaaRTV->Release(); g.msaaRTV = nullptr; }
        if (g.msaaTex) { g.msaaTex->Release(); g.msaaTex = nullptr; }
        if (g.dsv) { g.dsv->Release(); g.dsv = nullptr; }
        if (g.depthSRV) { g.depthSRV->Release(); g.depthSRV = nullptr; }
        if (g.depthTex) { g.depthTex->Release(); g.depthTex = nullptr; }
        g.sc->ResizeBuffers(1, nw, nh, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        g.bbW = nw; g.bbH = nh;
        if (SUCCEEDED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&g.bbTex)) && g.bbTex) g.dev->CreateRenderTargetView(g.bbTex, nullptr, &g.rtv);
        CreateMSAATargets(nw, nh);
        CreateSceneDepth(nw, nh);
        g.curRTV = g.msaaSamples > 1 ? g.msaaRTV : g.rtv; g.curDSV = g.dsv;
        CreateOutlineTarget(nw, nh);
        curVP.X = 0; curVP.Y = 0; curVP.Width = nw; curVP.Height = nh; curVP.MinZ = 0.f; curVP.MaxZ = 1.f;
        Log("[ne] Reset -> %ux%u\n", nw, nh);
        return D3D_OK;
    }
    STDMETHOD(GetDirect3D)(IDirect3D9** pp) { *pp = parent; if (parent) parent->AddRef(); return D3D_OK; }

    // --- resource creation (real D3D11) ---
    STDMETHOD(CreateTexture)(UINT w, UINT h, UINT lv, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** pp, HANDLE*) {
        NTexture* t = new NTexture(w, h, lv, u, f, p);
        // If the D3D11 texture itself failed we hand back an object the game will
        // happily bind and that draws nothing. Say so: a missing effect texture looks
        // exactly like a quad painted flat white.
        if (!t->tex) {
            static LONG n = 0; if (InterlockedIncrement(&n) <= 20)
                Log("[ne] CreateTexture FAILED %ux%u lv=%u usage=0x%X fmt=0x%08X\n", w, h, lv, u, (DWORD)f);
        }
        *pp = t; return D3D_OK;
    }
    STDMETHOD(CreateVertexBuffer)(UINT len, DWORD u, DWORD f, D3DPOOL p, IDirect3DVertexBuffer9** pp, HANDLE*) { *pp = new NVBuf(len, u, f, p); return D3D_OK; }
    STDMETHOD(CreateIndexBuffer)(UINT len, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DIndexBuffer9** pp, HANDLE*) { *pp = new NIBuf(len, u, f, p); return D3D_OK; }
    STDMETHOD(CreateRenderTarget)(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9** pp, HANDLE*) {
        NSurface* s = new NSurface(w, h, f);
        D3D11_TEXTURE2D_DESC d{}; d.Width = w ? w : 1; d.Height = h ? h : 1; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g.dev->CreateTexture2D(&d, nullptr, &s->rtTex)) && s->rtTex) { g.dev->CreateRenderTargetView(s->rtTex, nullptr, &s->neRT.rtv); s->neRT.tex = s->rtTex; }
        *pp = s; return D3D_OK;
    }
    STDMETHOD(CreateDepthStencilSurface)(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9** pp, HANDLE*) {
        NSurface* s = new NSurface(w, h, f);
        D3D11_TEXTURE2D_DESC d{}; d.Width = w ? w : 1; d.Height = h ? h : 1; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (SUCCEEDED(g.dev->CreateTexture2D(&d, nullptr, &s->rtTex)) && s->rtTex) g.dev->CreateDepthStencilView(s->rtTex, nullptr, &s->neRT.dsv);
        *pp = s; return D3D_OK;
    }
    STDMETHOD(CreateOffscreenPlainSurface)(UINT w, UINT h, D3DFORMAT f, D3DPOOL, IDirect3DSurface9** pp, HANDLE*) { *pp = new NSurface(w, h, f); return D3D_OK; }
    STDMETHOD(CreateVertexDeclaration)(const D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** pp) { *pp = new NVDecl(e); return D3D_OK; }
    STDMETHOD(CreateVertexShader)(const DWORD*, IDirect3DVertexShader9** pp) { *pp = new NVShader(); return D3D_OK; }
    STDMETHOD(CreatePixelShader)(const DWORD*, IDirect3DPixelShader9** pp) { *pp = new NPShader(); return D3D_OK; }
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9** pp) { *pp = new NStateBlock(); return D3D_OK; }
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** pp) { *pp = new NStateBlock(); return D3D_OK; }
    STDMETHOD(CreateQuery)(D3DQUERYTYPE t, IDirect3DQuery9** pp) { if (pp) *pp = new NQuery(t); return D3D_OK; }
    STDMETHOD(CreateCubeTexture)(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9** pp, HANDLE*) { Log("[ne] CreateCubeTexture (returning null!)\n"); *pp = nullptr; return D3DERR_NOTAVAILABLE; }
    STDMETHOD(CreateVolumeTexture)(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9** pp, HANDLE*) { Log("[ne] CreateVolumeTexture (returning null!)\n"); *pp = nullptr; return D3DERR_NOTAVAILABLE; }
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9** pp) { Log("[ne] CreateAdditionalSwapChain (returning null!)\n"); *pp = nullptr; return D3DERR_NOTAVAILABLE; }
    // D3D9 returns THE SAME surface with an AddRef (not a new one on every call)
    NSurface* sBB = nullptr; NSurface* sDS = nullptr; NSurface* sRT = nullptr;
    // The engine keeps a STACK of render targets: PushRenderTarget does
    // GetRenderTarget(0) -> push -> SetRenderTarget(new), and Pop restores what
    // was pushed (CRenderTargetManager_D3D::PushRenderTarget @0x01D902E0).
    // That is why GetRenderTarget has to return the CURRENT target: if it always
    // returned the backbuffer, a nested push would stack the backbuffer instead of
    // the outer target and the Pop would send to screen what belonged in the texture.
    // They have to be COUNTED references: keeping the bare pointer left a dangling one
    // when the game released the surface, and the next AddRef landed on freed memory
    // (the vtable read came back as 2 -> crash in the engine's Release).
    IDirect3DSurface9* curRTSurf = nullptr; IDirect3DSurface9* curDSSurf = nullptr;
    static void Retain(IDirect3DSurface9*& slot, IDirect3DSurface9* s) {
        if (slot == s) return;
        if (s) s->AddRef();
        if (slot) slot->Release();
        slot = s;
    }
    STDMETHOD(GetBackBuffer)(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** pp) { if (!sBB) sBB = new NSurface(g.bbW, g.bbH, D3DFMT_A8R8G8B8); sBB->AddRef(); *pp = sBB; return D3D_OK; }
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** pp) {
        if (curDSSurf) { curDSSurf->AddRef(); *pp = curDSSurf; return D3D_OK; }
        if (!sDS) sDS = new NSurface(g.bbW, g.bbH, D3DFMT_D24S8);
        sDS->AddRef(); *pp = sDS; return D3D_OK;
    }
    STDMETHOD(GetRenderTarget)(DWORD, IDirect3DSurface9** pp) {
        if (curRTSurf) { curRTSurf->AddRef(); *pp = curRTSurf; return D3D_OK; }
        if (!sRT) { sRT = new NSurface(g.bbW, g.bbH, D3DFMT_A8R8G8B8); ((NSurface*)sRT)->neRT.rtv = MainSceneRTV(); } // it is the backbuffer
        sRT->AddRef(); *pp = sRT; return D3D_OK;
    }

    // --- caps / info ---
    STDMETHOD_(UINT, GetAvailableTextureMem)() { return 256u * 1024 * 1024; }
    STDMETHOD(EvictManagedResources)() { return D3D_OK; }
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* c) { if (c) { ZeroMemory(c, sizeof(*c)); c->DeviceType = D3DDEVTYPE_HAL; c->VertexShaderVersion = D3DVS_VERSION(3, 0); c->PixelShaderVersion = D3DPS_VERSION(3, 0); c->MaxTextureWidth = c->MaxTextureHeight = 8192; c->MaxSimultaneousTextures = 8; c->MaxStreams = 16; c->NumSimultaneousRTs = 4; c->MaxVertexShaderConst = 256; c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_NONPOW2CONDITIONAL; c->TextureFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MIPFPOINT; c->MaxTextureAspectRatio = 8192; c->MaxAnisotropy = 16; } return D3D_OK; }
    STDMETHOD(GetDisplayMode)(UINT, D3DDISPLAYMODE* m) { if (m) { m->Width = g.bbW; m->Height = g.bbH; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; } return D3D_OK; }
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS* p) { if (p) { ZeroMemory(p, sizeof(*p)); p->DeviceType = D3DDEVTYPE_HAL; p->hFocusWindow = hwnd; } return D3D_OK; }
    STDMETHOD(SetCursorProperties)(UINT, UINT, IDirect3DSurface9*) { return D3D_OK; }
    STDMETHOD_(void, SetCursorPosition)(int, int, DWORD) {}
    STDMETHOD_(BOOL, ShowCursor)(BOOL) { return TRUE; }
    STDMETHOD(GetSwapChain)(UINT, IDirect3DSwapChain9** pp) { Log("[ne] GetSwapChain (returning null!)\n"); *pp = nullptr; return D3DERR_NOTAVAILABLE; }
    STDMETHOD_(UINT, GetNumberOfSwapChains)() { return 1; }
    STDMETHOD(GetRasterStatus)(UINT, D3DRASTER_STATUS* s) { if (s) { s->InVBlank = FALSE; s->ScanLine = 0; } return D3D_OK; }
    STDMETHOD(SetDialogBoxMode)(BOOL) { return D3D_OK; }
    STDMETHOD_(void, SetGammaRamp)(UINT, DWORD, const D3DGAMMARAMP*) {}
    STDMETHOD_(void, GetGammaRamp)(UINT, D3DGAMMARAMP*) {}
    STDMETHOD(UpdateSurface)(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*) { return D3D_OK; }
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* srcB, IDirect3DBaseTexture9* dstB) {
        InterlockedIncrement(&dbg_updateTex);
        NTexture* s = (NTexture*)srcB, * d = (NTexture*)dstB;
        if (s && d && s->tex && d->tex) {
            UINT n = s->levels < d->levels ? s->levels : d->levels;
            for (UINT l = 0; l < n; ++l) {
                if (l < NTexture::MAXLV && s->LevelSize(l) == d->LevelSize(l) && s->shadow[l] && d->shadow[l]) {
                    memcpy(d->shadow[l], s->shadow[l], d->LevelSize(l));
                    g.ctx->UpdateSubresource(d->tex, l, nullptr, d->shadow[l], d->RowPitch(l), d->LevelSize(l));
                }
            }
        }
        return D3D_OK;
    }
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9*, IDirect3DSurface9*) { return D3D_OK; }
    STDMETHOD(GetFrontBufferData)(UINT, IDirect3DSurface9*) { return D3D_OK; }
    // The game copies the backbuffer into the blur/glow texture with StretchRect.
    // Without this the texture stays BLACK and the blur averages black -> dark streaks while running.
    STDMETHOD(StretchRect)(IDirect3DSurface9* src, const RECT* srcR, IDirect3DSurface9* dst, const RECT*, D3DTEXTUREFILTERTYPE) {
        CtxLock lk;
        NE_RTView* s = NE_QueryRT(src); NE_RTView* d = NE_QueryRT(dst);
        // The SCENE texture (512x512, filled by UpdateScreenTexture). Several .fx files
        // sample it as g_TexSceneMap with tex2Dproj; if it does not resolve they fall back
        // to white and the whole quad comes out opaque white (the railgun / dagger /
        // wall jump square).
        if (d && d->w == 512 && d->h == 512 && d->tex) {
            InterlockedIncrement(&dbg_sceneCopy);
            if (g_sceneTex != d->tex) {
                if (g_sceneSRV) { g_sceneSRV->Release(); g_sceneSRV = nullptr; }
                if (SUCCEEDED(g.dev->CreateShaderResourceView(d->tex, nullptr, &g_sceneSRV))) g_sceneTex = d->tex;
            }
        }


        if (!d || !d->tex) return D3D_OK;
        // source: the given surface, or the backbuffer if it has no texture of its own
        ID3D11Texture2D* srcTex = (s && s->tex) ? s->tex : nullptr;
        // The real scene lives in g.msaaTex all frame; g.sc's own buffer (below) only gets
        // that content at Present()'s resolve. Reading it here mid-frame under MSAA means
        // "the backbuffer" is really last frame's fully-finished image -- the weapon-trail
        // and scene-reflection effects that StretchRect this into g_TexSceneMap then show
        // a one-frame-stale, washed-out/white ghost while the character is moving fast.
        // Resolving here first keeps it current with this frame's progress instead.
        if (!srcTex && g.ctx && g.msaaSamples > 1 && g.msaaTex && g.bbTex)
            g.ctx->ResolveSubresource(g.bbTex, 0, g.msaaTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        ID3D11Texture2D* bb = nullptr;
        if (!srcTex && g.sc && SUCCEEDED(g.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) srcTex = bb;
        if (srcTex) {
            D3D11_TEXTURE2D_DESC sd{}, dd{}; srcTex->GetDesc(&sd); d->tex->GetDesc(&dd);
            bool sameSize = (sd.Width == dd.Width && sd.Height == dd.Height);
            if (sd.Format == dd.Format && sameSize && !srcR) {
                g.ctx->CopyResource(d->tex, srcTex); // mismo tamano: copia directa
            } else if (d->rtv && BuildBlit(g.dev)) {
                // different size -> we have to SCALE (StretchRect scales; a copy does not).
                // Without this the blur only saw a stretched crop of the backbuffer and
                // "reflections" of geometry from somewhere else showed up.
                ID3D11ShaderResourceView* srcSRV = nullptr;
                if (SUCCEEDED(g.dev->CreateShaderResourceView(srcTex, nullptr, &srcSRV)) && srcSRV) {
                    ID3D11RenderTargetView* oldRT = nullptr; ID3D11DepthStencilView* oldDS = nullptr;
                    g.ctx->OMGetRenderTargets(1, &oldRT, &oldDS);
                    g.ctx->OMSetRenderTargets(1, &d->rtv, nullptr);
                    D3D11_VIEWPORT vp{}; vp.Width = (float)dd.Width; vp.Height = (float)dd.Height; vp.MaxDepth = 1.f;
                    g.ctx->RSSetViewports(1, &vp);
                    g.ctx->IASetInputLayout(nullptr);
                    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    g.ctx->VSSetShader(g_blitVS, nullptr, 0);
                    g.ctx->PSSetShader(g_blitPS, nullptr, 0);
                    g.ctx->PSSetShaderResources(0, 1, &srcSRV);
                    g.ctx->PSSetSamplers(0, 1, &g_blitSamp);
                    g.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                    g.ctx->OMSetDepthStencilState(g.dsOff, 0);
                    g.ctx->Draw(3, 0);
                    ID3D11ShaderResourceView* nul = nullptr; g.ctx->PSSetShaderResources(0, 1, &nul);
                    g.ctx->OMSetRenderTargets(1, oldRT ? &oldRT : &g.curRTV, oldRT ? oldDS : g.curDSV);
                    if (oldRT) oldRT->Release(); if (oldDS) oldDS->Release();
                    ApplyViewport();
                    srcSRV->Release();
                }
            }
        }
        if (bb) bb->Release();
        return D3D_OK;
    }
    STDMETHOD(ColorFill)(IDirect3DSurface9*, const RECT*, D3DCOLOR) { return D3D_OK; }
    STDMETHOD(SetRenderTarget)(DWORD idx, IDirect3DSurface9* surf) {
        InterlockedIncrement(&dbg_SetRT);
        if (idx != 0) return D3D_OK; // only the main RT
        CtxLock lk;
        // Do NOT take a reference: doing so unbalanced the refcount the engine keeps
        // and the render target stack ended up corrupted -> PopRenderTarget
        // (0x01D90450) read garbage from back() and crashed.
        curRTSurf = surf;
        NE_RTView* v = NE_QueryRT(surf);
        g.curRTV = (v && v->rtv) ? v->rtv : MainSceneRTV(); // no rtv of its own (or a cached backbuffer) -> backbuffer
        g.ctx->OMSetRenderTargets(1, &g.curRTV, EffDSV());
        // D3D9 resets the viewport to the target size on every SetRenderTarget.
        curVP.X = 0; curVP.Y = 0; curVP.MinZ = 0.f; curVP.MaxZ = 1.f;
        if (v && v->rtv && v->w) { curVP.Width = v->w; curVP.Height = v->h; }
        else { curVP.Width = g.bbW; curVP.Height = g.bbH; } // backbuffer -> full screen
        g_effectTargetNeedsAlphaClear = v && v->rtv && v->w == 256 && v->h == 256;
        ApplyViewport();
        return D3D_OK;
    }
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* surf) {
        CtxLock lk;
        curDSSurf = surf;
        NE_RTView* v = NE_QueryRT(surf);
        // NULL means "no depth buffer" in D3D9: depth testing and writing are off. It is
        // what the engine passes before drawing into an offscreen effect target. Falling
        // back to the screen depth here is what painted the white square on the weapon
        // charge effects: the effect renders into a 256x256 target that was just cleared
        // to opaque white, and with the 1680x1050 scene depth still bound its fragments
        // get depth-tested against the top-left corner of the scene and rejected, so the
        // target keeps the clear colour. Which is also why it only happened on some maps
        // -- whether those stale depths reject the effect depends on what the map draws
        // in that corner. Confirmed in a RenderDoc capture: EIDs 5380/5382/5393/5406 all
        // flagged "depth target is larger than render target", and the saved 256x256
        // target after the draw is white.
        g.curDSV = (v && v->dsv) ? v->dsv : (surf ? g.dsv : nullptr);
        g.ctx->OMSetRenderTargets(1, &g.curRTV, EffDSV());
        return D3D_OK;
    }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) {
        if (m) {
            if (s == D3DTS_WORLD) mWorld = *m;
            else if (s == D3DTS_VIEW) mView = *m;
            else if (s == D3DTS_PROJECTION) mProj = *m;
            else if (s >= D3DTS_TEXTURE0 && s <= D3DTS_TEXTURE7) {
                // Texture matrices were being discarded entirely.
                UINT i = (UINT)s - D3DTS_TEXTURE0;
                mTex[i] = *m; texMatSet[i] = true;
                bool ident = (m->_11 == 1 && m->_22 == 1 && m->_31 == 0 && m->_32 == 0 && m->_21 == 0 && m->_12 == 0);
                if (!ident) {
                    static LONG n = 0; if (InterlockedIncrement(&n) <= 12)
                        Log("[uvxform] SetTransform TEXTURE%u  [%d %d / %d %d / %d %d] (milesimos)\n", i,
                            (int)(m->_11 * 1000), (int)(m->_12 * 1000), (int)(m->_21 * 1000),
                            (int)(m->_22 * 1000), (int)(m->_31 * 1000), (int)(m->_32 * 1000));
                }
            }
        }
        return D3D_OK;
    }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE, D3DMATRIX*) { return D3D_OK; }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) { return D3D_OK; }
    STDMETHOD(SetViewport)(const D3DVIEWPORT9* v) {
        if (v) {
            curVP = *v;
        }
        return D3D_OK;
    }
    STDMETHOD(GetViewport)(D3DVIEWPORT9* v) { if (v) *v = curVP; return D3D_OK; }
    void ApplyViewport() { D3D11_VIEWPORT vp{}; vp.TopLeftX = (float)curVP.X; vp.TopLeftY = (float)curVP.Y; vp.Width = (float)curVP.Width; vp.Height = (float)curVP.Height; vp.MinDepth = curVP.MinZ; vp.MaxDepth = curVP.MaxZ; g.ctx->RSSetViewports(1, &vp); }
    STDMETHOD(SetMaterial)(const D3DMATERIAL9*) { return D3D_OK; }
    STDMETHOD(GetMaterial)(D3DMATERIAL9* m) { if (m) ZeroMemory(m, sizeof(*m)); return D3D_OK; }
    STDMETHOD(SetLight)(DWORD, const D3DLIGHT9*) { return D3D_OK; }
    STDMETHOD(GetLight)(DWORD, D3DLIGHT9* l) { if (l) ZeroMemory(l, sizeof(*l)); return D3D_OK; }
    STDMETHOD(LightEnable)(DWORD, BOOL) { return D3D_OK; }
    STDMETHOD(GetLightEnable)(DWORD, BOOL* e) { if (e) *e = FALSE; return D3D_OK; }
    STDMETHOD(SetClipPlane)(DWORD i, const float* pl) {
        static LONG n = 0;
        if (InterlockedIncrement(&n) <= 8 && pl) Log("[diag] SetClipPlane %u = %.3f %.3f %.3f %.3f\n", i, pl[0], pl[1], pl[2], pl[3]);
        return D3D_OK;
    }
    STDMETHOD(GetClipPlane)(DWORD, float*) { return D3D_OK; }
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE s, DWORD v) {
        if ((DWORD)s < 256) rs[s] = v;
        // FOGENABLE toggles per object (noisy). The ranges and the color are set by the MAP
        // (FOG_MINDIST/FOG_MAXDIST through CFogPropertyCommand), so those matter every time
        // they CHANGE: the startup ones are not the in-match ones.
        // Log() uses wvsprintfA and does not support %f, which is why the value goes in thousandths.
        if (s == D3DRS_FOGENABLE || s == D3DRS_FOGCOLOR || s == D3DRS_FOGTABLEMODE || s == D3DRS_FOGVERTEXMODE ||
            s == D3DRS_FOGSTART || s == D3DRS_FOGEND || s == D3DRS_FOGDENSITY) {
            static DWORD last[256] = {}; static bool seen[256] = {};
            if (!seen[(DWORD)s] || last[(DWORD)s] != v) {
                seen[(DWORD)s] = true; last[(DWORD)s] = v;
                float f; memcpy(&f, &v, 4);
                Log("[fog] rs=%d val=0x%08lX milesimos=%d\n", (int)s, v, (int)(f * 1000.0f));
                void** stack = (void**)_AddressOfReturnAddress();
                Log("[fog-trace] ret=%p this=%p arg_state=%p arg_value=%p\n",
                    _ReturnAddress(), stack[1], stack[2], stack[3]);
            }
        }
        return D3D_OK;
    }
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE s, DWORD* v) { if (v) *v = (DWORD)s < 256 ? rs[s] : 0; return D3D_OK; }
    STDMETHOD(BeginStateBlock)() { return D3D_OK; }
    STDMETHOD(SetClipStatus)(const D3DCLIPSTATUS9*) { return D3D_OK; }
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* s) { if (s) ZeroMemory(s, sizeof(*s)); return D3D_OK; }
    STDMETHOD(GetTexture)(DWORD, IDirect3DBaseTexture9** pp) { *pp = nullptr; return D3D_OK; }
    STDMETHOD(SetTexture)(DWORD s, IDirect3DBaseTexture9* t) {
        if (s < 8) tex[s] = (NTexture*)t;
        // Remember the light ramp. When g_TexShadeMap does not resolve we used to take
        // whatever happened to be bound to stage 1 at that instant, and stage 1 is mutable
        // global state: any effect that binds a sprite there becomes the light ramp for the
        // next world draw, so the map's lighting jumps for a few frames. That is the flicker.
        // The real ramp is recognisable -- the clean client binds a 256x1 strip here
        // (confirmed with the spy DLL) -- so we latch onto that and ignore everything else.
        // We hold a reference: the texture can be destroyed on a map change and a stale SRV
        // would be a dangling pointer.
        if (s == 1) {
            NTexture* n = (NTexture*)t;
            if (n && n->srv && n->w == 256 && (n->h <= 2 || NE_LooksLikeSquareRamp(n)) && n->srv != g_rampSRV) {
                if (g_rampSRV) g_rampSRV->Release();
                g_rampSRV = n->srv; g_rampSRV->AddRef();
            }
        }
        return D3D_OK;
    }
    STDMETHOD(GetTextureStageState)(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD* v) { if (v) *v = (s < 8 && t < 8) ? tss[s][t] : 0; return D3D_OK; }
    // The engine uses the fixed-function stage pipeline for everything that is not .fx
    // (CTextureState_D3D::Apply @0x01D8E0C0 sets COLOROP/ARG1/ARG2 and ALPHAOP/ARG1/ARG2).
    // Ignoring them made everything come out as a fixed MODULATE(texture,diffuse): the
    // sprites with SELECTARG1 came out gray and the second-stage lightmaps were not applied.
    STDMETHOD(SetTextureStageState)(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) {
        if (s < 8 && t < 32) tss[s][t] = v;
        // Does the game actually use the texture coordinate transform? If it does, we
        // have been ignoring it, and an animated sprite sheet would sample the wrong
        // region of the atlas.
        if ((t == D3DTSS_TEXCOORDINDEX && v != s) || (t == D3DTSS_TEXTURETRANSFORMFLAGS && v != 0)) {
            static LONG n = 0; if (InterlockedIncrement(&n) <= 12)
                Log("[uvxform] SetTextureStageState stage=%lu %s=0x%lX\n", s,
                    t == D3DTSS_TEXCOORDINDEX ? "TEXCOORDINDEX" : "TEXTURETRANSFORMFLAGS", v);
        }
        return D3D_OK;
    }
    STDMETHOD(GetSamplerState)(DWORD, D3DSAMPLERSTATETYPE, DWORD* v) { if (v) *v = 0; return D3D_OK; }
    // The engine asks for CLAMP on several samplers (CTextureState_D3D @0x01D8EB70,
    // table 0x02570014); forcing WRAP everywhere made the textures that depend on the
    // edge not repeating come out striped.
    STDMETHOD(SetSamplerState)(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) { if (s < 8 && t < 14) ss[s][t] = v; return D3D_OK; }
    STDMETHOD(ValidateDevice)(DWORD* n) { if (n) *n = 1; return D3D_OK; }
    STDMETHOD(SetPaletteEntries)(UINT, const PALETTEENTRY*) { return D3D_OK; }
    STDMETHOD(GetPaletteEntries)(UINT, PALETTEENTRY*) { return D3D_OK; }
    STDMETHOD(SetCurrentTexturePalette)(UINT) { return D3D_OK; }
    STDMETHOD(GetCurrentTexturePalette)(UINT* n) { if (n) *n = 0; return D3D_OK; }
    STDMETHOD(SetScissorRect)(const RECT*) { return D3D_OK; }
    STDMETHOD(GetScissorRect)(RECT* r) { if (r) ZeroMemory(r, sizeof(*r)); return D3D_OK; }
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL) { return D3D_OK; }
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)() { return FALSE; }
    STDMETHOD(SetNPatchMode)(float) { return D3D_OK; }
    STDMETHOD_(float, GetNPatchMode)() { return 0.f; }

    // --- draws ---
    // programmable path: the effect already bound VS/PS/cbuffers/SRVs/states; here we
    // set RT/viewport/input layout (from the vertex decl)/VB/IB and draw.
    // The scene depth (g.dsv) is backbuffer-sized. Binding it to a smaller offscreen
    // render target makes D3D11 reject every draw ("depth target larger than render
    // target"), so the weapon-charge glow renders into a 256x256 buffer that keeps its
    // white clear and the additive composite paints a white square -- worst from below,
    // where the effect sits against the screen edge. Only the backbuffer matches g.dsv;
    // on any other render target, run depthless. This is the draw-time catch-all that
    // covers the cases SetDepthStencilSurface(NULL) alone did not.
    ID3D11DepthStencilView* EffDSV() { return (g.curDSV == g.dsv && g.curRTV != MainSceneRTV()) ? nullptr : g.curDSV; }
    bool BeginProgDraw() {
        if (!g_prog.active) return false;
        CtxLock lk;
        InterlockedIncrement(&dbg_progActive);
        if (!stream0 || !stream0->buf) { InterlockedIncrement(&dbg_noStream); return false; }
        ID3D11InputLayout* il = curDecl ? BuildInputLayout(curDecl, g_prog.vsbc, g_prog.vslen)
                                        : BuildInputLayoutFVF(fvf, g_prog.vsbc, g_prog.vslen);
        if (!il) { InterlockedIncrement(&dbg_noIL); return false; }
        InterlockedIncrement(&dbg_progOK);
        // The .fx pass wins over the device state, for this pass's draws only. D3DX
        // applies the pass states to the device and restores them afterwards; doing it
        // as an override here has the same effect without ever touching rs[], so it
        // cannot leak into later draws or fight with what the game sets in between.
        auto RS = [&](DWORD state) -> DWORD {
            for (int i = 0; i < g_passStateCount; ++i) if (g_passState[i] == state) return g_passValue[i];
            return rs[state];
        };
        g.ctx->OMSetRenderTargets(1, &g.curRTV, EffDSV());
        ApplyViewport();
        if (g_effectTargetNeedsAlphaClear && g.curRTV != MainSceneRTV() && curVP.Width == 256 && curVP.Height == 256) {
            const float transparent[4] = { 1.f, 1.f, 1.f, 0.f };
            g.ctx->ClearRenderTargetView(g.curRTV, transparent);
            g_effectTargetNeedsAlphaClear = false;
        }
        { float bs, sl; memcpy(&bs, &rs[D3DRS_DEPTHBIAS], 4); memcpy(&sl, &rs[D3DRS_SLOPESCALEDEPTHBIAS], 4); g.ctx->RSSetState(GetRasterizer(bs, sl, RS(D3DRS_CULLMODE), rs[D3DRS_FILLMODE])); }
        ID3D11DepthStencilState* dss = !RS(D3DRS_ZENABLE) ? g.dsOff : (RS(D3DRS_ZWRITEENABLE) ? g.dsWrite : g.dsNoWrite);
        g.ctx->OMSetDepthStencilState(dss, 0);
        float bf[4] = { 0,0,0,0 };
        // "White" TEST: force OPAQUE. If it disappears, the white is an additive pass.
        static int opaqueTest = -1;
        if (opaqueTest < 0) { char b[8]; opaqueTest = GetEnvironmentVariableA("NE_OPAQUE", b, 8) ? 1 : 0; }
        if (opaqueTest) { g.ctx->OMSetBlendState(nullptr, bf, 0xffffffff); }
        // The .fx pass can declare BlendOp just like it declares AlphaBlendEnable, and the
        // parser already reads it -- it was GetBlend that threw it away. Same bug as the
        // wall jump / dagger square: a render state the pass asks for and we ignored.
        else if (RS(D3DRS_ALPHABLENDENABLE)) g.ctx->OMSetBlendState(GetBlend(RS(D3DRS_SRCBLEND), RS(D3DRS_DESTBLEND), RS(D3DRS_BLENDOP)), bf, 0xffffffff);
        else g.ctx->OMSetBlendState(nullptr, bf, 0xffffffff);
        g.ctx->IASetInputLayout(il);
        g.ctx->VSSetShader(g_prog.vs, nullptr, 0);
        g.ctx->PSSetShader(g_prog.ps, nullptr, 0);
        UINT stride = stream0Stride, off = stream0Off;
        g.ctx->IASetVertexBuffers(0, 1, &stream0->buf, &stride, &off);
        // NE_FogCB (b1) has to be refilled HERE, not in BindPass (effects.cpp): BindPass
        // runs once per BeginPass, but the game can draw several objects under the same
        // pass while toggling FOGENABLE between them (e.g. the sky dome sharing the
        // terrain's shader with fog off) -- a cbuffer filled once per pass would carry a
        // stale enable flag into the wrong draw. BeginProgDraw runs right before every
        // individual draw, so this always reflects the current object's real state.
        {
            LoadConfig();
            static ID3D11Buffer* fogCB = nullptr;
            if (g_cfg.fog && !fogCB) {
                D3D11_BUFFER_DESC d{}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_DYNAMIC;
                d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                g.dev->CreateBuffer(&d, nullptr, &fogCB);
            }
            if (g_cfg.fog && fogCB) {
                float data[8]; NE_FogState(&data[0], &data[4]);
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(g.ctx->Map(fogCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, data, sizeof(data)); g.ctx->Unmap(fogCB, 0);
                }
                g.ctx->PSSetConstantBuffers(1, 1, &fogCB);
            }
        }
        return true;
    }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE pt, UINT start, UINT primCount) {
        Stopwatch sw;
        InterlockedIncrement(&dbg_DP);
        if (g_prog.active) {
            if (BeginProgDraw()) {
                g.ctx->IASetPrimitiveTopology(Topo(pt));
                UINT vc = PrimCount(pt, primCount);
                g.ctx->Draw(vc, start);
                DrawOutlineMaskCopy(false, vc, start, 0);
            }
            else InterlockedIncrement(&dbg_skipProg);
            return D3D_OK;
        }
        if (!CanDrawFVF()) { InterlockedIncrement(curVS ? &dbg_skipVS : &dbg_skipFVF); return D3D_OK; }
        if (!stream0 || !stream0->buf) { InterlockedIncrement(&dbg_skipBuf); return D3D_OK; }
        ID3D11InputLayout* il = BuildInputLayoutFVF(fvf, g.vsDefaultBlob->GetBufferPointer(), g.vsDefaultBlob->GetBufferSize());
        if (!il) return D3D_OK;
        ApplyDefaultState();
        g.ctx->IASetInputLayout(il);
        UINT stride = stream0Stride ? stream0Stride : FvfStride(fvf), off = stream0Off;
        g.ctx->IASetVertexBuffers(0, 1, &stream0->buf, &stride, &off);
        g.ctx->IASetPrimitiveTopology(Topo(pt));
        g.ctx->Draw(PrimCount(pt, primCount), start);
        return D3D_OK;
    }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE pt, INT baseV, UINT, UINT, UINT startIdx, UINT primCount) {
        Stopwatch sw;
        InterlockedIncrement(&dbg_DIP);
        // basic_walljump.scn is 27 models of exactly 6 verts / 4 tris, all textured
        // with tick2_blue. Plasma sword and the melee trails are built the same way.
        // 4 triangles is a narrow enough signature to catch those draws and nothing
        // else, so we can finally see whether the texture reaches them.
        if (primCount == 4 && pt == D3DPT_TRIANGLELIST) {
            static LONG n = 0;
            if (InterlockedIncrement(&n) <= 25)
                Log("[quad4] prog=%d tex0=%p srv0=%p cop=%lu ca1=%lu aop=%lu blend=%lu src=%lu dst=%lu zw=%lu\n",
                    g_prog.active ? 1 : 0, tex[0], tex[0] ? tex[0]->srv : nullptr,
                    tss[0][D3DTSS_COLOROP], tss[0][D3DTSS_COLORARG1], tss[0][D3DTSS_ALPHAOP],
                    rs[D3DRS_ALPHABLENDENABLE], rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND],
                    rs[D3DRS_ZWRITEENABLE]);
        }
        if (g_prog.active) {
            if (!(indices && indices->buf)) InterlockedIncrement(&dbg_skipProg);
            if (BeginProgDraw() && indices && indices->buf) {
                DXGI_FORMAT ifmt = indices->fmt == D3DFMT_INDEX16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
                g.ctx->IASetIndexBuffer(indices->buf, ifmt, 0);
                g.ctx->IASetPrimitiveTopology(Topo(pt));
                UINT ic = PrimCount(pt, primCount);
                g.ctx->DrawIndexed(ic, startIdx, baseV);
                DrawOutlineMaskCopy(true, ic, startIdx, baseV);
            }
            return D3D_OK;
        }
        // 1:1 accounting: every draw the game asks for either happens or gets counted
        // here. A silent return is how draws disappear without leaving a trace.
        if (!CanDrawFVF()) { InterlockedIncrement(curVS ? &dbg_skipVS : &dbg_skipFVF); return D3D_OK; }
        if (!stream0 || !stream0->buf || !indices || !indices->buf) { InterlockedIncrement(&dbg_skipBuf); return D3D_OK; }
        ID3D11InputLayout* il = BuildInputLayoutFVF(fvf, g.vsDefaultBlob->GetBufferPointer(), g.vsDefaultBlob->GetBufferSize());
        if (!il) return D3D_OK;
        ApplyDefaultState();
        g.ctx->IASetInputLayout(il);
        UINT stride = stream0Stride ? stream0Stride : FvfStride(fvf), off = stream0Off;
        g.ctx->IASetVertexBuffers(0, 1, &stream0->buf, &stride, &off);
        DXGI_FORMAT ifmt = indices->fmt == D3DFMT_INDEX16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        g.ctx->IASetIndexBuffer(indices->buf, ifmt, 0);
        g.ctx->IASetPrimitiveTopology(Topo(pt));
        g.ctx->DrawIndexed(PrimCount(pt, primCount), startIdx, baseV);
        return D3D_OK;
    }
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE pt, UINT primCount, const void* data, UINT stride) {
        Stopwatch sw;
        InterlockedIncrement(&dbg_UP);
        if (curVS != nullptr || !(fvf & D3DFVF_POSITION_MASK) || !g.dev || !g.vsDefaultBlob) { InterlockedIncrement(&dbg_fvfSkip); dbg_lastFvf = fvf; return D3D_OK; }
        ID3D11InputLayout* il = BuildInputLayoutFVF(fvf, g.vsDefaultBlob->GetBufferPointer(), g.vsDefaultBlob->GetBufferSize());
        if (!il) { InterlockedIncrement(&dbg_fvfSkip); dbg_lastFvf = fvf; return D3D_OK; }
        InterlockedIncrement(&dbg_fvfDrew);
        UINT verts = PrimCount(pt, primCount); UINT bytes = verts * stride;
        if (bytes == 0) return D3D_OK;
        if (bytes > g.upVBsize) {
            if (g.upVB) g.upVB->Release();
            D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            g.dev->CreateBuffer(&d, nullptr, &g.upVB); g.upVBsize = bytes;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(g.ctx->Map(g.upVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return D3D_OK;
        memcpy(m.pData, data, bytes); g.ctx->Unmap(g.upVB, 0);
        ApplyDefaultState();
        g.ctx->IASetInputLayout(il);
        UINT off = 0; g.ctx->IASetVertexBuffers(0, 1, &g.upVB, &stride, &off);
        g.ctx->IASetPrimitiveTopology(Topo(pt));
        g.ctx->Draw(verts, 0);
        return D3D_OK;
    }
    // This was a stub that accepted the draw and rendered nothing. It is the call the
    // engine uses for dynamic indexed geometry: weapon trails, particles, the jump
    // wave. So the textured effect on top was never drawn, and all that survived was
    // the untextured quad behind it, which comes out as flat vertex colour (the white
    // square on the dagger swing, the jump and the weapon beam).
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE pt, UINT minIdx, UINT numVerts, UINT primCount,
                                      const void* idxData, D3DFORMAT idxFmt, const void* vtxData, UINT stride) {
        Stopwatch sw;
        InterlockedIncrement(&dbg_IUP);
        if (curVS != nullptr || !(fvf & D3DFVF_POSITION_MASK) || !g.dev || !g.vsDefaultBlob) return D3D_OK;
        if (!idxData || !vtxData || !stride || !primCount) return D3D_OK;
        ID3D11InputLayout* il = BuildInputLayoutFVF(fvf, g.vsDefaultBlob->GetBufferPointer(), g.vsDefaultBlob->GetBufferSize());
        if (!il) return D3D_OK;

        UINT idxCount = PrimCount(pt, primCount);
        bool i16 = (idxFmt == D3DFMT_INDEX16);
        UINT idxBytes = idxCount * (i16 ? 2u : 4u);
        // The indices are relative to vtxData, so the buffer has to cover minIdx too.
        UINT vtxBytes = (minIdx + numVerts) * stride;
        if (!idxBytes || !vtxBytes) return D3D_OK;

        CtxLock lk;
        if (vtxBytes > g.upVBsize) {
            if (g.upVB) g.upVB->Release();
            D3D11_BUFFER_DESC d{}; d.ByteWidth = vtxBytes; d.Usage = D3D11_USAGE_DYNAMIC;
            d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(g.dev->CreateBuffer(&d, nullptr, &g.upVB))) { g.upVBsize = 0; return D3D_OK; }
            g.upVBsize = vtxBytes;
        }
        if (idxBytes > g.upIBsize) {
            if (g.upIB) g.upIB->Release();
            D3D11_BUFFER_DESC d{}; d.ByteWidth = idxBytes; d.Usage = D3D11_USAGE_DYNAMIC;
            d.BindFlags = D3D11_BIND_INDEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(g.dev->CreateBuffer(&d, nullptr, &g.upIB))) { g.upIBsize = 0; return D3D_OK; }
            g.upIBsize = idxBytes;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(g.ctx->Map(g.upVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return D3D_OK;
        memcpy(m.pData, vtxData, vtxBytes); g.ctx->Unmap(g.upVB, 0);
        if (FAILED(g.ctx->Map(g.upIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return D3D_OK;
        memcpy(m.pData, idxData, idxBytes); g.ctx->Unmap(g.upIB, 0);

        ApplyDefaultState();
        g.ctx->IASetInputLayout(il);
        UINT off = 0; g.ctx->IASetVertexBuffers(0, 1, &g.upVB, &stride, &off);
        g.ctx->IASetIndexBuffer(g.upIB, i16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
        g.ctx->IASetPrimitiveTopology(Topo(pt));
        g.ctx->DrawIndexed(idxCount, 0, 0);
        return D3D_OK;
    }
    STDMETHOD(ProcessVertices)(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) { return D3D_OK; }
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* d) { curDecl = (NVDecl*)d; return D3D_OK; }
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** pp) { *pp = nullptr; return D3D_OK; }
    STDMETHOD(SetFVF)(DWORD f) { fvf = f; return D3D_OK; }
    STDMETHOD(GetFVF)(DWORD* f) { if (f) *f = fvf; return D3D_OK; }
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* s) { curVS = s; return D3D_OK; }
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** pp) { *pp = nullptr; return D3D_OK; }
    STDMETHOD(SetVertexShaderConstantF)(UINT, const float*, UINT) { return D3D_OK; }
    STDMETHOD(GetVertexShaderConstantF)(UINT, float*, UINT) { return D3D_OK; }
    STDMETHOD(SetVertexShaderConstantI)(UINT, const int*, UINT) { return D3D_OK; }
    STDMETHOD(GetVertexShaderConstantI)(UINT, int*, UINT) { return D3D_OK; }
    STDMETHOD(SetVertexShaderConstantB)(UINT, const BOOL*, UINT) { return D3D_OK; }
    STDMETHOD(GetVertexShaderConstantB)(UINT, BOOL*, UINT) { return D3D_OK; }
    STDMETHOD(SetStreamSource)(UINT n, IDirect3DVertexBuffer9* b, UINT off, UINT stride) { if (n == 0) { stream0 = (NVBuf*)b; stream0Off = off; stream0Stride = stride; } return D3D_OK; }
    STDMETHOD(GetStreamSource)(UINT, IDirect3DVertexBuffer9** pp, UINT* o, UINT* s) { if (pp) *pp = nullptr; if (o) *o = 0; if (s) *s = 0; return D3D_OK; }
    STDMETHOD(SetStreamSourceFreq)(UINT, UINT) { return D3D_OK; }
    STDMETHOD(GetStreamSourceFreq)(UINT, UINT* s) { if (s) *s = 1; return D3D_OK; }
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* i) { indices = (NIBuf*)i; return D3D_OK; }
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** pp) { if (pp) *pp = nullptr; return D3D_OK; }
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* s) { curPS = s; return D3D_OK; }
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** pp) { *pp = nullptr; return D3D_OK; }
    STDMETHOD(SetPixelShaderConstantF)(UINT, const float*, UINT) { return D3D_OK; }
    STDMETHOD(GetPixelShaderConstantF)(UINT, float*, UINT) { return D3D_OK; }
    STDMETHOD(SetPixelShaderConstantI)(UINT, const int*, UINT) { return D3D_OK; }
    STDMETHOD(GetPixelShaderConstantI)(UINT, int*, UINT) { return D3D_OK; }
    STDMETHOD(SetPixelShaderConstantB)(UINT, const BOOL*, UINT) { return D3D_OK; }
    STDMETHOD(GetPixelShaderConstantB)(UINT, BOOL*, UINT) { return D3D_OK; }
    STDMETHOD(DrawRectPatch)(UINT, const float*, const D3DRECTPATCH_INFO*) { return D3D_OK; }
    STDMETHOD(DrawTriPatch)(UINT, const float*, const D3DTRIPATCH_INFO*) { return D3D_OK; }
    STDMETHOD(DeletePatch)(UINT) { return D3D_OK; }
};

// offered resolutions (windowed). The game filters by the X8R8G8B8 format.
static const D3DDISPLAYMODE kModes[] = {
    {800,600,60,D3DFMT_X8R8G8B8}, {1024,768,60,D3DFMT_X8R8G8B8}, {1152,864,60,D3DFMT_X8R8G8B8},
    {1280,720,60,D3DFMT_X8R8G8B8}, {1280,800,60,D3DFMT_X8R8G8B8}, {1280,1024,60,D3DFMT_X8R8G8B8},
    {1366,768,60,D3DFMT_X8R8G8B8}, {1440,900,60,D3DFMT_X8R8G8B8}, {1600,900,60,D3DFMT_X8R8G8B8},
    {1680,1050,60,D3DFMT_X8R8G8B8}, {1920,1080,60,D3DFMT_X8R8G8B8}, {1920,1200,60,D3DFMT_X8R8G8B8},
    {2560,1440,60,D3DFMT_X8R8G8B8}, {3440,1440,60,D3DFMT_X8R8G8B8},
};
static const UINT kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// ================= objeto D3D9 (fabrica) =================
struct ND3D9 : Unk<IDirect3D9> {
    STDMETHOD(RegisterSoftwareDevice)(void*) { return D3D_OK; }
    STDMETHOD_(UINT, GetAdapterCount)() { return 1; }
    STDMETHOD(GetAdapterIdentifier)(UINT, DWORD, D3DADAPTER_IDENTIFIER9* id) {
        if (!id) return D3D_OK;
        ZeroMemory(id, sizeof(*id));
        lstrcpynA(id->Driver, "nvldumd.dll", sizeof(id->Driver));
        lstrcpynA(id->Description, "NVIDIA GeForce RTX", sizeof(id->Description));
        lstrcpynA(id->DeviceName, "\\\\.\\DISPLAY1", sizeof(id->DeviceName));
        id->DriverVersion.QuadPart = 0x000A00120000ULL;
        id->VendorId = 0x10DE; id->DeviceId = 0x2484; id->SubSysId = 0; id->Revision = 0xA1;
        id->WHQLLevel = 1;
        return D3D_OK;
    }
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT, D3DFORMAT) { return kModeCount; }
    STDMETHOD(EnumAdapterModes)(UINT, D3DFORMAT, UINT i, D3DDISPLAYMODE* m) { if (m && i < kModeCount) *m = kModes[i]; return D3D_OK; }
    STDMETHOD(GetAdapterDisplayMode)(UINT, D3DDISPLAYMODE* m) { if (m) { m->Width = 1024; m->Height = 768; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; } return D3D_OK; }
    STDMETHOD(CheckDeviceType)(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) { return D3D_OK; }
    STDMETHOD(CheckDeviceFormat)(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) { return D3D_OK; }
    STDMETHOD(CheckDeviceMultiSampleType)(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE, DWORD* q) { if (q) *q = 1; return D3D_OK; }
    STDMETHOD(CheckDepthStencilMatch)(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) { return D3D_OK; }
    STDMETHOD(CheckDeviceFormatConversion)(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) { return D3D_OK; }
    STDMETHOD(GetDeviceCaps)(UINT, D3DDEVTYPE, D3DCAPS9* c) { if (c) { ZeroMemory(c, sizeof(*c)); c->DeviceType = D3DDEVTYPE_HAL; c->VertexShaderVersion = D3DVS_VERSION(3, 0); c->PixelShaderVersion = D3DPS_VERSION(3, 0); c->MaxTextureWidth = c->MaxTextureHeight = 8192; c->MaxSimultaneousTextures = 8; c->MaxStreams = 16; c->NumSimultaneousRTs = 4; c->MaxVertexShaderConst = 256; c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_NONPOW2CONDITIONAL; c->TextureFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MIPFPOINT; c->MaxTextureAspectRatio = 8192; c->MaxAnisotropy = 16; } return D3D_OK; }
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT) { return MonitorFromWindow(GetActiveWindow(), MONITOR_DEFAULTTOPRIMARY); }
    STDMETHOD(CreateDevice)(UINT, D3DDEVTYPE, HWND hFocus, DWORD, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** ret) {
        HWND h = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : hFocus;
        Log("[ne] CreateDevice hwnd=%p %ux%u\n", h, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
        *ret = new NDevice(this, h, pp);
        return D3D_OK;
    }
};

// ---- integration API for the effect (backend_shared.h) ----
ID3D11Device* NE_Dev() { return g.dev; }
ID3D11DeviceContext* NE_Ctx() { return g.ctx; }
ID3D11ShaderResourceView* NE_SRV(IDirect3DBaseTexture9* tex9) { return tex9 ? ((NTexture*)tex9)->srv : nullptr; }
ID3D11ShaderResourceView* NE_DeviceTexSRV(UINT stage) { NDevice* d = (NDevice*)g_dev9; return (d && stage < 8 && d->tex[stage]) ? d->tex[stage]->srv : nullptr; }
// The last texture bound to stage 1 that actually looks like a light ramp (256x1).
ID3D11ShaderResourceView* NE_ShadeRampSRV() { return g_rampSRV; }
ID3D11PixelShader* NE_FixedFuncPS() { return g.psFF1; }
// The .fx passes with PixelShader = null use the fixed-function pixel pipeline.
// We upload the texture stage states + tfactor + alpha test to the device cbuffer and
// bind the per-stage textures/samplers, just like on the path without an effect.
void NE_BindFixedFuncPS() {
    NDevice* d = (NDevice*)g_dev9; if (!d || !g.psFF1) return;
    d->defaultCBValid = false;
    // Stage 0 with no texture makes PSCore break the whole cascade and return the
    // vertex color untouched (see the comment by that break -- it was the fix for the
    // jump-square/railgun white bug). The projected character shadow uses exactly this
    // fixed-function path (VS_ProjectiveShadow/VS_ProjectiveShadowCast), so if ITS
    // stage-0 texture never resolves, the same mechanism paints the vertex color
    // (commonly white, near-full alpha, for a shadow decal whose shape is meant to come
    // from the texture) instead of a dark gradient -- a white translucent smudge where
    // a shadow should be, not a missing shadow and not a black one.
    {
        static int logged = 0;
        if (!d->tex[0] && logged < 30) {
            ++logged;
            Log("[shadowdbg] fixed-function draw with NO stage-0 texture (tex[0]=null) -- vertex color will show through as-is\n");
        } else if (d->tex[0] && !d->tex[0]->srv && logged < 30) {
            ++logged;
            Log("[shadowdbg] fixed-function draw: stage-0 texture exists but srv=null (format 0x%08X, %ux%u) -- same white-passthrough result\n",
                (unsigned)d->tex[0]->fmt, d->tex[0]->w, d->tex[0]->h);
        }
    }
    CBData cb{};
    for (int st = 0; st < 4; ++st) {
        cb.stageC[st][0] = (float)d->tss[st][D3DTSS_COLOROP];
        cb.stageC[st][1] = (float)d->tss[st][D3DTSS_COLORARG1];
        cb.stageC[st][2] = (float)d->tss[st][D3DTSS_COLORARG2];
        cb.stageC[st][3] = d->tex[st] ? 1.f : 0.f;
        cb.stageA[st][0] = (float)d->tss[st][D3DTSS_ALPHAOP];
        cb.stageA[st][1] = (float)d->tss[st][D3DTSS_ALPHAARG1];
        cb.stageA[st][2] = (float)d->tss[st][D3DTSS_ALPHAARG2];
        cb.stageA[st][3] = (d->tex[st] && d->tex[st]->isA8) ? 1.f : 0.f;
    }
    DWORD af = d->rs[D3DRS_ALPHAFUNC];
    cb.pad0 = d->rs[D3DRS_ALPHATESTENABLE] ? (d->rs[D3DRS_ALPHAREF] & 0xFF) / 255.f : 0.f;
    cb.pad1 = (af == D3DCMP_LESS || af == D3DCMP_LESSEQUAL) ? 1.f : 0.f;
    DWORD tf = d->rs[D3DRS_TEXTUREFACTOR];
    cb.tfactor[0] = ((tf >> 16) & 0xFF) / 255.f; cb.tfactor[1] = ((tf >> 8) & 0xFF) / 255.f;
    cb.tfactor[2] = (tf & 0xFF) / 255.f;         cb.tfactor[3] = ((tf >> 24) & 0xFF) / 255.f;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(g.ctx->Map(g.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof(cb)); g.ctx->Unmap(g.cb, 0); }
    g.ctx->PSSetConstantBuffers(0, 1, &g.cb);
    ID3D11ShaderResourceView* srvs[4] = { d->tex[0] ? d->tex[0]->srv : nullptr, d->tex[1] ? d->tex[1]->srv : nullptr,
                                          d->tex[2] ? d->tex[2]->srv : nullptr, d->tex[3] ? d->tex[3]->srv : nullptr };
    g.ctx->PSSetShaderResources(0, 4, srvs);
    ID3D11SamplerState* samps[4];
    for (int st = 0; st < 4; ++st)
        samps[st] = GetSampler(d->ss[st][D3DSAMP_ADDRESSU], d->ss[st][D3DSAMP_ADDRESSV], d->ss[st][D3DSAMP_ADDRESSW],
                               d->ss[st][D3DSAMP_MINFILTER], d->ss[st][D3DSAMP_MAGFILTER], d->ss[st][D3DSAMP_MIPFILTER]);
    g.ctx->PSSetSamplers(0, 4, samps);
    { static LONG n = 0; if (InterlockedIncrement(&n) <= 16)
        Log("[ffps] st0 cop=%lu ca1=%lu ca2=%lu aop=%lu aa1=%lu aa2=%lu tex0=%d st1cop=%lu | blend=%lu src=%lu dst=%lu atest=%lu ref=%lu tfactor=%08lX\n",
            d->tss[0][D3DTSS_COLOROP], d->tss[0][D3DTSS_COLORARG1], d->tss[0][D3DTSS_COLORARG2],
            d->tss[0][D3DTSS_ALPHAOP], d->tss[0][D3DTSS_ALPHAARG1], d->tss[0][D3DTSS_ALPHAARG2],
            d->tex[0] ? (d->tex[0]->srv ? 1 : -1) : 0, d->tss[1][D3DTSS_COLOROP],
            d->rs[D3DRS_ALPHABLENDENABLE], d->rs[D3DRS_SRCBLEND], d->rs[D3DRS_DESTBLEND],
            d->rs[D3DRS_ALPHATESTENABLE], d->rs[D3DRS_ALPHAREF], d->rs[D3DRS_TEXTUREFACTOR]); }
}
void NE_FogState(float* fog4, float* color4) {
    NDevice* d = (NDevice*)g_dev9;
    if (!d) { fog4[0]=0; fog4[1]=1; fog4[2]=0; fog4[3]=0; color4[0]=color4[1]=color4[2]=color4[3]=1; return; }
    if (g_fogMapValid) {
        fog4[0] = g_fogMap[0]; fog4[1] = g_fogMap[1];
    } else {
        memcpy(&fog4[0], &d->rs[D3DRS_FOGSTART], 4);
        memcpy(&fog4[1], &d->rs[D3DRS_FOGEND], 4);
    }
    // TEST (NE_FOG_FORCE=1): ignores FOGENABLE and turns on the shader's fixed blend.
    static int force = -1;
    if (force < 0) { char b[8] = ""; force = (GetEnvironmentVariableA("NE_FOG_FORCE", b, sizeof(b)) && b[0] == '1') ? 1 : 0; }
    // Used to be read once per BeginPass, when the game can draw several objects
    // (e.g. the sky dome sharing a shader with fogged terrain) under one pass while
    // toggling FOGENABLE between them -- that stale read is why this used to fall
    // back to "the map HAS fog configured" instead of trusting it. Now that this is
    // called from BeginProgDraw, right before each individual draw, FOGENABLE is
    // accurate for THIS object, so it is authoritative again: trusting the map-wide
    // fallback over it was exactly what fogged the sky, which the game deliberately
    // draws with fog off.
    fog4[2] = (force || d->rs[D3DRS_FOGENABLE]) ? 1.f : 0.f;
    fog4[3] = force ? 1.f : 0.f;   // NE_FOG_FORCE also turns on the fixed test blend
    if (g_fogMapValid) {
        color4[0] = g_fogMap[2]; color4[1] = g_fogMap[3]; color4[2] = g_fogMap[4]; color4[3] = 1.f;
    } else {
        DWORD c = d->rs[D3DRS_FOGCOLOR];
        color4[0] = ((c >> 16) & 0xFF) / 255.f; color4[1] = ((c >> 8) & 0xFF) / 255.f;
        color4[2] = (c & 0xFF) / 255.f;         color4[3] = 1.f;
    }
}
void NE_SetFogValues(float minDist, float maxDist, float r, float g, float b) {
    g_fogMap[0] = minDist; g_fogMap[1] = maxDist;
    g_fogMap[2] = r; g_fogMap[3] = g; g_fogMap[4] = b;
    InterlockedExchange(&g_fogMapValid, 1);
}
ID3D11ShaderResourceView* NE_SceneSRV() { return g_sceneSRV; }
void NE_SetPassStates(const DWORD* states, const DWORD* values, int count) {
    if (count > 16) count = 16;
    g_passStateCount = 0;
    for (int i = 0; i < count; ++i) { g_passState[i] = states[i]; g_passValue[i] = values[i]; }
    g_passStateCount = count;
}
// Cached backbuffer surface for the render target stack guard. It carries no rtv of
// its own, and SetRenderTarget already falls back to the real backbuffer in that
// case, which is exactly what a failed pop should restore.
IDirect3DSurface9* NE_BackbufferSurface() {
    NDevice* d = (NDevice*)g_dev9;
    if (!d) return nullptr;
    IDirect3DSurface9* s = nullptr;
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &s);
    return s;   // the surface is cached and never destroyed, so the ref is fine to keep
}
bool NE_IsAdditive() { NDevice* d = (NDevice*)g_dev9; return d && d->rs[D3DRS_ALPHABLENDENABLE] && d->rs[D3DRS_DESTBLEND] == D3DBLEND_ONE; }
float NE_AlphaRef() {
    NDevice* d = (NDevice*)g_dev9; if (!d) return 0.f;
    if (d->rs[D3DRS_ALPHATESTENABLE]) { float r = (d->rs[D3DRS_ALPHAREF] & 0xFF) / 255.f; return r > 0.f ? r : 0.02f; }
    // the game draws glows/particles with alpha textures; without discarding the nearly
    // transparent parts you get opaque blotches (the web engine uses alphaTest 0.1)
    return 0.02f;
}
void NE_SetProgram(ID3D11VertexShader* vs, ID3D11PixelShader* ps, const void* vsbc, SIZE_T vslen, bool isSkinned) {
    g_prog.isSkinned = isSkinned;
    g_prog.vs = vs; g_prog.ps = ps; g_prog.vsbc = vsbc; g_prog.vslen = vslen; g_prog.active = true;
}
void NE_ClearProgram() { g_prog.active = false; g_passStateCount = 0; }

} // namespace ne

extern "C" IDirect3D9* WINAPI NE_Direct3DCreate9(UINT) {
    ne::Log("[ne] NE_Direct3DCreate9\n");
    return new ne::ND3D9();
}

void InstallNativeD3D9() {
    HMODULE host = GetModuleHandleA(nullptr);
    void* old = ne::PatchIAT(host, "d3d9.dll", "Direct3DCreate9", (void*)&NE_Direct3DCreate9);
    ne::Log("[ne] InstallNativeD3D9 patched=%d\n", old != nullptr);
}
