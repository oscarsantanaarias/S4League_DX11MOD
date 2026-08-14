# NativeEngine — native D3D11 render engine for S4 League

Goal: replace the S4 client's D3D9+D3DXEffect rendering with a **native D3D11
engine** (not a 9on12-style shim, not a third-party wrapper). End goal: native
performance plus unlocking improvements a translator cannot give (multithreaded
submission, better post/shadows, HD assets, removing the D3D9 overhead).

Builds to `sneoz.dll` (the name the S4 injector requires). 32-bit, v145.

## Context of the client's engine (reversing, see the other doc)

- Rendering is centralized in `CRenderer_D3D` (Init=`FUN_01c82620`, exe base 0x00E80000).
- The D3D9 device lives at `CRenderer+0x69c`; `Direct3DCreate9(0x20)` at `01c826c2`.
- **All material runs on `ID3DXEffect` (`D3DXCreateEffect`, D3DX9_29.dll)** with
  text `.fx` HLSL (vs_1_1 / ps_2_0), compiled at runtime. ~130 effects.

## Strategy (why D3D11 and not D3D12)

Native D3D12 by hand = huge (command lists, heaps, barriers, manual PSOs).
D3D11 is **also native** (not a shim), gives the modern GPU improvements, and is
realistic to write by hand. We picked D3D11 as the v1 backend; a D3D12 backend is
a later evolution behind the same `IRenderBackend` interface.

## The 2 fronts of the port

1. **D3D9 API → D3D11** (device, resources, states, draws). We intercept
   `Direct3DCreate9` and hand back our own `IDirect3DDevice9` backed by real D3D11
   (native buffers/textures/states/draws). We present through our own D3D11
   swapchain (DISCARD) — proven to work in the game's window.

2. **D3DXEffect → native effects** (the real WALL). We intercept
   `D3DXCreateEffect` and hand back our own `ID3DXEffect` that:
   - Parses the `.fx` (techniques/passes/params).
   - **Transpiles the fx_2_0 HLSL → SM4/5** (samplers/states/technique change
     syntax between fx_2_0 and fx_4/5) and compiles with `D3DCompile`
     (d3dcompiler_47, ships with Windows).
   - Binds VS/PS/states/constants per pass to our D3D11 device.
   Without this the 3D is not drawn at all (the game draws INSIDE the effect passes).

## Project layout

```
NativeEngine/
  NativeEngine.sln
  ARCHITECTURE.md            (this doc)
  src/
    dllmain.cpp              installs the hooks (IAT) on load
    iat.h                    base-independent IAT patching
    backend_d3d11.h/.cpp     IDirect3D9/IDirect3DDevice9 on top of real D3D11
    resources_d3d11.h        VB/IB/Texture/Surface with real D3D11 resources
    state_map.h              D3DRENDERSTATE/SAMPLER/etc. -> D3D11 state objects
    effects.h/.cpp           D3DXCreateEffect hook + our own ID3DXEffect
    fx_transpiler.h/.cpp     fx_2_0 -> SM4/5 HLSL
    log.h                    file logging
```

## Status

Done:

- IAT hooking, real D3D11 device + swapchain + present.
- Real VB/IB resources; textures in their real format (DXT1/3/5->BC, A8, L8/A8L8
  converted, 24-bit expanded), uploaded through `GetSurfaceLevel`.
- Input layout built from the FVF (or from the vertex declaration when there is one),
  VB/IB binding, topology, draws.
- Our own `ID3DXEffect`: technique/pass parsing, fx_2_0 -> SM4 transpile, `$Globals`
  mapped to a cbuffer by reflection, sampler -> texture mapping, per-pass binding.
- Blend, depth and cull from the render states; alpha test and the fixed-function
  texture stage cascade emulated in the default shader.
- Skinned meshes (bones as 4-float texcoords, honouring `D3DFVF_TEXCOORDSIZE`).
- Render states declared by a `.fx` pass.

Still open: see `QUEUE.md`. A D3D12 backend behind the same interface is a possible
later step.

## Verification

Build, drop the DLL next to the client, run it. The window title reports the API,
feature level and GPU, so a screenshot is enough to confirm what is rendering. The
engine writes a log next to the client with per-frame counters (draws by path, draws
not executed and why, RAM held by the CPU-side texture copies), which is what most of
the debugging in `QUEUE.md` was based on.

`spy/` builds a logging-only D3D9 spy that can be loaded into an unmodified client to
capture what the game really asks D3D9 for, as ground truth to compare against.
