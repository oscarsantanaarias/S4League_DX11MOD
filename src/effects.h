#pragma once
// Hooks D3DXCreateEffect (D3DX9_29.dll) to capture the .fx source of every
// effect in the game (PHASE 2: transpile fx_2_0 -> SM4/5 + our own ID3DXEffect).
namespace ne { void InstallEffectsHook(); }
