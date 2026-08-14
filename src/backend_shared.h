#pragma once
// Integration surface between the effect (effects.cpp) and the D3D11 device
// (backend_d3d11.cpp). The effect compiles and binds shaders + params; the
// device builds the input layout from the game's vertex declaration and draws.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d9.h>

namespace ne {
ID3D11Device* NE_Dev();
ID3D11DeviceContext* NE_Ctx();
// given an IDirect3DBaseTexture9* (which is really our NTexture), its D3D11 SRV.
ID3D11ShaderResourceView* NE_SRV(IDirect3DBaseTexture9* tex9);
// SRV of the texture the game set through device->SetTexture(stage) (UI/2D).
ID3D11ShaderResourceView* NE_DeviceTexSRV(UINT stage);
// true if the current blend is additive (DESTBLEND=ONE): there the neutral value is BLACK, not white.
bool NE_IsAdditive();
// alpha test threshold (D3DRS_ALPHATESTENABLE/ALPHAREF); 0 = no alpha test.
float NE_AlphaRef();
// the effect sets the active "program" (D3D11 VS/PS + the VS bytecode for the
// input layout) before the game calls DrawPrimitive/DrawIndexedPrimitive.
void NE_SetProgram(ID3D11VertexShader* vs, ID3D11PixelShader* ps, const void* vsBytecode, SIZE_T vsLen);
void NE_ClearProgram();
// fixed-function pixel shader for the .fx passes with PixelShader = null
// (projected shadows, TextureNoise); NE_BindFixedFuncPS uploads its state.
ID3D11PixelShader* NE_FixedFuncPS();
void NE_BindFixedFuncPS();
// D3D9 fixed-function fog: fog = (start, end, enable, 0), color = rgba
void NE_FogState(float* fog4, float* color4);
// SCENE texture (512x512) filled by UpdateScreenTexture; the .fx files sample it
// as g_TexSceneMap. Without it the white fallback paints the whole quad.
ID3D11ShaderResourceView* NE_SceneSRV();
// The backbuffer surface, for the render target stack guard: when the engine's
// deque is corrupted we hand this back instead of a null, because PopRenderTarget
// dereferences what front() returns before doing anything with it.
IDirect3DSurface9* NE_BackbufferSurface();
// A .fx pass also sets render states, and D3DX applies them to the device when the
// pass begins. The effect calls this so the pass states actually take effect instead
// of the pass inheriting whatever the device happened to have.
// The states a .fx pass declares. They apply ONLY to that pass's draws and are
// dropped when the program is cleared, so they never leak into the device state and
// never fight with what the game sets in between.
void NE_SetPassStates(const DWORD* states, const DWORD* values, int count);
} // namespace ne
