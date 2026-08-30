# Pending queue (NativeEngine)

Things seen in the game that are still unresolved. Everything below is
visual/cosmetic: the engine already runs the whole game on LOW/MEDIUM/HIGH.

## The .fx pass render states: WORKING, do not "improve" them

A `pass` in a `.fx` declares render states (`AlphaBlendEnable`, `ZWriteEnable`,
`CullMode`...) and D3DX applies them when the pass begins. We used to ignore them, so
every pass inherited whatever the device had. That is what painted the white square on
the wall jump, the dagger and the plasma sword: the refraction pass returns the scene
to REPLACE the background and asks for `AlphaBlendEnable = false`, and inheriting the
surrounding additive blend summed the scene over itself and burned it to white.

The version that WORKS (commit 02d3b7e, UI fine and effects fine) is simple:
- parse the states of every pass,
- hand them to the backend as an override consulted by `BeginProgDraw`,
- never write them to the device, and drop them in `NE_ClearProgram`.

Four "refinements" were then tried on top of it and every one broke the UI, all of
them leaving the elements opaque with their background rectangle showing:
1. writing the states to the device and restoring them on EndPass,
2. applying them only to the pass whose shader samples the scene,
3. consuming them in the first draw and dropping them there,
4. routing `DrawPrimitiveUP` through the effect program (this one also fixed nothing:
   the game never issues UP draws inside a pass, `UP == fvfDrew` in every run).

The lesson is not about any of those four in particular: it is that the plain version
was already correct, and each "improvement" was applied without measuring first. If
this needs to be touched again, verify against 02d3b7e before assuming the override is
at fault.

`NE_PASSSTATES` is not needed: the working version has no switch.

## OPEN ISSUES — all of these are WORK IN PROGRESS

Everything below still happens in game. Nothing here breaks the client: it runs, it is
playable, and the frame rate is fine. These are visual.

### 1. A white square is drawn OVER the weapon glow (camera dependent)
The glow itself renders **correctly** — the blue halo is there. What is wrong is a
**separate quad drawn on top of it**, opaque white. Two draws, not one broken effect.
This corrects the framing this issue carried all day.

It is **camera dependent, not map dependent**. Same map, seconds apart, only the camera
moved: from most angles it is now fine, from below the white quad still covers the halo.
The old "fails on Station 1/2, Wonderland, Ice Square, Neden 3" list was just where it had
been seen; every bginfo correlation chased on that premise was chasing nothing.

**The offending draw**, from a RenderDoc pixel history on a white pixel:
```
EID 5382  ClearRenderTargetView(1,1,1,1)  -> texture 6082, a 256x256 offscreen target
EID 5406  Draw(42)                        -> one effect sprite into a corner of it
EID 6188  DrawIndexed(6)  SRV=6082        -> quad on screen, SrcAlpha/One (additive)
```
`EID 6188` uses `Technique_DiffuseMap_Light0`, whose `PS_DiffuseMap_Light0` returns the
texture **verbatim** — no vertex colour, no modulation. So whatever is in that 256x256
buffer goes straight to the screen. It is white, so the quad is white.

**What is missing:** nothing fills that buffer with the scene. `GetUsage` on 6082 lists
exactly four events — the clear, one draw, two samples. In the engine the glow/blur chain
should put the blurred scene there:
`CRenderer_D3D::UpdateScreenTexture` (0x01C84680) is
`dev->StretchRect(GetRenderTarget(0) -> screenTex->GetSurfaceLevel(0))`, which fills the
512x512 screen texture (verified correct: dumped, it holds the real scene), and
`CFullSceneBlurShader_BindSceneTexture` (0x01CBDA00, vtable 0x023FB904) feeds that texture
to the blur as `g_TexDiffuseMap`. Where the blur's OUTPUT goes, and why it never lands in
the 256x256, is the open question. Next step: find the owner of that vtable and the render
target it pushes.

**MEASURED 2026-08-22 (logs [glowprog]/[glowdraw]/[clr], do not re-derive):**
- The draw INTO the 256x256 (texture 6082) is a **program/effect draw**, NOT the
  fixed-function path — the FVF log ([glowdraw]) is empty; only [glowprog] fires.
  So every RHW/vpW/Y-flip fix tried in the FVF default shader (line ~1335, VS at
  line ~481) is on the WRONG path and changed nothing for this effect.
- [glowprog]: `curVP=256x256 vpXY=0,0 src=5 dst=6 ab=1 zen=0` -> the effect draws
  into 6082 with the FULL 256x256 viewport at origin (no offset), blend
  **SrcAlpha/InvSrcAlpha** (normal alpha, over the white clear), Z off.
- [clr]: `256x256 color=0xFFFFFFFF rects=0` -> whole buffer cleared opaque white.
- Clearing that buffer to black/transparent to kill the additive white **breaks the
  UI** (UI uses the same small white-cleared offscreen buffers, filled opaque). So
  the white clear is REQUIRED; the fix is NOT the clear.
- The `EffDSV()` depth guard (never bind scene-size depth to a smaller RT) is kept:
  it fixed most angles. The remaining white is angle-dependent = the effect's
  POSITION inside 6082 (its g_matWVP, set by the game) vs where the composite quad
  samples it. Effect measured earlier at NDC y -0.73..-0.63 (bottom), not centered.
- **NEXT STEP (the only clean one): capture the COMPOSITE draw's geometry/UVs** (the
  DrawIndexed(6) additive quad on the backbuffer that samples 6082) — its screen
  rect + UVs tell whether it expects the effect centered or at NDC, and the Y fix
  follows deterministically. The effect goes through BeginProgDraw (program path);
  its g_matWVP is the lever, not anything in the FVF path.

**USER INSIGHT 2026-08-22 (decisive): it is the HEIGHT, not the angle.** At one
specific player height in the map the ball renders correctly; at other heights the
white square. Meaning: the effect draws into 6082 with the SCENE CAMERA, so its
position in the buffer tracks the player's on-screen Y (= map height). The composite
samples a FIXED spot, so it only lines up at the height where the effect's buffer Y
matches. => The fix must make the effect's position in 6082 INDEPENDENT of height
(centered/fixed), OR match the composite. Two concrete things to check first:
  1. Is the game setting a height-compensating SetViewport for the offscreen pass
     that our SetRenderTarget clobbers (it resets curVP to the full RT size)?
     Log SetViewport + SetRenderTarget order + curVP at the [glowprog] draw for a
     GOOD height vs a BAD height and diff.
  2. Log the effect's g_matWVP (the game's matrix) at good vs bad height; the delta
     is the Y term to neutralize/center.

  REFINED: effect cbuffer (matrices) IS uploaded per-pass (effects.cpp UploadCB in
  BeginPass ~686-694), so the effect uses the game's scene-camera matrix = height-
  dependent AS INTENDED, same as the original. So the effect is NOT the bug -> the
  COMPOSITE is: the DrawIndexed(6) additive quad on the backbuffer that samples 6082.
  In the original it must sample the effect's sub-region (or follow it); ours samples
  UV 0..1 of the whole buffer, so it only lines up at the one height where the effect
  sits centered in the buffer. NEXT: capture that composite quad's per-vertex UVs +
  positions (log the UP/indexed vertex data for the backbuffer draw that binds a
  256x256 SRV additively) and compare to a UV 0..1 fullscreen assumption.

**Ruled out with evidence — do not re-derive:**
- `.fx` pass states: the client declares only `FogEnable` (x9) and `AlphaBlendEnable` (x5)
  across every pass block, and we parse both. Zero `.fx` on disk. Not the dagger fix.
- The weapon sprite texture is correct (white RGB, shape in the alpha channel).
- The per-vertex colour arrives: `(0.098, 0.098, 0.247, alpha 0.149)`.
- The white clear is what the game asks for (`flags=0x1 color=0xFFFFFFFF rects=0`),
  identical on maps where it works and where it fails.
- The 512x512 scene texture content is correct (dumped from a capture).
- `MapBlend` is 1:1 with `D3DBLEND`; the fixed-function cascade and its alpha are complete.
- Scene texture refresh (107/120 frames on a failing map), bginfo keys (four checked,
  none splits the list), the glow/haze pass gating from IDA.
- `g_vEyePos` never arrives — but the `.fx` gives it no default either, so D3DX would also
  leave it at zero. That is 1:1, not our bug.
- Orphan interpolants: the VS and PS share the struct, so there are none.

**Fixed along the way (real 1:1 gaps, each independent of this bug):**
- Blend state leak: with the cache full, `GetBlend` built a new `ID3D11BlendState` per draw
  and released none — 3.5GB in seconds once `BlendOp` widened the key.
- `D3DRS_BLENDOP` was hardcoded to `ADD`; the pass state parser already read `BlendOp`.
- `Clear` ignored the `D3DRECT` list and always cleared the screen depth, not the bound one.
- `SetDepthStencilSurface(NULL)` bound the screen depth instead of unbinding it.
- `NTexSurface::UnlockRect` — the client's real texture path — bypassed the crash guard.
- `ParseDefaults` registered shader LOCALS (`Final`, `tmpPos`, `ShadeColor`...) as uniforms.
- Entry points were matched as substrings: `PS_DiffuseMap` hit `PS_DiffuseMap_BumpMap_*`.
- `IsParameterUsed` returned `TRUE` for everything; the engine builds a 29x15 decision
  table from it (`sub_11D6910`).
- `GetParameter`/`GetDesc` reported no parameters at all; now 18 for the world shader.
- **The bump fallback was `0xFFFF8080`, which in BGRA is R=1.0** — and the refraction
  shader does `BumpMapColor.xy = (tex.xy - 0.5) * 2` and adds it to the projective UV, so
  we were shifting the refraction lookup by a full 1.0. Corrected to 0.5 grey (zero
  offset). This is the change that made the glow appear at all.

### 2. Fog is not visible
The `_NEFOG` wrapper compiles now (it never did before: the `.fx` input structs have no
member called `Position`, so the wrapper has to read whichever member carries the
POSITION semantic), and the constants ride their own cbuffer at `b1` refilled on every
draw with the real per-object `FOGENABLE`. Despite that, no fog shows up in game.
`D3DRS_FOGTABLEMODE` is 3 (`D3DFOG_LINEAR`), so pixel fog, which is what the wrapper
implements.

### 3. The map flickers when there are many players
A flicker over the map that shows up with several players on screen. Not diagnosed yet.
Worth checking whether it correlates with the number of effect passes per frame, since
the render target stack is pushed and popped per effect.

### Known-good baseline
Commit 02d3b7e is the reference: UI correct, and wall jump / dagger / plasma sword
correct. Four attempts to "improve" the pass state handling on top of it all broke the
UI (see the section above). Verify against that commit before assuming a regression
comes from the pass states.

## Jump effect: a WHITE SQUARE expands
On jumping, instead of the effect (hexagons/wave) you see a **growing white square**.
So the scale animation DOES run; what fails is the effect's TEXTURE: it does not
resolve, falls back to white, and since the quad scales you get an expanding white
square.

`basic_walljump.scn` parsed with the authoritative parser: 27 models of 6 verts /
4 tris each, all textured `tick2_blue.bmp`, material
`nolight alphablend2 nocull nofog nodepthwrite`, 1 bone, 1 animation clip. Those 27
quads are the hexagons of the sphere. Note the `.scn` asks for `.bmp` and the real
files are `.dds`: no `.bmp` exists in the resources at all, the client substitutes
the extension.

## STANDBY platform floor with stripes
Horizontal stripes on the waiting room floor, they change with the angle.
RULED OUT so far:
- z-fighting: `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` were implemented (translated to
  D3D11 integer units, x2^24 for the 24 bit depth) and it did NOT change.
- the `TextureNoise` layer: the striping was there BEFORE the PixelShader=null fix,
  so that layer did not introduce it.
- empty mips: the SRV now exposes only the filled levels + 16x anisotropic.
- generating the missing mips: implemented (decode BC -> box filter -> re-encode) and
  it only improved things slightly.
- forcing mips where the game asks for 1 level: REVERTED, it broke the UI buttons
  (the BC re-encode is lossy) and added ~120MB.
- per-sampler CLAMP (the .fx asks for it on ShadeMapSampler, which indexes by the
  light term): the idea is GOOD but the implementation with a global pointer hung
  loading. Redo it passing the map as a parameter.

DECISIVE FACT: the floor is REFLECTIVE and it is reflecting the WRONG geometry (you
can see the waiting room, which is up in the sky, reflected in the match floor). The
effect also "comes and goes" as the camera moves.
=> It is NOT filtering (mips/CLAMP/aliasing): it is the CONTENT of the reflection
texture. The shader uses `tex2Dproj(SceneMapSampler, ...)` (seen in fx_2), i.e. it
samples the SCENE texture with projective coordinates.
We fill that texture by copying the backbuffer in StretchRect. Suspicion: the game
expects a MIRRORED render of the scene there (planar reflection) or the same frame at
a different point in the pipeline; with a copy of the backbuffer taken at the end, the
reflection shows whatever is on screen (hence the waiting room).
Next step: log the real order -> who does SetRenderTarget to that texture and what the
game draws while it is active (if it draws the mirrored scene, that render has to be
respected instead of being overwritten with the backbuffer copy).

NOTE: the per-slot CLAMP sampler was implemented anyway (it is what the .fx declares
and it is correct), even though it was not the cause of this.

CAUSE FOUND (reversing, see renderer_documentation.txt):
CRenderTargetManager_D3D::PushRenderTarget (0x01D902E0) saves the current target with
`GetRenderTarget(0)` and pushes it; Pop restores it with SetRenderTarget. Our
GetRenderTarget ALWAYS returned the backbuffer, so on a nested push the backbuffer got
pushed instead of the outer target and the Pop sent to SCREEN what belonged in the
texture (leaving the texture half filled / stale).
On top of that the screen texture is a fixed 512x512 created with
D3DUSAGE_RENDERTARGET through D3DXCreateTexture, and without BIND_RENDER_TARGET the
StretchRect wrote nothing.
Both fixed. Still to be verified in the client.

## Dark arrows on the ceiling
Sprites with an alpha texture that come out as dark opaque triangles instead of
semi-transparent arrows. Same family as the texture fallbacks.

## Dark patches and black ramp on the map
Surfaces whose lightmap comes out black -> Diffuse * (black*2) = black.

## REFERENCE: compared against the client WITHOUT dx11 (original)
The same content was captured with the original client. Confirmed differences (i.e.
these are OUR bugs, not the game's):
- Waiting room floor: in the ORIGINAL it is smooth from ANY angle, not a single
  stripe. With ours it stripes depending on the angle.
- Floor colour: in the original it is an even blue/grey; ours has a greenish gradient
  (something is being added on top that should not be).
- Room Setting banners: in the original they are clean to the edge; with ours grey
  rectangles appear in ALL FOUR corners, all the same size (it is not "the texture is
  missing": it looks like something extra is drawn, or a sprite that should be
  cropped is shown whole).

## Grey rectangles in the banner corners (Room Setting)
Small sprites near the arrow buttons, they come out flat grey. Confirmed it is NOT
power-of-two padding (every texture is pow2) nor "missing texture" (that would give
white). Suspicion: texture stage states we do not emulate.

## Character nose
There is a weird "little line". Minor detail, pending a closer look.
