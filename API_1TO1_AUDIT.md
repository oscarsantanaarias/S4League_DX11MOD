# D3D9 API: what we implement 1:1 and what we do not

Audit of `NDevice` (our `IDirect3DDevice9`) against what the client actually calls.
177 methods; 69 of them are pure stubs that accept the call and do nothing.

## Draw accounting

Every draw the game issues now either executes or is counted. The `[lost]` line in
the log breaks the misses down by reason:

```
[lost] draws no ejecutados=N (vs=.. fvf=.. buf=.. prog=.. up=..)
```

- `vs`   a vertex shader is set and no effect is active, so `CanDrawFVF()` refuses
- `fvf`  no position in the FVF
- `buf`  no vertex or index buffer bound
- `prog` the effect path could not build an input layout or had no stream
- `up`   `DrawPrimitiveUP` bailed out

Measured in the lobby, 49201 frames: **no `[lost]` line at all**, `progOK == DIP`,
`fvfDrew == UP`, `fvfSkip == 0`. In the lobby the correspondence is exact. It still
has to be measured in a match, which is where the effects live.

## Gaps that can change what you see

These are the stubs that touch rendering. Everything else that is stubbed
(private data, palettes, cursor, patches, `TestCooperativeLevel`, the `CheckDevice*`
family, `BeginScene`/`EndScene`) is either meaningless under D3D11 or correct as-is.

| Method | State | Why it matters |
|---|---|---|
| `SetTextureStageState` types >= 8 | **fixed** | dropped `D3DTSS_TEXCOORDINDEX` (11) and `D3DTSS_TEXTURETRANSFORMFLAGS` (24). The state is kept now; the transform itself is still not applied in the shader |
| `SetTransform(D3DTS_TEXTURE0..7)` | **fixed** | texture matrices were discarded entirely. Stored now, not yet applied |
| `DrawIndexedPrimitiveUP` | **fixed** | was a stub that threw the draw away. Implemented. The game does not use it (`IUP=0` in a match) but a call that drops draws is wrong regardless |
| `SetVertexShaderConstantF` | stub | only matters if the game drives shaders without going through `ID3DXEffect`. Not observed so far |
| `SetPixelShaderConstantF` | stub | same |
| `SetScissorRect` | stub | UI clipping. Would show extra pixels, never a white quad |
| `SetLight` / `LightEnable` / `SetMaterial` | stub | fixed-function lighting. The effect materials are `nolight`, so not involved |
| `ProcessVertices` | stub | CPU vertex processing. Not observed |
| `ColorFill` / `UpdateSurface` / `GetRenderTargetData` | stub | surface helpers |
| `SetStreamSourceFreq` | stub | instancing |
| `MultiplyTransform` | stub | never seen called |

The texture coordinate transform is the interesting one: it is exactly how an
animated sprite sheet moves its UVs, and it was being dropped on the floor in two
different places at once. Whether the client actually drives it is logged as
`[uvxform]`; nothing appeared in the lobby, so it needs a match to confirm.

## The effect that renders white

`resources/effects/basic_walljump.scn`, parsed with the authoritative parser:

```
27 models, 6 verts / 4 tris each, all textured "tick2_blue.bmp"
material: nolight alphablend2 nocull nofog nodepthwrite
1 bone, 1 animation clip
```

Those 27 quads are the hexagons of the sphere. Notes:

- The `.scn` asks for `tick2_blue.bmp`; **no `.bmp` exists in the resources at all**
  (450 `.dds`, 0 `.bmp`). Every `.scn` asks for `.bmp` and the client substitutes the
  extension. The real files are DXT1, 32x32 / 128x128 / 64x64 with mips.
- The textures do load: `texUp` jumps by 19 on the exact frame of the wall jump,
  and 21 is the total mip count of the three textures.
- The material says `haze`, and **the string `haze` does not appear anywhere in the
  executable**. This client does not know that token.

Ruled out with evidence, so nobody has to try them again:

| Hypothesis | Result |
|---|---|
| White fallback in the effect path | 0 `WHITE fallback` in 7801 frames of play |
| `DrawIndexedPrimitiveUP` dropping the draw | `IUP=0` in a match |
| Texture format we do not map | 0 `UNKNOWN texture format` |
| Texture failing to be created | 0 `CreateTexture FAILED` |
| sampler -> texture mapping lost | resolves `=Y`, the game sets all 8 textures |

The one `[flat]` combination that does show up is `fvf=0x152`, `tex0=NULL`,
`COLOROP=SELECTARG1(DIFFUSE)` and `ALPHAOP=SELECTARG1(DIFFUSE)` — colour *and* alpha
come from the vertex, which is what D3D9 does too. That draw is rendered correctly.

## Fog

Implemented and compiling. It had two problems stacked:

1. `g.fogCB` was created and never used. So `NE_FogState` ran once per `BeginPass`
   and had to guess with "the map has fog configured", which also fogged the UI, and
   that is why the wrapper was disabled.
2. The wrapper never compiled: `error X3018: invalid subscript 'Position'` — the `.fx`
   input structs have no member with that name. The error was invisible because
   `wvsprintfA` ignores precision, so `%.600s` printed nothing (`LogLong` fixes that).
   Adding our own `SV_Position` parameter fails too, with `X4574`, because the
   `POSITION` semantic the struct *does* have already maps to it.

Fix: find whichever member carries `POSITION` and read its `w`, and move the fog
constants to their own cbuffer at `b1` that the backend refills on every draw with
the real per-object `FOGENABLE`. The UI draws with fog off, so the wrapper is inert
for it.

`D3DRS_FOGTABLEMODE` is 3 (`D3DFOG_LINEAR`), i.e. pixel fog, which is what the
wrapper does. At startup the values are `FOGSTART=0`, `FOGEND=10000`,
`FOGCOLOR=white` — invisible by definition; the per-map values are the real ones
(5000/6000 were observed in a match).
