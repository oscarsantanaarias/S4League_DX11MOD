#pragma once
#include <string>

namespace ne {

// Transpiles the source of a .fx effect (fx_2_0, old HLSL with technique/pass/
// VertexShader=compile vs_1_1 ...) into HLSL that d3dcompiler_47 can compile to
// SM4/5, and exposes the per-pass VS/PS entry points and states.
//
// PHASE 2. This header fixes the contract; the real impl is built incrementally.
struct FxPass {
    std::string vsEntry;   // e.g. "VS_DiffuseMap_LightMap"
    std::string psEntry;   // e.g. "PS_FX"
    std::string vsTarget;  // "vs_4_0" / "vs_5_0"
    std::string psTarget;  // "ps_4_0" / "ps_5_0"
    // TODO(native): pass render states (blend/ztest/cull/sampler) -> D3D11.
};
struct FxTechnique { std::string name; /* passes */ };

struct FxTranspileResult {
    bool ok = false;
    std::string hlsl;                 // the source already adapted to SM4/5
    std::string error;
    // parsed techniques/passes
};

// Converts fx_2_0 -> HLSL SM4/5. For now: passthrough + marked adjustment
// points. See the notes in the .cpp for the actual syntax differences.
FxTranspileResult TranspileFx2ToSm4(const char* src, unsigned len);

} // namespace ne
