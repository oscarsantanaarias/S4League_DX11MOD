// effects.cpp -- NATIVE ID3DXEffect on top of D3D11.
//
// We intercept D3DXCreateEffect and return OUR ID3DXEffect (MyEffect):
//   - We parse the .fx technique/pass (per-pass VS/PS entry points).
//   - We compile each entry to SM4 with D3DCompile (+backwards-compat, +row-major).
//   - Reflection: we map params ($Globals) -> cbuffer offset, and textures -> slot.
//   - In BeginPass we bind VS/PS + cbuffers + SRVs to the D3D11 device; the game
//     draws and it renders natively.
//
// No DXSDK headers: the interface lives in d3dx9_min.h (exact vtable).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include "iat.h"
#include "log.h"
#include "d3dx9_min.h"
#include "backend_shared.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

namespace ne {

struct VarSlot { std::string name; UINT off, size; };
struct TexBind { std::string name; UINT slot; };

struct ShaderProg {
    bool built = false, ok = false, psIsFF = false;  // psIsFF: PixelShader = null -> fixed-function
    std::string vsName;   // the VS entry point, to know which pass draws what
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3DBlob* vsBlob = nullptr;
    ID3D11Buffer* vsCB = nullptr; UINT vsCBsize = 0; std::vector<VarSlot> vsVars; std::vector<BYTE> vsShadow;
    ID3D11Buffer* psCB = nullptr; UINT psCBsize = 0; std::vector<VarSlot> psVars; std::vector<BYTE> psShadow;
    std::vector<TexBind> psTex;
    std::string vsEntry, psEntry;
};
// A pass in a .fx also carries RENDER STATES, and D3DX applies them to the device
// when the pass begins. We were ignoring them entirely, so every pass inherited
// whatever the device had. The refraction pass asks for AlphaBlendEnable = false
// (it returns the refracted scene to REPLACE the background), and inheriting the
// additive blend of the surrounding effect summed the scene over itself and burned
// it to white -- the square on the wall jump, the plasma sword and the melee trails.
struct PassState { DWORD state; DWORD value; };
struct Pass {
    std::string vsEntry, psEntry;
    std::vector<PassState> states;
    ShaderProg* prog = nullptr;
};
struct Technique { std::string name; std::vector<Pass> passes; };

static std::string StripTechniques(const std::string& in) {
    std::string out; size_t i = 0;
    while (i < in.size()) {
        size_t t = in.find("technique", i);
        if (t == std::string::npos) { out += in.substr(i); break; }
        out += in.substr(i, t - i);
        size_t b = in.find('{', t); if (b == std::string::npos) break;
        int d = 0; size_t j = b;
        for (; j < in.size(); ++j) { if (in[j] == '{') d++; else if (in[j] == '}') { if (--d == 0) { j++; break; } } }
        i = j;
    }
    return out;
}
static std::string Token(const std::string& s, size_t& i) {
    while (i < s.size() && (isspace((unsigned char)s[i]))) i++;
    size_t st = i;
    while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
    return s.substr(st, i - st);
}
// extracts the entry from "compile vs_1_1 VS_Foo (" -> "VS_Foo"
static std::string CompileEntry(const std::string& s, size_t from) {
    size_t c = s.find("compile", from); if (c == std::string::npos) return "";
    size_t i = c + 7; Token(s, i); /* target vs_1_1 */ return Token(s, i); /* entry */
}

// maps sampler NAME = sampler_state { Texture = <g_TexX>; } -> NAME:g_TexX
// It also reports which samplers ask for CLAMP: using WRAP for all of them breaks
// the ones that index outside 0..1 (the reflection's tex2Dproj, the ShadeMap
// indexed by light): with WRAP the coords wrap around -> stripes; with CLAMP they
// stick to the edge.
static void ParseSamplers(const std::string& s, std::unordered_map<std::string, std::string>& m,
                          std::unordered_map<std::string, bool>* clampOut = nullptr) {
    size_t p = 0;
    while ((p = s.find("sampler_state", p)) != std::string::npos) {
        size_t eq = s.rfind('=', p);
        if (eq == std::string::npos) { p += 13; continue; }
        size_t e = eq; while (e > 0 && isspace((unsigned char)s[e - 1])) e--;
        size_t b = e; while (b > 0 && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_')) b--;
        std::string name = s.substr(b, e - b);
        size_t ob = s.find('{', p); if (ob == std::string::npos) { p += 13; continue; }
        int d = 0; size_t j = ob; for (; j < s.size(); ++j) { if (s[j] == '{') d++; else if (s[j] == '}') { if (--d == 0) { j++; break; } } }
        std::string block = s.substr(ob, j - ob);
        size_t tp = block.find("Texture");
        if (tp != std::string::npos) {
            std::string tex; size_t lt = block.find('<', tp);
            if (lt != std::string::npos) { size_t gt = block.find('>', lt); if (gt != std::string::npos) tex = block.substr(lt + 1, gt - lt - 1); }
            else { size_t a = block.find('=', tp); if (a != std::string::npos) { size_t k = a + 1; while (k < block.size() && isspace((unsigned char)block[k])) k++; size_t ks = k; while (k < block.size() && (isalnum((unsigned char)block[k]) || block[k] == '_')) k++; tex = block.substr(ks, k - ks); } }
            while (!tex.empty() && isspace((unsigned char)tex.front())) tex.erase(tex.begin());
            while (!tex.empty() && isspace((unsigned char)tex.back())) tex.pop_back();
            if (!name.empty() && !tex.empty()) m[name] = tex;
        }
        if (clampOut && !name.empty()) {
            size_t ap = block.find("AddressU");
            if (ap != std::string::npos) {
                size_t eqp = block.find('=', ap);
                size_t cp = (eqp == std::string::npos) ? std::string::npos : block.find("CLAMP", eqp);
                if (cp != std::string::npos && cp < eqp + 24) (*clampOut)[name] = true;
            }
        }
        p = j;
    }
}

// The render states a .fx pass is allowed to set. Only the ones that change what
// ends up on screen; the rest of the D3DRS space is not worth carrying.
static DWORD PassStateValue(const std::string& v) {
    if (v == "TRUE" || v == "true" || v == "1") return 1;
    if (v == "FALSE" || v == "false" || v == "0") return 0;
    if (v == "ZERO") return D3DBLEND_ZERO;             if (v == "ONE") return D3DBLEND_ONE;
    if (v == "SRCALPHA") return D3DBLEND_SRCALPHA;     if (v == "INVSRCALPHA") return D3DBLEND_INVSRCALPHA;
    if (v == "SRCCOLOR") return D3DBLEND_SRCCOLOR;     if (v == "INVSRCCOLOR") return D3DBLEND_INVSRCCOLOR;
    if (v == "DESTALPHA") return D3DBLEND_DESTALPHA;   if (v == "INVDESTALPHA") return D3DBLEND_INVDESTALPHA;
    if (v == "DESTCOLOR") return D3DBLEND_DESTCOLOR;   if (v == "INVDESTCOLOR") return D3DBLEND_INVDESTCOLOR;
    if (v == "NONE") return D3DCULL_NONE;              if (v == "CW") return D3DCULL_CW;
    if (v == "CCW") return D3DCULL_CCW;
    if (v == "ADD") return D3DBLENDOP_ADD;
    if (v == "NEVER") return D3DCMP_NEVER;             if (v == "LESS") return D3DCMP_LESS;
    if (v == "EQUAL") return D3DCMP_EQUAL;             if (v == "LESSEQUAL") return D3DCMP_LESSEQUAL;
    if (v == "GREATER") return D3DCMP_GREATER;         if (v == "GREATEREQUAL") return D3DCMP_GREATEREQUAL;
    if (v == "ALWAYS") return D3DCMP_ALWAYS;
    return (DWORD)atoi(v.c_str());
}
static void ParsePassStates(const std::string& block, std::vector<PassState>& out) {
    static const struct { const char* name; DWORD rs; } kMap[] = {
        { "AlphaBlendEnable", D3DRS_ALPHABLENDENABLE }, { "SrcBlend",       D3DRS_SRCBLEND },
        { "DestBlend",        D3DRS_DESTBLEND },        { "BlendOp",        D3DRS_BLENDOP },
        { "ZEnable",          D3DRS_ZENABLE },          { "ZWriteEnable",   D3DRS_ZWRITEENABLE },
        { "ZFunc",            D3DRS_ZFUNC },            { "CullMode",       D3DRS_CULLMODE },
        { "AlphaTestEnable",  D3DRS_ALPHATESTENABLE },  { "AlphaRef",       D3DRS_ALPHAREF },
        { "AlphaFunc",        D3DRS_ALPHAFUNC },        { "FogEnable",      D3DRS_FOGENABLE },
        { "ColorWriteEnable", D3DRS_COLORWRITEENABLE },
    };
    for (const auto& m : kMap) {
        size_t p = block.find(m.name);
        if (p == std::string::npos) continue;
        // must be the whole token, not a prefix of another state
        if (p && (isalnum((unsigned char)block[p - 1]) || block[p - 1] == '_')) continue;
        size_t eq = block.find('=', p); if (eq == std::string::npos) continue;
        size_t semi = block.find(';', eq); if (semi == std::string::npos) continue;
        std::string v = block.substr(eq + 1, semi - eq - 1);
        size_t a = v.find_first_not_of(" \t\r\n"); size_t b = v.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        v = v.substr(a, b - a + 1);
        out.push_back({ m.rs, PassStateValue(v) });
    }
}

static void ParseTechniques(const std::string& fx, std::vector<Technique>& techs) {
    size_t i = 0;
    while (true) {
        size_t t = fx.find("technique", i); if (t == std::string::npos) break;
        size_t nameStart = t + 9; size_t ni = nameStart; std::string tname = Token(fx, ni);
        size_t tb = fx.find('{', t); if (tb == std::string::npos) break;
        int d = 0; size_t te = tb;
        for (; te < fx.size(); ++te) { if (fx[te] == '{') d++; else if (fx[te] == '}') { if (--d == 0) { te++; break; } } }
        Technique tech; tech.name = tname;
        size_t p = tb;
        while (true) {
            size_t ps = fx.find("pass", p); if (ps == std::string::npos || ps > te) break;
            size_t pb = fx.find('{', ps); if (pb == std::string::npos || pb > te) break;
            int pd = 0; size_t pe = pb;
            for (; pe < fx.size(); ++pe) { if (fx[pe] == '{') pd++; else if (fx[pe] == '}') { if (--pd == 0) { pe++; break; } } }
            std::string block = fx.substr(pb, pe - pb);
            Pass pass;
            size_t vpos = block.find("VertexShader");
            size_t ppos = block.find("PixelShader");
            if (vpos != std::string::npos) pass.vsEntry = CompileEntry(block, vpos);
            if (ppos != std::string::npos) pass.psEntry = CompileEntry(block, ppos);
            ParsePassStates(block, pass.states);
            tech.passes.push_back(pass);
            p = pe;
        }
        techs.push_back(tech);
        i = te;
    }
}

struct MyEffect : public ID3DXEffect {
    LONG ref = 1;
    std::string hlsl;
    std::vector<Technique> techs;
    int curTech = -1;
    bool isPostFx = false; // FullSceneGlow/Blur/Haze/Refraction: post-processing to a render target (not implemented)
    std::set<std::string> interned;
    std::unordered_map<std::string, std::vector<BYTE>> params;       // value by name
    std::unordered_map<std::string, IDirect3DBaseTexture9*> texParams; // texture by name
    std::unordered_map<std::string, std::string> samplerToTex;        // sampler -> texture
    std::unordered_map<std::string, bool> samplerClamp;               // sampler -> asks for CLAMP
    ID3D11SamplerState* sampClamp = nullptr;
    ID3D11SamplerState* samp = nullptr;
    ID3D11ShaderResourceView* whiteSRV = nullptr;
    ID3D11ShaderResourceView* graySRV = nullptr;  // neutral for lightmap/shademap (modulate2x)
    ID3D11ShaderResourceView* blackSRV = nullptr; // neutral for additive/shadows
    ID3D11ShaderResourceView* normalSRV = nullptr; // neutral for bump/normal maps: (0,0,1)

    static void ReplaceAll(std::string& s, const char* a, const char* b) { size_t p = 0, la = strlen(a); while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, la, b); p += strlen(b); } }
    MyEffect(const char* src, UINT len) {
        std::string full(src, len);
        // The fullscreen post-processing .fx files (Haze/FullSceneGlow/FullSceneBlur/
        // Refraction) render to a render target we do not implement -> they wash the
        // scene out to white. The world shader has g_matBone (skinning); the post
        // effects do NOT.
        {
            // The fullscreen post effects sample a render target (which I do not
            // provide -> white fallback). They are detected by their own uniforms that
            // the world shader (63KB, has g_matBone) does NOT use: g_TexBlurMap,
            // g_fColorRev, g_TexCoordOffset.
            auto has = [&](const char* w){ return full.find(w) != std::string::npos; };
            // SceneMap/tex2Dproj: samples the SCENE render target (which I do not have)
            // -> white fallback -> white object. That is an unmistakable sign of post
            // processing / refraction, REGARDLESS of g_matBone (the distortion shader
            // skins too).
            bool scene = has("SceneMap") || has("tex2Dproj") || has("g_LuminanceConv");
            bool world = has("g_matBone");
            isPostFx = scene || (!world && (has("g_TexBlurMap") || has("g_BlurFactor") || has("g_fColorRev")
                        || has("g_fOrgColorRev") || has("g_TexCoordOffset") || has("g_TexScreen")
                        || has("g_TexScene") || has("Haze") || has("FullScene")));
        }
        if (isPostFx) Log("[fx] post-processing skipped (samples a render target): len=%u\n", len);
        hlsl = StripTechniques(full);
        ParseTechniques(full, techs);
        ParseSamplers(full, samplerToTex, &samplerClamp);
        ParseDefaults(full);
        if (ID3D11Device* dev = NE_Dev()) {
            D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_ANISOTROPIC; sd.MaxAnisotropy = 16;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = 0.0f; // mip 0 only (D3DX does not fill the higher levels)
            dev->CreateSamplerState(&sd, &samp);
            { D3D11_SAMPLER_DESC cd = sd; cd.AddressU = cd.AddressV = cd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
              dev->CreateSamplerState(&cd, &sampClamp); }
            // 1x1 white fallback texture (so the geometry shows up even if the texture is missing)
            D3D11_TEXTURE2D_DESC td{}; td.Width = td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            UINT white = 0xFFFFFFFF; D3D11_SUBRESOURCE_DATA srd{}; srd.pSysMem = &white; srd.SysMemPitch = 4;
            ID3D11Texture2D* wt = nullptr; if (SUCCEEDED(dev->CreateTexture2D(&td, &srd, &wt)) && wt) { dev->CreateShaderResourceView(wt, nullptr, &whiteSRV); wt->Release(); }
            // lightmap/shademap are applied MODULATE2X (color * map * 2): the neutral is GRAY 0.5, not white.
            UINT gray = 0xFF808080; srd.pSysMem = &gray;
            ID3D11Texture2D* gt = nullptr; if (SUCCEEDED(dev->CreateTexture2D(&td, &srd, &gt)) && gt) { dev->CreateShaderResourceView(gt, nullptr, &graySRV); gt->Release(); }
            UINT black = 0x00000000; srd.pSysMem = &black;
            ID3D11Texture2D* bt = nullptr; if (SUCCEEDED(dev->CreateTexture2D(&td, &srd, &bt)) && bt) { dev->CreateShaderResourceView(bt, nullptr, &blackSRV); bt->Release(); }
            // A bump map is not a colour, and in THIS shader family it is not used as a
            // normal either -- every use is a dot product against the light or half vector
            // in tangent space:
            //     BumpMapColor = (tex - 0.5) * 2;
            //     DifPow   = g_BumpDepth * dot(BumpMapColor, In.L) + 0.5;
            //     SpePower = pow(max(dot(BumpMapColor, In.H), 0), g_SpecularShiness);
            // So the neutral is the ZERO vector, i.e. 0.5 grey, which gives dot = 0 and a
            // constant DifPow of 0.5. The old (0.5,0.5,1) "flat normal" decodes to (0,0,1),
            // whose dot with L is L.z -- that makes the brightness swing with the light
            // angle and blow out to white from certain camera positions. Same family as the
            // white characters: a lighting term that depends on the angle when it must not.
            UINT flat = 0xFF808080; srd.pSysMem = &flat;   // R=G=B=0x80 -> (tex-0.5)*2 = 0
            ID3D11Texture2D* nt = nullptr; if (SUCCEEDED(dev->CreateTexture2D(&td, &srd, &nt)) && nt) { dev->CreateShaderResourceView(nt, nullptr, &normalSRV); nt->Release(); }
        }
        CollectParamNames();
        Log("[fx] MyEffect: %d techniques, %d parameters, hlsl %d bytes\n",
            (int)techs.size(), (int)paramNames.size(), (int)hlsl.size());
        // Dump the .fx source so the transpiled shaders can be read offline. The
        // effects that render white resolve every texture and get the right blend
        // state, so whatever is wrong is inside the shader itself.
        { char e[8] = "";
          if (GetEnvironmentVariableA("NE_FX_DUMP", e, sizeof(e)) && e[0] == '1') {
            static LONG idx = 0; LONG i = InterlockedIncrement(&idx);
            char path[64]; wsprintfA(path, "fx_dump_%03ld.fx", i);
            HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, full.data(), (DWORD)full.size(), &w, nullptr); CloseHandle(h); }
          } }
    }
    ~MyEffect() { if (graySRV) graySRV->Release(); if (whiteSRV) whiteSRV->Release(); if (samp) samp->Release(); }

    D3DXHANDLE Intern(const char* n) { if (!n) return nullptr; auto it = interned.insert(n).first; return it->c_str(); }

    // The .fx carries initializers (`int g_SpecularShiness = 16;`, `float g_BumpDepth = 0.75;`,
    // `float3 g_SaturationConst = float3(...)`) that the D3DX framework applies. D3D11
    // reflection does NOT expose them (DefaultValue is usually NULL), so we parse them
    // from the source. Without this g_SpecularShiness=0 -> pow(x,0)=1 -> blown-out
    // specular (white depending on the camera).
    void ParseDefaults(const std::string& s) {
        static const char* kTypes[] = { "float4", "float3", "float2", "float", "int", "bool", "half" };
        // Only GLOBAL declarations are parameters. This used to scan the whole source, so
        // shader LOCALS ("float4 RefractionMapColor = tex2D(...);" inside a function body)
        // were registered as parameters with a bogus default. That is the same bug that
        // once left g_matBone empty -> broken skinning -> garbage normal -> DifPow NaN ->
        // WHITE CHARACTERS. Uniforms sit at brace depth 0; function bodies and
        // sampler_state blocks are deeper, so a depth map separates them.
        std::vector<int> depth(s.size() + 1, 0);
        { int d = 0;
          for (size_t i = 0; i < s.size(); ++i) {
              if (s[i] == '{') { depth[i] = d; d++; }
              else if (s[i] == '}') { if (d > 0) d--; depth[i] = d; }
              else depth[i] = d;
          } }
        for (const char* ty : kTypes) {
            size_t tlen = strlen(ty), p = 0;
            while ((p = s.find(ty, p)) != std::string::npos) {
                size_t q = p + tlen;
                if (depth[p] != 0) { p = q; continue; }   // inside a function or a block
                if (p > 0 && (isalnum((unsigned char)s[p - 1]) || s[p - 1] == '_')) { p = q; continue; }
                if (q < s.size() && (isalnum((unsigned char)s[q]) || s[q] == '_')) { p = q; continue; }
                while (q < s.size() && isspace((unsigned char)s[q])) q++;
                size_t ns = q; while (q < s.size() && (isalnum((unsigned char)s[q]) || s[q] == '_')) q++;
                std::string name = s.substr(ns, q - ns);
                while (q < s.size() && isspace((unsigned char)s[q])) q++;
                if (name.empty() || q >= s.size() || s[q] != '=') { p = ns + 1; continue; }
                size_t semi = s.find(';', q); if (semi == std::string::npos) break;
                std::string rhs = s.substr(q + 1, semi - q - 1);
                // Strip a leading constructor before scanning for numbers. The digit in the
                // TYPE NAME is a number too: "float3( 0.2126, 0.7152, 0.0722 )" was read as
                // (3.0, 0.2126, 0.7152), shifting every component by one. That is why
                // g_SaturationConst came out as 3.0 while g_LuminanceConv, written with
                // braces instead, parsed correctly.
                {
                    size_t par = rhs.find('(');
                    if (par != std::string::npos) {
                        std::string head = rhs.substr(0, par);
                        size_t a = head.find_first_not_of(" \t\r\n");
                        if (a != std::string::npos &&
                            (head.compare(a, 5, "float") == 0 || head.compare(a, 4, "half") == 0 ||
                             head.compare(a, 3, "int") == 0 || head.compare(a, 6, "matrix") == 0 ||
                             head.compare(a, 4, "bool") == 0))
                            rhs = rhs.substr(par + 1);
                    }
                }
                // pull the numbers out of the right hand side
                std::vector<float> v; size_t i = 0;
                while (i < rhs.size()) {
                    if (isdigit((unsigned char)rhs[i]) || (rhs[i] == '-' && i + 1 < rhs.size() && isdigit((unsigned char)rhs[i + 1])) || rhs[i] == '.') {
                        size_t st = i; while (i < rhs.size() && (isdigit((unsigned char)rhs[i]) || rhs[i] == '.' || rhs[i] == '-' || rhs[i] == 'e' || rhs[i] == 'f')) i++;
                        std::string num = rhs.substr(st, i - st);
                        if (!num.empty() && num.back() == 'f') num.pop_back();
                        try { v.push_back((float)atof(num.c_str())); } catch (...) {}
                    } else i++;
                }
                if (!v.empty()) {
                    std::vector<BYTE> bytes;
                    bool isInt = (strcmp(ty, "int") == 0 || strcmp(ty, "bool") == 0);
                    for (float f : v) {
                        if (isInt) { int n = (int)f; BYTE* b = (BYTE*)&n; bytes.insert(bytes.end(), b, b + 4); }
                        else { BYTE* b = (BYTE*)&f; bytes.insert(bytes.end(), b, b + 4); }
                    }
                    // Log() is wvsprintfA: no %f and no %g. Printing the float here made the
                    // line read "default X = g (0 vals)", which looks like a parse failure and
                    // is not one -- the %g swallowed the argument and %d then read garbage.
                    if (!params.count(name)) { params[name] = bytes; Log("[fx] default %s = %d/1000 (%d vals)\n", name.c_str(), (int)(v[0] * 1000.0f), (int)v.size()); }
                }
                p = semi;
            }
        }
    }

    // D3D11 has no alpha test (D3DRS_ALPHATESTENABLE): it is emulated with clip() in the PS.
    // Without it, the transparent areas of glows/particles are drawn OPAQUE -> white
    // blotches that move with the camera (they are billboards). The web engine uses alphaTest 0.1.
    std::string src;   // the source to compile (with the alpha test wrapper if it applies)
    std::string atStruct;   // the PS input struct, for the fog wrapper
    // Find an identifier as a WHOLE WORD, not as a substring. Every entry point here is a
    // prefix of several others -- "PS_DiffuseMap" is the start of PS_DiffuseMap_Light0,
    // PS_DiffuseMap_LightMap, PS_DiffuseMap_BumpMap_RefractionMap and a dozen more, and
    // "sPS_DiffuseMap_LightMap" even contains it mid-token. A plain find() therefore lands
    // on the WRONG function, and the wrapper built from it declares an input struct the
    // vertex shader never fills: the pixel shader then reads uninitialised interpolants,
    // which is the same failure as the old white-geometry bug (a shader input with no real
    // data behind it) and shows up the same way -- white that changes with the camera.
    size_t FindWord(const std::string& hay, const std::string& word, size_t from) const {
        while (true) {
            size_t p = hay.find(word, from);
            if (p == std::string::npos) return std::string::npos;
            bool okL = (p == 0) || !(isalnum((unsigned char)hay[p - 1]) || hay[p - 1] == '_');
            size_t e = p + word.size();
            bool okR = (e >= hay.size()) || !(isalnum((unsigned char)hay[e]) || hay[e] == '_');
            if (okL && okR) return p;
            from = p + 1;
        }
    }
    // Member names of a struct declared in the .fx.
    std::vector<std::string> StructMembers(const std::string& sname) {
        std::vector<std::string> out;
        size_t p = FindWord(hlsl, sname, 0);
        while (p != std::string::npos) {
            size_t br = hlsl.find('{', p);
            size_t semi = hlsl.find(';', p);
            if (br == std::string::npos) break;
            if (semi != std::string::npos && semi < br) { p = FindWord(hlsl, sname, p + 1); continue; }
            size_t end = hlsl.find('}', br);
            if (end == std::string::npos) break;
            std::string body = hlsl.substr(br + 1, end - br - 1);
            size_t i = 0;
            while (i < body.size()) {
                size_t sc = body.find(';', i);
                if (sc == std::string::npos) break;
                std::string decl = body.substr(i, sc - i);
                size_t colon = decl.find(':');
                if (colon != std::string::npos) decl = decl.substr(0, colon);
                size_t e = decl.find_last_not_of(" \t\n\n");
                if (e != std::string::npos) {
                    size_t b = e;
                    while (b > 0 && (isalnum((unsigned char)decl[b - 1]) || decl[b - 1] == '_')) b--;
                    std::string nm = decl.substr(b, e - b + 1);
                    if (!nm.empty()) out.push_back(nm);
                }
                i = sc + 1;
            }
            break;
        }
        return out;
    }
    // Return type of a function = the token right before its name.
    std::string FuncReturnType(const std::string& entry) {
        if (entry.empty()) return "";
        size_t p = FindWord(hlsl, entry, 0);
        while (p != std::string::npos) {
            size_t op = hlsl.find('(', p);
            if (op != std::string::npos && hlsl.find_first_not_of(" \t\n\n", p + entry.size()) == op) {
                size_t e = p; while (e > 0 && isspace((unsigned char)hlsl[e - 1])) e--;
                size_t b = e; while (b > 0 && (isalnum((unsigned char)hlsl[b - 1]) || hlsl[b - 1] == '_')) b--;
                std::string t = hlsl.substr(b, e - b);
                if (!t.empty() && t != "compile") return t;
            }
            p = FindWord(hlsl, entry, p + 1);
        }
        return "";
    }
    std::string AlphaTestWrapper(const std::string& entry, const std::string& vsEntry) {
        // signature: float4 <entry>( <Struct> In ) : COLOR
        size_t p = FindWord(hlsl, entry, 0);
        while (p != std::string::npos) {
            size_t op = hlsl.find('(', p);
            if (op == std::string::npos) break;
            size_t cp = hlsl.find(')', op);
            if (cp == std::string::npos) break;
            std::string args = hlsl.substr(op + 1, cp - op - 1);
            size_t a = args.find_first_not_of(" \t\r\n");
            if (a != std::string::npos) {
                size_t b = args.find_first_of(" \t\r\n", a);
                if (b != std::string::npos) {
                    std::string sname = args.substr(a, b - a);
                    if (!sname.empty() && sname != "float4" && sname != "float3" && sname != "float2" && sname != "float") {
                        // TEST (NE_NOLIGHT=1): return ONLY the diffuse texture, without the
                        // lighting term. If everything looks right that way, the "white/black"
                        // is entirely in the lighting (g_vLightColor/g_vLightPos/normals).
                        char nl[8];
                        if (GetEnvironmentVariableA("NE_NOLIGHT", nl, 8))
                            return "\nfloat g_NE_AlphaRef;\nfloat4 " + entry + "_NEAT(" + sname + " In) : COLOR {\n"
                                   "  float4 c = tex2D(DiffuseMapSampler, In.DifMapUV);\n  clip(c.a - g_NE_AlphaRef);\n  return c;\n}\n";
                        // Fog was left OUT on purpose: wrapping the PS with
                        // `In.Position.w` does not compile (POSITION cannot be read in a
                        // pixel shader) and the silent fallback left those passes without
                        // alpha test either. See renderer_documentation.txt.
                        // Storing the struct enables the FOG wrapper, which is compiled
                        // ON TOP of this one. Without it atStruct stayed empty and _NEFOG
                        // was never used: that is why there was no fog even though it
                        // compiled and the map values arrived fine. The fallback stays
                        // independent.
                        atStruct = sname;   // enables the fog wrapper (fog now rides b1, per draw)
                        // The .fx pairs pixel shaders with vertex shaders that do NOT write
                        // every member the PS reads: Technique_DiffuseMap_Rigid runs
                        // VS_DiffuseMap_Rigid (Pos + DifMapUV only) into
                        // PS_DiffuseMap_BumpMap_RefractionMap, which reads In.L and In.H.
                        // Those interpolants have no data behind them, and since L and H are
                        // DIRECTIONS the garbage changes with where the camera looks: white
                        // looking down, fine looking up. Same failure as the old white
                        // geometry bug, where a COLOR0 with no data read vertex positions.
                        // Give the orphans a defined value so the term is simply neutral.
                        std::string zero;
                        std::string vsRet = FuncReturnType(vsEntry);
                        if (!vsRet.empty() && vsRet != sname) {
                            auto psM = StructMembers(sname);
                            auto vsM = StructMembers(vsRet);
                            for (auto& m : psM) {
                                bool have = false;
                                for (auto& v : vsM) if (v == m) { have = true; break; }
                                if (!have) zero += "  In." + m + " = 0;\n";
                            }
                            if (!zero.empty()) {
                                static std::set<std::string> logged;
                                if (logged.size() < 30 && logged.insert(entry).second)
                                    Log("[fx] %s reads interpolants %s never writes -> zeroed\n",
                                        entry.c_str(), vsEntry.c_str());
                            }
                        }
                        return "\nfloat g_NE_AlphaRef;\nfloat4 " + entry + "_NEAT(" + sname + " In) : COLOR {\n"
                               + zero +
                               "  float4 c = " + entry + "(In);\n  clip(c.a - g_NE_AlphaRef);\n  return c;\n}\n";
                    }
                }
            }
            p = FindWord(hlsl, entry, p + 1);
        }
        return "";
    }
    // Fog layer SEPARATE from the alpha test: if this one does not compile we fall
    // back to the alpha test wrapper, which still works. They used to share a
    // function and a fog failure left those passes without alpha test (it broke the UI).
    // View depth comes from the pixel shader's SV_Position.w (which is 1/w).
    // Finds the member of the PS input struct carrying the POSITION semantic. The .fx
    // structs do not call it "Position" (hence "invalid subscript 'Position'"), and we
    // cannot just add our own SV_Position parameter either, because with backwards
    // compatibility POSITION already maps to it and D3DCompile rejects the duplicate
    // ("X4574: Duplicate system value semantic"). So we read the one that is there.
    std::string PositionMember(const std::string& sname) {
        // Left as a plain find on purpose: making this whole-word changes whether the FOG
        // wrapper compiles, which changes the compiled entry point and therefore what gets
        // bound. Reverted to keep the known-good state while the white quad is unexplained.
        size_t p = hlsl.find("struct " + sname);
        if (p == std::string::npos) return "";
        size_t b = hlsl.find('{', p); if (b == std::string::npos) return "";
        size_t e = hlsl.find('}', b);  if (e == std::string::npos) return "";
        std::string body = hlsl.substr(b + 1, e - b - 1);
        for (size_t i = 0; (i = body.find("POSITION", i)) != std::string::npos; ) {
            // walk back over ": " to the member name that precedes it
            size_t c = body.rfind(':', i);
            if (c == std::string::npos) { i += 8; continue; }
            size_t q = c; while (q > 0 && isspace((unsigned char)body[q - 1])) q--;
            size_t s = q; while (s > 0 && (isalnum((unsigned char)body[s - 1]) || body[s - 1] == '_')) s--;
            if (s < q) return body.substr(s, q - s);
            i += 8;
        }
        return "";
    }
    static void AddFogMember(std::string& source, const std::string& sname) {
        size_t p = source.find("struct " + sname);
        if (p == std::string::npos || source.find("NE_FogDepth", p) != std::string::npos) return;
        size_t b = source.find('{', p); size_t e = b == std::string::npos ? std::string::npos : source.find('}', b);
        if (e != std::string::npos) source.insert(e, "  float NE_FogDepth : TEXCOORD7;\n");
    }
    std::string WrapVertexFog(const std::string& entry, std::string& source) {
        std::string out = FuncReturnType(entry);
        if (out.empty()) return "";
        size_t p = source.find(entry);
        if (p == std::string::npos) return "";
        size_t op = source.find('(', p), cp = op == std::string::npos ? std::string::npos : source.find(')', op);
        if (cp == std::string::npos) return "";
        std::string args = source.substr(op + 1, cp - op - 1);
        size_t a = args.find_last_not_of(" \t\r\n");
        size_t q = a == std::string::npos ? std::string::npos : args.rfind(' ', a);
        if (q == std::string::npos) return "";
        std::string arg = args.substr(q + 1, a - q);
        std::string ret = out;
        std::string pos = PositionMember(out);
        if (pos.empty() || arg.empty()) return "";
        AddFogMember(source, out);
        source.replace(p, entry.size(), entry + "_NEORIG");
        source += "\n" + ret + " " + entry + "(" + args + ") {\n"
                  "  " + ret + " o = " + entry + "_NEORIG(" + arg + ");\n"
                  "  o.NE_FogDepth = o." + pos + ".w;\n"
                  "  return o;\n}\n";
        return entry;
    }
    // The fog constants live in their own cbuffer at b1, NOT in $Globals (b0). The
    // effect's cbuffer is uploaded once per BeginPass, but the game toggles FOGENABLE
    // per object, so reading it there gave the wrong answer and the previous code had
    // to guess with "the map has fog configured" -- which also fogged the UI, which is
    // why the whole wrapper ended up disabled. b1 is refilled on every draw by the
    // backend, so each object gets its own real enable.
    std::string FogWrapper(const std::string& entry, const std::string& sname) {
        return "\ncbuffer NE_FogCB : register(b1) { float4 g_NE_Fog; float4 g_NE_FogColor; };\n"
               "float4 " + entry + "_NEFOG(" + sname + " In) : COLOR {\n"
               "  float4 c = " + entry + "_NEAT(In);\n"
               "  if (g_NE_Fog.z > 0.5) {\n"
               "    float d = In.NE_FogDepth;\n"
               "    float f = saturate((g_NE_Fog.y - d) / max(g_NE_Fog.y - g_NE_Fog.x, 0.001));\n"
               "    c.rgb = lerp(g_NE_FogColor.rgb, c.rgb, f);\n  }\n"
               "  return c;\n}\n";
    }
    bool CompileOne(const std::string& entry, const char* target, ShaderProg& sp, bool isVS, const std::string& vsEntry = std::string()) {
        if (entry.empty()) {
            if (isVS) return false;
            // PixelShader = null -> fixed function: sample the stage0 texture * vertex color.
            static const char* kNullPS =
                "Texture2D neT:register(t0); SamplerState neS:register(s0);"
                "float4 NE_NullPS(float4 p:SV_Position, float4 c:COLOR0, float2 uv:TEXCOORD0):SV_Target{"
                " return neT.Sample(neS, uv) * c; }";
            ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
            if (SUCCEEDED(D3DCompile(kNullPS, strlen(kNullPS), "nullps", nullptr, nullptr, "NE_NullPS", "ps_4_0", 0, 0, &code, &err)) && code) {
                NE_Dev()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &sp.ps);
                code->Release(); sp.psTex.push_back({ "neT", 0 }); // the sampler already goes to slots 0..4
                if (err) err->Release(); return sp.ps != nullptr;
            }
            if (err) err->Release(); if (code) code->Release(); return false;
        }
        std::string use = hlsl; std::string ent = entry;
        std::string atOnly; // intermediate level: alpha test only
        if (isVS) WrapVertexFog(entry, use);
        if (!isVS) {
            atStruct.clear();
            std::string w = AlphaTestWrapper(entry, vsEntry);
            if (!w.empty()) {
                atOnly = hlsl + w; use = atOnly; ent = entry + "_NEAT";
                if (!atStruct.empty()) {
                    AddFogMember(atOnly, atStruct);
                    std::string fw = FogWrapper(entry, atStruct);
                    if (!fw.empty()) { use = atOnly + fw; ent = entry + "_NEFOG"; }
                }
            }
        }
        ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
        HRESULT hr = D3DCompile(use.data(), use.size(), "fx", nullptr, nullptr, ent.c_str(), target, flags, 0, &code, &err);
        if (FAILED(hr) && !atOnly.empty() && ent == entry + "_NEFOG") { // fog out, alpha test in
            static LONG nf = 0; if (InterlockedIncrement(&nf) <= 6) {
                Log("[fx] fog does not compile in %s:\n", entry.c_str());
                LogLong("[fx]  !", err ? (char*)err->GetBufferPointer() : "?");
            }
            if (err) { err->Release(); err = nullptr; }
            ent = entry + "_NEAT";
            hr = D3DCompile(atOnly.data(), atOnly.size(), "fx", nullptr, nullptr, ent.c_str(), target, flags, 0, &code, &err);
        }
        if (FAILED(hr) && ent != entry) { // if the wrapper does not compile, fall back to the original entry
            // This fallback used to be SILENT: without the log there was no way to know
            // that fog and alpha test were not being applied in that pass.
            static LONG nf = 0; if (InterlockedIncrement(&nf) <= 12)
                Log("[fx] wrapper for %s does NOT compile, no fog and no alpha test: %.200s\n",
                    entry.c_str(), err ? (char*)err->GetBufferPointer() : "?");
            if (err) { err->Release(); err = nullptr; }
            hr = D3DCompile(hlsl.data(), hlsl.size(), "fx", nullptr, nullptr, entry.c_str(), target, flags, 0, &code, &err);
        }
        if (FAILED(hr)) { Log("[fx] compile %s FAIL: %.300s\n", entry.c_str(), err ? (char*)err->GetBufferPointer() : "?"); if (err) err->Release(); if (code) code->Release(); return false; }
        if (err) err->Release();
        ID3D11Device* dev = NE_Dev();
        if (isVS) { dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &sp.vs); sp.vsBlob = code; }
        else { dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &sp.ps); }
        // reflection: $Globals + textures
        ID3D11ShaderReflection* refl = nullptr;
        if (SUCCEEDED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&refl)) && refl) {
            D3D11_SHADER_DESC sd{}; refl->GetDesc(&sd);
            if (ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("$Globals")) {
                D3D11_SHADER_BUFFER_DESC bd{};
                if (SUCCEEDED(cb->GetDesc(&bd)) && bd.Size > 0) {
                    std::vector<VarSlot>& vars = isVS ? sp.vsVars : sp.psVars;
                    UINT sz0 = (bd.Size + 15) & ~15u;
                    std::vector<BYTE> defs(sz0, 0);
                    for (UINT v = 0; v < bd.Variables; ++v) {
                        ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                        D3D11_SHADER_VARIABLE_DESC vd{}; var->GetDesc(&vd);
                        vars.push_back({ vd.Name, vd.StartOffset, vd.Size });
                        // the .fx carries defaults (g_SpecularShiness=16, g_BumpDepth=0.75, ...)
                        // that the D3DX framework applies; without them pow(x,0)=1 -> blown-out specular.
                        if (vd.DefaultValue && vd.StartOffset + vd.Size <= sz0) memcpy(&defs[vd.StartOffset], vd.DefaultValue, vd.Size);
                    }
                    UINT sz = sz0;
                    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sz; cbd.Usage = D3D11_USAGE_DYNAMIC;
                    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    ID3D11Buffer* buf = nullptr; dev->CreateBuffer(&cbd, nullptr, &buf);
                    if (isVS) { sp.vsCB = buf; sp.vsCBsize = sz; sp.vsShadow = defs; }
                    else { sp.psCB = buf; sp.psCBsize = sz; sp.psShadow = defs; }
                }
            }
            if (!isVS) {
                for (UINT r = 0; r < sd.BoundResources; ++r) {
                    D3D11_SHADER_INPUT_BIND_DESC rbd{}; refl->GetResourceBindingDesc(r, &rbd);
                    if (rbd.Type == D3D_SIT_TEXTURE) sp.psTex.push_back({ rbd.Name, rbd.BindPoint });
                }
            }
            refl->Release();
        }
        if (isVS && sp.vsBlob != code) { /* keep code as vsBlob */ }
        if (!isVS) code->Release();
        return true;
    }
    ShaderProg* GetProg(Pass& p) {
        if (p.prog) return p.prog;
        ShaderProg* sp = new ShaderProg(); sp->built = true; sp->vsName = p.vsEntry;
        bool a = CompileOne(p.vsEntry, "vs_4_0", *sp, true);
        // PixelShader = null in the .fx: in D3D9 that leaves the pixel to the
        // fixed-function pipeline (texture stages), it does not mean "do not draw".
        if (p.psEntry.empty()) {
            sp->psIsFF = true; sp->ps = NE_FixedFuncPS();
            sp->ok = a && sp->vs && sp->ps;
        } else {
            bool b = CompileOne(p.psEntry, "ps_4_0", *sp, false, p.vsEntry);
            sp->ok = a && b && sp->vs && sp->ps;
        }
        Log("[fx] GetProg vs=%s ps=%s ok=%d\n", p.vsEntry.c_str(), p.psEntry.c_str(), sp->ok);
        p.prog = sp; return sp;
    }
    // the D3D11 context is not thread-safe (see CtxLock in the backend); the effect
    // is used from the render thread, but the compiles can come from loading.
    void UploadCB(ID3D11Buffer* cb, std::vector<BYTE>& shadow, std::vector<VarSlot>& vars) {
        if (!cb) return;
        for (auto& v : vars) {
            if (v.name == "g_NE_AlphaRef") { float r = NE_AlphaRef(); if (v.off + 4 <= shadow.size()) memcpy(&shadow[v.off], &r, 4); continue; }
            // g_NE_Fog / g_NE_FogColor are no longer here: they live in their own
            // cbuffer at b1, refilled per draw by the backend.
            auto it = params.find(v.name);
            if (it != params.end()) { UINT n = v.size < it->second.size() ? v.size : (UINT)it->second.size(); if (v.off + n <= shadow.size()) memcpy(&shadow[v.off], it->second.data(), n); }
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(NE_Ctx()->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, shadow.data(), shadow.size()); NE_Ctx()->Unmap(cb, 0); }
    }
    void BindPass(ShaderProg* sp) {
        ID3D11DeviceContext* ctx = NE_Ctx(); if (!ctx || !sp || !sp->ok) return;
        if (sp->psIsFF) {   // PixelShader = null -> the texture stages resolve it
            { static std::set<std::string> seen;
              if (seen.size() < 20 && seen.insert(sp->vsName).second)
                  Log("[fx] fixed-function pass: vs=%s\n", sp->vsName.c_str()); }
            UploadCB(sp->vsCB, sp->vsShadow, sp->vsVars);
            if (sp->vsCB) ctx->VSSetConstantBuffers(0, 1, &sp->vsCB);
            NE_BindFixedFuncPS();
            NE_SetProgram(sp->vs, sp->ps, sp->vsBlob ? sp->vsBlob->GetBufferPointer() : nullptr,
                          sp->vsBlob ? sp->vsBlob->GetBufferSize() : 0);
            return;
        }
        UploadCB(sp->vsCB, sp->vsShadow, sp->vsVars);
        UploadCB(sp->psCB, sp->psShadow, sp->psVars);
        if (sp->vsCB) ctx->VSSetConstantBuffers(0, 1, &sp->vsCB);
        if (sp->psCB) ctx->PSSetConstantBuffers(0, 1, &sp->psCB);
        // The effect materials in the .scn are "alphablend2" = additive, so filtering
        // by additive isolates exactly the effect draws (weapon trails, the wall jump
        // wave) from the world geometry. Keyed by shader, not by a call counter, so it
        // still fires long after startup.
        bool logIt = false;
        if (NE_IsAdditive()) {
            static std::set<std::string> addSeen;
            if (addSeen.size() < 40 && addSeen.insert(sp->vsName).second) logIt = true;
        }
        std::string dbg;
        for (auto& tb : sp->psTex) {
            auto sit = samplerToTex.find(tb.name);
            const std::string& texName = sit != samplerToTex.end() ? sit->second : tb.name;
            auto it = texParams.find(texName);
            ID3D11ShaderResourceView* srv = (it != texParams.end() && it->second) ? NE_SRV(it->second) : nullptr;
            if (!srv) srv = NE_DeviceTexSRV(tb.slot); // UI/2D sets the texture through device->SetTexture
            if (!srv) {
                // In D3D9 the sampler index IS the stage: the game binds the light ramp
                // with SetTexture(stage 1) (confirmed with the spy DLL on the clean
                // client: "SetTexture stage=1 ... 256x1"). But when transpiling to SM4,
                // D3DCompile numbers the registers according to ALL the samplers in the
                // .fx (Diffuse, LightMap, Bump, Specular, Shade -> s0..s4), so
                // ShadeMapSampler lands on slot 4 and there is nothing there.
                // Result: we fell back to gray 0.5, that is, a CONSTANT instead of the
                // gradient, and all the geometry ended up equally lit.
                const std::string n2 = tb.name + texName;
                // The LATCHED ramp, not whatever is on stage 1 right now. Stage 1 is
                // shared mutable state: an effect binding its sprite there turned that
                // sprite into the world's light ramp for a frame, and the map's lighting
                // jumped -- the flicker seen while using weapons.
                // NE_NO_LIGHTS=1 skips the ramp entirely: Shade/Light samplers fall to the
                // neutral gray below, i.e. flat lighting. Isolation switch for the flicker
                // and the heap crash: if either survives with this on, the ramp path is
                // innocent.
                static int noLights = -1;
                if (noLights < 0) { char e[8] = ""; noLights = (GetEnvironmentVariableA("NE_NO_LIGHTS", e, sizeof(e)) && e[0] == '1') ? 1 : 0; }
                if (!noLights && (n2.find("Shade") != std::string::npos || n2.find("Light") != std::string::npos))
                    srv = NE_ShadeRampSRV();
            }
            if (logIt) { dbg += tb.name; dbg += "->"; dbg += texName; dbg += srv ? "=Y " : "=N "; }
            if (!srv) {
                // which textures do NOT resolve: those are the ones that show up as the white square.
                static std::set<std::string> missing;
                if (missing.size() < 60 && missing.insert(texName).second)
                    Log("[fx] UNRESOLVED texture: sampler=%s tex=%s slot=%u\n", tb.name.c_str(), texName.c_str(), tb.slot);
                // The fallback has to be the NEUTRAL value for how the texture is USED:
                //  - Blur/Mask: its alpha is the mask (FinalColor.a = tex.a * g_BlurFactor);
                //    with white (alpha=1) the blur goes full strength and darkens too much -> alpha 0.
                //  - Specular/additive/shadow: they are ADDED -> black (add zero).
                //  - LightMap/ShadeMap: MODULATE2X (color*map*2) -> gray 0.5.
                //  - Diffuse: it is MULTIPLIED -> white.
                const std::string n = tb.name + texName;
                auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
                // FullScene/Refraction effects use g_TexDiffuseMap for the scene even
                // though the variable name does not contain "Scene".
                if (has("Scene") || has("Screen") || has("Refraction")) {
                    srv = NE_SceneSRV();
                }
                if (srv) { /* the real scene */ }
                else if (has("Bump") || has("Normal")) srv = normalSRV;
                else if (has("Blur") || has("Mask") || has("Specular") || has("Shadow") || NE_IsAdditive()) srv = blackSRV;
                else if (has("Light") || has("Shade")) srv = graySRV;
                else {
                    // White is the fallback that PAINTS: on a billboard it fills the
                    // whole quad and you get the square framing the jump wave and the
                    // weapon effects. Name the pass so we know which one to chase.
                    srv = whiteSRV;
                    static std::set<std::string> painted;
                    if (painted.size() < 30 && painted.insert(sp->vsName + "|" + tb.name).second)
                        Log("[fx] WHITE fallback (paints the quad): vs=%s sampler=%s tex=%s slot=%u\n",
                            sp->vsName.c_str(), tb.name.c_str(), texName.c_str(), tb.slot);
                }
            }
            ctx->PSSetShaderResources(tb.slot, 1, &srv);
        }
        if (logIt) {
            // Which constants actually arrived. g_matTexture is the one that matters
            // for the refraction shader: tex2Dproj divides by ScreenMapUV.w, and that
            // comes from mul(InPosition, g_matTexture). If the matrix is zero the
            // divide is by zero and the whole quad samples garbage.
            std::string vd;
            for (auto& v : sp->vsVars) { vd += v.name; vd += params.count(v.name) ? "=Y " : "=N "; }
            for (auto& v : sp->psVars) {
                vd += v.name;
                auto pit = params.find(v.name);
                if (pit == params.end() || pit->second.size() < 4) { vd += "=N "; continue; }
                float fv = 0.f; memcpy(&fv, pit->second.data(), 4);
                char nb[48]; wsprintfA(nb, "=%d/1000 ", (int)(fv * 1000.0f));
                vd += nb;
            }
            Log("[add] vs=%s tex[%s]\n[add]   vars: %s\n", sp->vsName.c_str(), dbg.c_str(), vd.c_str());
        }
        // One sampler per slot according to what the .fx declares: the ones that ask
        // for CLAMP (tex2Dproj reflection, ShadeMap) stripe if sampled with WRAP.
        if (samp) {
            for (auto& tb : sp->psTex) {
                auto cit = samplerClamp.find(tb.name);
                ID3D11SamplerState* s = (cit != samplerClamp.end() && cit->second && sampClamp) ? sampClamp : samp;
                ctx->PSSetSamplers(tb.slot, 1, &s);
            }
            if (sp->psTex.empty()) { ID3D11SamplerState* s = samp; ctx->PSSetSamplers(0, 1, &s); }
        }
        const void* bc = sp->vsBlob ? sp->vsBlob->GetBufferPointer() : nullptr;
        SIZE_T bl = sp->vsBlob ? sp->vsBlob->GetBufferSize() : 0;
        NE_SetProgram(sp->vs, sp->ps, bc, bl);
    }

    // stores a value by name
    void PutVal(D3DXHANDLE h, const void* data, UINT bytes) {
        if (!h) return;
        auto& v = params[(const char*)h]; v.resize(bytes); if (data) memcpy(v.data(), data, bytes);
    }

    // ===== IUnknown =====
    STDMETHOD(QueryInterface)(REFIID, void** ppv) { *ppv = this; AddRef(); return S_OK; }
    STDMETHOD_(ULONG, AddRef)() { return InterlockedIncrement(&ref); }
    STDMETHOD_(ULONG, Release)() { LONG r = InterlockedDecrement(&ref); if (r == 0) delete this; return r; }

    // ===== ID3DXBaseEffect =====
    // The engine DISCOVERS parameters by enumeration, not only by name: it walks
    // GetParameter(NULL, i) and caches the handles (the client keeps 15 of them at
    // this+0x84 and skips any that come back null). We reported zero parameters and
    // returned null for every index, so the engine cached nothing and never called the
    // setters -- g_vEyePos, for one, stayed at zero forever. That leaves
    // V = normalize(g_vEyePos - tmpPos) pointing at the world origin instead of the
    // camera, so the view-dependent terms are wrong and swing with where you look.
    // Same shape as the white-character bug: something the shader reads that nothing fills.
    std::vector<std::string> paramNames;
    void CollectParamNames() {
        static const char* kT[] = { "float4x4", "float4", "float3", "float2", "float",
                                    "matrix", "texture", "int", "bool", "half" };
        std::vector<int> depth(hlsl.size() + 1, 0);
        { int d = 0;
          for (size_t i = 0; i < hlsl.size(); ++i) {
              if (hlsl[i] == '{') { depth[i] = d; d++; }
              else if (hlsl[i] == '}') { if (d > 0) d--; depth[i] = d; }
              else depth[i] = d;
          } }
        std::set<std::string> seen;
        for (const char* ty : kT) {
            size_t tlen = strlen(ty), p = 0;
            while ((p = hlsl.find(ty, p)) != std::string::npos) {
                size_t q = p + tlen;
                if (depth[p] != 0) { p = q; continue; }
                if (p > 0 && (isalnum((unsigned char)hlsl[p - 1]) || hlsl[p - 1] == '_')) { p = q; continue; }
                if (q < hlsl.size() && (isalnum((unsigned char)hlsl[q]) || hlsl[q] == '_')) { p = q; continue; }
                while (q < hlsl.size() && isspace((unsigned char)hlsl[q])) q++;
                size_t ns = q;
                while (q < hlsl.size() && (isalnum((unsigned char)hlsl[q]) || hlsl[q] == '_')) q++;
                std::string nm = hlsl.substr(ns, q - ns);
                // a declaration ends in ; = or [ ; anything else is a cast or a local
                size_t r = hlsl.find_first_not_of(" \t\r\n", q);
                bool decl = (r != std::string::npos) && (hlsl[r] == ';' || hlsl[r] == '=' || hlsl[r] == '[');
                if (decl && !nm.empty() && nm.compare(0, 2, "g_") == 0 && seen.insert(nm).second)
                    paramNames.push_back(nm);
                p = ns + 1;
            }
        }
    }
    STDMETHOD(GetDesc)(D3DXEFFECT_DESC* d) { if (d) { d->Creator = "NativeEngine"; d->Parameters = 0; d->Techniques = (UINT)techs.size(); d->Functions = 0; } return D3D_OK; }
    STDMETHOD(GetParameterDesc)(D3DXHANDLE h, D3DXPARAMETER_DESC* d) {
        if (!d) return D3D_OK; ZeroMemory(d, sizeof(*d));
        d->Name = h ? (LPCSTR)h : "param"; d->Semantic = ""; // never null: the game does wstring(name)
        d->Class = D3DXPC_VECTOR; d->Type = D3DXPT_FLOAT; d->Rows = 1; d->Columns = 4; d->Bytes = 16;
        return D3D_OK;
    }
    STDMETHOD(GetTechniqueDesc)(D3DXHANDLE h, D3DXTECHNIQUE_DESC* d) {
        if (!d) return D3D_OK; ZeroMemory(d, sizeof(*d));
        d->Name = h ? (LPCSTR)h : (techs.empty() ? "T0" : Intern(techs[0].name.c_str()));
        d->Passes = 1;
        for (auto& t : techs) if (h && t.name == (const char*)h) d->Passes = (UINT)t.passes.size();
        return D3D_OK;
    }
    STDMETHOD(GetPassDesc)(D3DXHANDLE h, D3DXPASS_DESC* d) { if (!d) return D3D_OK; ZeroMemory(d, sizeof(*d)); d->Name = h ? (LPCSTR)h : "p0"; return D3D_OK; }
    STDMETHOD(GetFunctionDesc)(D3DXHANDLE h, D3DXFUNCTION_DESC* d) { if (!d) return D3D_OK; ZeroMemory(d, sizeof(*d)); d->Name = h ? (LPCSTR)h : "f0"; return D3D_OK; }
    // Reverted: enumerating parameters made the engine set more of them and that FLIPPED
    // which camera angle shows the white quad (from "up is fine" to "down is fine"). Kept
    // off until the quad itself is understood, so the known-better state is the one shipped.
    STDMETHOD_(D3DXHANDLE, GetParameter)(D3DXHANDLE, UINT) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetParameterByName)(D3DXHANDLE, LPCSTR name) { return Intern(name); }
    STDMETHOD_(D3DXHANDLE, GetParameterBySemantic)(D3DXHANDLE, LPCSTR) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetParameterElement)(D3DXHANDLE, UINT) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetTechnique)(UINT i) { return i < techs.size() ? Intern(techs[i].name.c_str()) : nullptr; }
    STDMETHOD_(D3DXHANDLE, GetTechniqueByName)(LPCSTR name) { return Intern(name); }
    STDMETHOD_(D3DXHANDLE, GetPass)(D3DXHANDLE, UINT) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetPassByName)(D3DXHANDLE, LPCSTR name) { return Intern(name); }
    STDMETHOD_(D3DXHANDLE, GetFunction)(UINT) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetFunctionByName)(LPCSTR) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetAnnotation)(D3DXHANDLE, UINT) { return nullptr; }
    STDMETHOD_(D3DXHANDLE, GetAnnotationByName)(D3DXHANDLE, LPCSTR) { return nullptr; }
    STDMETHOD(SetValue)(D3DXHANDLE h, LPCVOID d, UINT b) { PutVal(h, d, b); return D3D_OK; }
    STDMETHOD(GetValue)(D3DXHANDLE, LPVOID, UINT) { return D3D_OK; }
    STDMETHOD(SetBool)(D3DXHANDLE h, BOOL b) { PutVal(h, &b, 4); return D3D_OK; }
    STDMETHOD(GetBool)(D3DXHANDLE, BOOL*) { return D3D_OK; }
    STDMETHOD(SetBoolArray)(D3DXHANDLE h, CONST BOOL* b, UINT c) { PutVal(h, b, c * 4); return D3D_OK; }
    STDMETHOD(GetBoolArray)(D3DXHANDLE, BOOL*, UINT) { return D3D_OK; }
    STDMETHOD(SetInt)(D3DXHANDLE h, INT n) { PutVal(h, &n, 4); return D3D_OK; }
    STDMETHOD(GetInt)(D3DXHANDLE, INT*) { return D3D_OK; }
    STDMETHOD(SetIntArray)(D3DXHANDLE h, CONST INT* n, UINT c) { PutVal(h, n, c * 4); return D3D_OK; }
    STDMETHOD(GetIntArray)(D3DXHANDLE, INT*, UINT) { return D3D_OK; }
    STDMETHOD(SetFloat)(D3DXHANDLE h, FLOAT f) { PutVal(h, &f, 4); return D3D_OK; }
    STDMETHOD(GetFloat)(D3DXHANDLE, FLOAT*) { return D3D_OK; }
    STDMETHOD(SetFloatArray)(D3DXHANDLE h, CONST FLOAT* f, UINT c) { PutVal(h, f, c * 4); return D3D_OK; }
    STDMETHOD(GetFloatArray)(D3DXHANDLE, FLOAT*, UINT) { return D3D_OK; }
    STDMETHOD(SetVector)(D3DXHANDLE h, CONST D3DXVECTOR4* v) { PutVal(h, v, 16); return D3D_OK; }
    STDMETHOD(GetVector)(D3DXHANDLE, D3DXVECTOR4*) { return D3D_OK; }
    STDMETHOD(SetVectorArray)(D3DXHANDLE h, CONST D3DXVECTOR4* v, UINT c) { PutVal(h, v, c * 16); return D3D_OK; }
    STDMETHOD(GetVectorArray)(D3DXHANDLE, D3DXVECTOR4*, UINT) { return D3D_OK; }
    STDMETHOD(SetMatrix)(D3DXHANDLE h, CONST D3DXMATRIX* m) { PutVal(h, m, 64); return D3D_OK; }
    STDMETHOD(GetMatrix)(D3DXHANDLE, D3DXMATRIX*) { return D3D_OK; }
    STDMETHOD(SetMatrixArray)(D3DXHANDLE h, CONST D3DXMATRIX* m, UINT c) { PutVal(h, m, c * 64); return D3D_OK; }
    STDMETHOD(GetMatrixArray)(D3DXHANDLE, D3DXMATRIX*, UINT) { return D3D_OK; }
    STDMETHOD(SetMatrixPointerArray)(D3DXHANDLE, CONST D3DXMATRIX**, UINT) { return D3D_OK; }
    STDMETHOD(GetMatrixPointerArray)(D3DXHANDLE, D3DXMATRIX**, UINT) { return D3D_OK; }
    STDMETHOD(SetMatrixTranspose)(D3DXHANDLE h, CONST D3DXMATRIX* m) { PutVal(h, m, 64); return D3D_OK; }
    STDMETHOD(GetMatrixTranspose)(D3DXHANDLE, D3DXMATRIX*) { return D3D_OK; }
    STDMETHOD(SetMatrixTransposeArray)(D3DXHANDLE h, CONST D3DXMATRIX* m, UINT c) { PutVal(h, m, c * 64); return D3D_OK; }
    STDMETHOD(GetMatrixTransposeArray)(D3DXHANDLE, D3DXMATRIX*, UINT) { return D3D_OK; }
    STDMETHOD(SetMatrixTransposePointerArray)(D3DXHANDLE, CONST D3DXMATRIX**, UINT) { return D3D_OK; }
    STDMETHOD(GetMatrixTransposePointerArray)(D3DXHANDLE, D3DXMATRIX**, UINT) { return D3D_OK; }
    STDMETHOD(SetString)(D3DXHANDLE, LPCSTR) { return D3D_OK; }
    STDMETHOD(GetString)(D3DXHANDLE, LPCSTR* pp) { if (pp) *pp = "default"; return D3D_OK; } // never empty: the game does wstring(s).back()
    // Which textures the game actually SETS, and with which pointer. It tells us
    // whether g_TexShadeMap fails to resolve because the game never sends it (a
    // resource loading problem) or because we bind it wrong (a binding problem).
    STDMETHOD(SetTexture)(D3DXHANDLE h, LPDIRECT3DBASETEXTURE9 t) {
        if (!h) return D3D_OK;
        texParams[(const char*)h] = t;
        static std::set<std::string> seenTex;
        if (seenTex.size() < 40 && seenTex.insert((const char*)h).second)
            Log("[fx] the game sets %s = %p\n", (const char*)h, t);
        return D3D_OK;
    }
    STDMETHOD(GetTexture)(D3DXHANDLE, LPDIRECT3DBASETEXTURE9* t) { if (t) *t = nullptr; return D3D_OK; }
    STDMETHOD(GetPixelShader)(D3DXHANDLE, LPDIRECT3DPIXELSHADER9* p) { if (p) *p = nullptr; return D3D_OK; }
    STDMETHOD(GetVertexShader)(D3DXHANDLE, LPDIRECT3DVERTEXSHADER9* p) { if (p) *p = nullptr; return D3D_OK; }
    STDMETHOD(SetArrayRange)(D3DXHANDLE, UINT, UINT) { return D3D_OK; }

    // ===== ID3DXEffect =====
    STDMETHOD(GetPool)(LPD3DXEFFECTPOOL* p) { if (p) *p = nullptr; return D3D_OK; }
    STDMETHOD(SetTechnique)(D3DXHANDLE h) {
        // The engine picks techniques BY NAME (it caches ~60 handles with
        // GetTechniqueByName at startup, see sub_11D6910 in the client). If the name does
        // not match anything we parsed we used to silently fall back to technique 0 and
        // still return D3D_OK, so the game would draw with a completely different shader
        // and never know. Different parts of one weapon use different techniques, which is
        // exactly how an effect ends up "half fixed".
        int found = -1;
        if (h) for (size_t i = 0; i < techs.size(); ++i) if (techs[i].name == (const char*)h) { found = (int)i; break; }
        if (found < 0 && h) {
            static std::set<std::string> missed;
            if (missed.size() < 40 && missed.insert((const char*)h).second)
                Log("[fx] SetTechnique NO MATCH: \"%s\" (parsed=%d) -> falls back to technique 0\n",
                    (const char*)h, (int)techs.size());
        }
        curTech = found >= 0 ? found : 0;
        return D3D_OK;
    }
    STDMETHOD_(D3DXHANDLE, GetCurrentTechnique)() { return (curTech >= 0 && curTech < (int)techs.size()) ? Intern(techs[curTech].name.c_str()) : nullptr; }
    STDMETHOD(ValidateTechnique)(D3DXHANDLE) { return D3D_OK; }
    STDMETHOD(FindNextValidTechnique)(D3DXHANDLE, D3DXHANDLE* p) { if (p) *p = techs.empty() ? nullptr : Intern(techs[0].name.c_str()); return D3D_OK; }
    // Body of an entry point in the .fx source, so we can tell whether it mentions a
    // parameter. Returns an empty string when the function is not found.
    std::string FuncBody(const std::string& entry) {
        if (entry.empty()) return std::string();
        size_t p = 0;
        while ((p = FindWord(hlsl, entry, p)) != std::string::npos) {
            size_t after = p + entry.size();
            size_t op = hlsl.find_first_not_of(" \t\r\n", after);
            // the definition is "<entry> (" ; a call site is preceded by "= compile ..."
            if (op == std::string::npos || hlsl[op] != '(') { p = after; continue; }
            size_t br = hlsl.find('{', op);
            if (br == std::string::npos) return std::string();
            int depth = 0;
            for (size_t i = br; i < hlsl.size(); ++i) {
                if (hlsl[i] == '{') depth++;
                else if (hlsl[i] == '}' && --depth == 0) return hlsl.substr(br, i - br + 1);
            }
            return std::string();
        }
        return std::string();
    }
    // The engine builds a 29x15 "does technique T use parameter P" table at load time
    // (sub_11D6910 in the client calls this for every pair) and drives its technique
    // choice from it. Returning TRUE for everything made all 435 cells true, so the
    // engine could not tell the techniques apart and picked the wrong shader for some
    // effects -- which is why a weapon could come out half right.
    //
    // A texture parameter is used INDIRECTLY: the shader body names the sampler, and the
    // sampler declares "Texture = <g_TexWhatever>". So the aliases of a parameter are the
    // parameter itself plus every sampler bound to it.
    STDMETHOD_(BOOL, IsParameterUsed)(D3DXHANDLE hParam, D3DXHANDLE hTech) {
        if (!hParam) return FALSE;
        if (!hTech) return TRUE;   // no technique given: cannot rule it out
        const std::string param = (const char*)hParam;
        const std::string tech = (const char*)hTech;
        auto key = tech + "|" + param;
        auto it = usedCache.find(key);
        if (it != usedCache.end()) return it->second ? TRUE : FALSE;

        std::vector<std::string> alias{ param };
        for (auto& sm : samplerToTex) if (sm.second == param) alias.push_back(sm.first);

        bool used = false;
        for (auto& t : techs) {
            if (t.name != tech) continue;
            for (auto& ps : t.passes) {
                std::string body = FuncBody(ps.vsEntry) + FuncBody(ps.psEntry);
                if (body.empty()) { used = true; break; }   // cannot tell -> assume used
                for (auto& a : alias) if (body.find(a) != std::string::npos) { used = true; break; }
                if (used) break;
            }
            break;
        }
        usedCache[key] = used;
        return used ? TRUE : FALSE;
    }
    std::unordered_map<std::string, bool> usedCache;
    STDMETHOD(Begin)(UINT* passes, DWORD) {
        int t = curTech >= 0 ? curTech : 0;
        if (passes) *passes = (t < (int)techs.size()) ? (UINT)techs[t].passes.size() : 0;
        return D3D_OK;
    }
    STDMETHOD(BeginPass)(UINT pass) {
        // Which technique the ENGINE actually asked for, and which entry points we bound
        // for it. If a Light1 technique lands on a shader without DifPow, the lighting term
        // is garbage and the result goes white depending on the angle -- the same mechanism
        // as the white characters (DifPow = 0.5*dot(N,L)+0.5 is angle dependent by
        // definition, which is why that bug moved with the camera).
        {
            int tt = curTech >= 0 ? curTech : 0;
            if (tt < (int)techs.size() && pass < techs[tt].passes.size()) {
                static std::set<std::string> seenPass;
                std::string k = techs[tt].name + "#" + std::to_string(pass);
                if (seenPass.size() < 60 && seenPass.insert(k).second)
                    Log("[tech] %s pass=%u -> vs=%s ps=%s\n", techs[tt].name.c_str(), pass,
                        techs[tt].passes[pass].vsEntry.c_str(),
                        techs[tt].passes[pass].psEntry.empty() ? "(null->fixedfunc)" : techs[tt].passes[pass].psEntry.c_str());
            }
        }
        int t = curTech >= 0 ? curTech : 0;
        if (t >= (int)techs.size() || pass >= techs[t].passes.size()) return D3D_OK;
        // projected shadows can be drawn now: they are passes with PixelShader = null
        // and we now run the fixed-function pixel pipeline.
        // The post effects (FullSceneGlow/Blur/Haze/Refraction) DO run now: we implement
        // render targets (a real SetRenderTarget), so they sample the actual scene.
        (void)isPostFx;
        // Apply the pass render states BEFORE binding, exactly like D3DX does. Without
        // this the pass inherits the device state: the refraction pass asks for
        // AlphaBlendEnable = false and was being drawn additive, which is what burned
        // those effects to white.
        {
            Pass& p = techs[t].passes[pass];
            // Hand the pass states to the backend as an override for this pass's draws.
            // Nothing is written to the device, so nothing has to be restored and
            // nothing leaks into what is drawn afterwards.
            DWORD st_[16], sv_[16]; int sn = 0;
            for (const auto& st : p.states) { if (sn >= 16) break; st_[sn] = st.state; sv_[sn] = st.value; sn++; }
            NE_SetPassStates(st_, sv_, sn);
            static std::set<std::string> stSeen;
            if (!p.states.empty() && stSeen.size() < 20 && stSeen.insert(p.vsEntry).second) {
                std::string s;
                for (const auto& st : p.states) { char b[32]; wsprintfA(b, "%lu=%lu ", st.state, st.value); s += b; }
                Log("[pass] %s states: %s\n", p.vsEntry.c_str(), s.c_str());
            }
        }
        ShaderProg* sp = GetProg(techs[t].passes[pass]);
        BindPass(sp);
        return D3D_OK;
    }
    STDMETHOD(CommitChanges)() {
        int t = curTech >= 0 ? curTech : 0;
        if (t < (int)techs.size()) for (auto& p : techs[t].passes) if (p.prog && p.prog->ok) { BindPass(p.prog); break; }
        return D3D_OK;
    }
    // NE_ClearProgram also drops the pass state override, so there is nothing to undo.
    STDMETHOD(EndPass)() { NE_ClearProgram(); return D3D_OK; }
    STDMETHOD(End)() { NE_ClearProgram(); return D3D_OK; }
    STDMETHOD(GetDevice)(LPDIRECT3DDEVICE9* d) { if (d) *d = nullptr; return D3D_OK; }
    STDMETHOD(OnLostDevice)() { return D3D_OK; }
    STDMETHOD(OnResetDevice)() { return D3D_OK; }
    STDMETHOD(SetStateManager)(LPD3DXEFFECTSTATEMANAGER) { return D3D_OK; }
    STDMETHOD(GetStateManager)(LPD3DXEFFECTSTATEMANAGER* p) { if (p) *p = nullptr; return D3D_OK; }
    STDMETHOD(BeginParameterBlock)() { return D3D_OK; }
    STDMETHOD_(D3DXHANDLE, EndParameterBlock)() { return nullptr; }
    STDMETHOD(ApplyParameterBlock)(D3DXHANDLE) { return D3D_OK; }
    STDMETHOD(DeleteParameterBlock)(D3DXHANDLE) { return D3D_OK; }
    STDMETHOD(CloneEffect)(LPDIRECT3DDEVICE9, LPD3DXEFFECT* p) { if (p) { AddRef(); *p = this; } return D3D_OK; }
    STDMETHOD(SetRawValue)(D3DXHANDLE h, LPCVOID d, UINT off, UINT b) {
        // NOTE: do not log here. This is the call the game uses to feed every
        // constant, thousands of times per frame and from more than one thread; a
        // static std::set in this path races and kills the client before the first
        // frame. The [add] line already reports which constants arrived (=Y/=N).
        if (!h) return D3D_OK;
        auto& v = params[(const char*)h]; if (v.size() < off + b) v.resize(off + b); if (d) memcpy(&v[off], d, b); return D3D_OK;
    }
};

typedef HRESULT(WINAPI* PFN_D3DXCreateEffect)(void*, const void*, UINT, const void*, void*, DWORD, void*, void**, void**);
static PFN_D3DXCreateEffect g_realCreateEffect = nullptr;

static HRESULT WINAPI Hook_D3DXCreateEffect(void* dev, const void* src, UINT len, const void* defines,
    void* inc, DWORD flags, void* pool, void** effect, void** errors) {
    if (effect && NE_Dev()) {
        *effect = (void*)static_cast<ID3DXEffect*>(new MyEffect((const char*)src, len));
        if (errors) *errors = nullptr;
        return S_OK;
    }
    if (g_realCreateEffect) return g_realCreateEffect(dev, src, len, defines, inc, flags, pool, effect, errors);
    if (effect) *effect = nullptr;
    return E_FAIL;
}

void InstallEffectsHook() {
    HMODULE host = GetModuleHandleA(nullptr);
    void* old = PatchIAT(host, "d3dx9_29.dll", "D3DXCreateEffect", (void*)&Hook_D3DXCreateEffect);
    g_realCreateEffect = (PFN_D3DXCreateEffect)old;
    Log("[fx] InstallEffectsHook (NATIVE) patched=%d real=%p\n", old != nullptr, old);
}

} // namespace ne
