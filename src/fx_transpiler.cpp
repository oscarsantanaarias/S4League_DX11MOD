// fx_transpiler.cpp -- fx_2_0 (D3DX9 Effects) -> HLSL SM4/5.
//
// PHASE 2 (bulk still pending). The old FX framework (fx_2_0) and modern HLSL
// differ in:
//   - samplers: fx_2_0 uses `sampler S = sampler_state { Texture=<t>; ... };` and
//     `tex2D(S, uv)`. In SM4/5: `Texture2D t; SamplerState s; t.Sample(s,uv)`.
//   - pass states (AlphaBlendEnable, ZEnable, CullMode, SrcBlend...) -> they do
//     not exist in SM4 HLSL; they go to OMSetBlendState/RSSetState/etc. from C++.
//   - technique/pass { VertexShader = compile vs_1_1 VS(); } -> we extract the
//     entry points and compile to vs_4_0/ps_4_0 with D3DCompile.
//   - old semantics (POSITION -> SV_Position on the VS output, COLOR ->
//     SV_Target on the PS), matrices, and a few intrinsics.
//
// Incremental strategy: parse techniques/passes, split the shader HLSL from the
// state block, rewrite samplers/tex2D, and compile per pass. What follows is the
// skeleton plus a safe passthrough so the pipeline compiles and can be filled in
// effect by effect.

#include "fx_transpiler.h"
#include <cstring>

namespace ne {

FxTranspileResult TranspileFx2ToSm4(const char* src, unsigned len) {
    FxTranspileResult r;
    if (!src || !len) { r.error = "empty fx"; return r; }
    // ponytail: passthrough for now. The real transpile (samplers/states/
    // technique parsing) is implemented effect by effect in PHASE 2; see notes above.
    r.hlsl.assign(src, len);
    r.ok = true;
    return r;
}

} // namespace ne
