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

### 1. Some weapon animations do not render ON SOME MAPS
The basic (non-skin) Mind Shock and Mind Heal, and the railgun charge, render a **white
square instead of the charge animation** — but only on SOME maps. On Station 2 the
railgun charge shows up perfectly (the radial arc gauge and its glow); on other maps the
same weapon, same skin, same everything, gives the white square.

Maps where the effect renders CORRECTLY, confirmed in game so far:
**Station 1, Station 2, Wonderland, Ice Square, Neden 3.**
There are more, they just have not been verified yet — the list is what has actually
been seen in game, not the complete set.
On the rest the same weapon gives the white square. That list is the most useful piece
of evidence there is: diff the bginfo `[RENDERER]` block of these five against a map
where it fails and see which key differs (fog range/colour, shademap texture,
FullSceneGlow*), then check that key against what the effect's pass samples.

That makes it a MAP-dependent bug, not a weapon-dependent one. The weapon effect is a
constant; what changes between maps is the environment the effect samples from: the
scene/refraction texture, the bginfo `[RENDERER]` block (fog range and colour, shademap
texture, light position), and the set of post-process passes the map runs. Two maps
therefore leave a different device state around the same draw.

The custom/skin versions of the same weapons are fine on maps where the basic ones fail,
which is the second clue: whatever separates a skin effect from a basic one is on the
same path as whatever separates one map from another.

Next step is to compare, on a map where it works against a map where it does not, the
device state around that exact draw: which SRV lands on the scene sampler, and which
render states the surrounding passes left behind.

Confirmed by measurement, so it does NOT need to be re-checked:
- the effect draws reach us (`prog=1`), with a bound texture and a valid SRV,
- the blend state is correct: `src=SRC_ALPHA dst=ONE`, i.e. additive, which is what
  the material's `alphablend2` asks for,
- no draw is lost (`[lost]` stays at zero), no unknown texture format, no failed
  texture creation, and the scene texture we fill is correct (dumped and inspected:
  it holds the real scene),
- `g_matTexture` does arrive, so the projective coordinates have their matrix.

The same white-square symptom on the wall jump, the dagger and the plasma sword WAS
fixed, by applying the render states a `.fx` pass declares. These two weapons were not
fixed by that, so they are a different cause.

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
