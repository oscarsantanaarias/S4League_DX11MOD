# S4League DX11 MOD

> **Work in progress.** The client runs and is playable at a good frame rate, but there
> are open visual issues — see [Known issues](#known-issues) and `QUEUE.md`.

A **native Direct3D 11 renderer** for the S4 League client. Not a shim (9on12), not a
third-party wrapper (DXVK, dgVoodoo): the DLL implements `IDirect3D9`/
`IDirect3DDevice9` on top of a real D3D11 device, and reimplements `ID3DXEffect` to
transpile the game's `fx_2_0` shaders to SM4/5 at runtime.

The window title says it plainly while the game runs:

```
S4 Client  |  Direct3D 11 (FL 11_0) NativeEngine  |  <your GPU>
```

## How it works

The game calls `Direct3DCreate9` and gets our object back, hooked through the IAT. From
there everything is real D3D11:

- **Resources** — vertex/index buffers and textures are D3D11 resources, with the real
  format (DXT1/3/5 -> BC1/2/3, A8, L8/A8L8 converted, 24-bit expanded). Textures upload
  through `GetSurfaceLevel`, which is the path the client actually uses.
- **Draws** — the input layout is built from the FVF (the game uses `SetFVF`, not vertex
  declarations, for most geometry) or from the vertex declaration when there is one.
- **Effects** — `D3DXCreateEffect` is intercepted and returns our own `ID3DXEffect`. It
  parses the techniques and passes, compiles each entry point to SM4 with `D3DCompile`
  (backwards compatibility + row-major), maps `$Globals` to a cbuffer by reflection, and
  binds shaders, constants and textures per pass. Without this the 3D is not drawn at
  all, because the client draws *inside* the effect passes.
- **Fixed function** — the texture stage cascade (`COLOROP`/`ALPHAOP` with
  `SELECTARG`/`MODULATE`/`ADD`...), alpha test and fog are emulated in a default shader,
  since D3D11 has none of them.

## Layout

```
src/
  dllmain.cpp          installs the hooks on load
  backend_d3d11.cpp    IDirect3D9/IDirect3DDevice9 on real D3D11
  effects.cpp          D3DXCreateEffect hook + our ID3DXEffect
  fx_transpiler.*      fx_2_0 -> SM4/5
  guards.cpp           targeted mitigations for the client's own crashes
  s4_base.h            S4(va): rebases the patch addresses at runtime
spy/                   logging-only D3D9 spy, to capture ground truth from a clean client
tools/                 offline harness to iterate the shader transpile
```

`ARCHITECTURE.md` explains the design, `API_1TO1_AUDIT.md` records which D3D9 methods
are implemented and which are still stubs, and `QUEUE.md` is the list of known visual
issues with what has already been ruled out for each.

## Building

Visual Studio, Win32 (32-bit, the client is 32-bit), toolset v145. Open
`NativeEngine.sln` and build Release. The output goes next to the client.

## Known issues

All of these are work in progress. None of them breaks the client.

| Issue | State |
|---|---|
| **Some weapon animations render a white square instead of the charge effect** — the basic (non-skin) Mind Shock and Mind Heal, and the railgun charge | open — it is **camera-position dependent**, not map dependent: on the same map, moving the camera turns the correct blue halo into a white block. The glow is rendered into an offscreen buffer cleared to white and the sprite lands off-centre inside it, so the compositing quad shows the clear colour instead of the glow |
| **Fog is not visible** | open — the shader wrapper compiles and the constants are fed per draw, but nothing shows up |
| **The map flickers with many players on screen** | open — not diagnosed yet |

Fixed: the white square on the wall jump, the dagger and the plasma sword. Those go
through the refraction shader, whose pass asks for `AlphaBlendEnable = false` because
it returns the scene to replace the background; inheriting the surrounding additive
blend summed the scene over itself and burned it to white. The fix was to apply the
render states a `.fx` pass declares, which we were ignoring.

`QUEUE.md` lists, for each open issue, what has already been measured and ruled out, so
the same ground does not get covered twice.

## Notes

Patch addresses were reversed against the client whose ImageBase is `0x00E80000`. An
unpacked client keeps the same code at a different base, so every hardcoded address
goes through the `S4()` macro and is rebased at runtime from `GetModuleHandleW(NULL)`.
Each patch also verifies the bytes it expects before writing, and logs instead of
patching when they do not match.
