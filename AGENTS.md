# AGENTS.md

Project operating instructions and shared memory for AI assistants working in this repository.

## Shared Project Memory

- At the start of each new task or thread involving this repository, read this file before inspecting files, running commands, making a plan, or taking any other project action.
- Treat follow-up replies in the same continuous task as part of that task. Do not reread this file unless the repository or working directory changes, this file is modified, or its instructions are no longer available in context.
- Treat this file as the shared project memory for AI assistants.
- Do not rely on vendor-specific, proprietary, or hidden memory systems for project facts, preferences, or operating instructions. (except to remember to ALWAYS read this file first before doing anything.  Remember that.)
- Update this file with important repo-specific information learned during work, including build commands, test commands, conventions, decisions, pitfalls, and current project preferences.
- Keep this file accurate and current. Remove or correct stale, misleading, or incorrect information when discovered.
- If information is temporary or uncertain, label it clearly rather than presenting it as permanent fact.

Scope policy: this file holds cross-cutting rules, workflows, and gotchas that most sessions need, plus a feature index. Keep it around 30 KB. Feature deep-dives live in `docs/<topic>.md`: before working on a feature listed in the index, read its doc; when finishing feature work, update that doc and keep the index entry here to one or two lines (where it lives + the non-obvious constraint). Cross-cutting rules and new gotchas still land here directly. When a change makes anything stale, here or in a linked doc, update it in the same change.

## What this is

HoloVCS runs Atari 2600 / NES / Virtual Boy emulators (libretro cores) and renders each game as
stacked textured quads at different depths ("diorama" effect), originally for Looking Glass
holographic displays. Author: Seth Robinson (rtsoft.com).

## Two uprojects, one source tree

| File | Purpose | Engine | Game target |
|---|---|---|---|
| `HoloVCS_Flat.uproject` | Flat build for a normal monitor, no plugin. Day-to-day dev. | UE 5.8 (`F:\UnrealEngine\UE_5.8`) | `HoloVCS` (DefaultGameTarget in DefaultEngine.ini) |
| `HoloVCS.uproject` | Looking Glass hardware build (vendored ported plugin enabled) | UE 5.8 (same engine) | `HoloVCSLKG` (bats pass `-target=HoloVCSLKG`) |

**Why two game targets:** both uprojects share Source/ and one target name would share
Intermediate/Binaries too, so whichever flavor linked last got staged by BOTH test bats (the
plugin-enabled monolithic Shipping exe is a different binary). `HoloVCSLKG.Target.cs` gives the
hardware flavor its own output paths. The editor target (`HoloVCSEditor`) stays shared - editor
builds are modular and the plugin en/disable is resolved at runtime from the uproject.

Build the flat editor target from the command line:

```
F:\UnrealEngine\UE_5.8\Engine\Build\BatchFiles\Build.bat HoloVCSEditor Win64 Development -project="f:\Unreal\HoloVCS_UE56\HoloVCS_Flat.uproject" -waitmutex
```

The hardware variant builds the same way with `-project="f:\Unreal\HoloVCS_UE56\HoloVCS.uproject"`.

GOTCHA: the shared HoloVCSEditor target also shares ONE UBT makefile
(`Intermediate\Build\Win64\x64\HoloVCSEditor\Development\Makefile.bin`) between both uprojects.
After building the Flat flavor, building the LKG flavor can report "Target is up to date" WITHOUT
compiling plugin changes (and vice versa). If a plugin edit doesn't compile, delete that
Makefile.bin and build again.

Run flat: `F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe HoloVCS_Flat.uproject -game -windowed -resx=1280 -resy=720`
The default map is `Content/Maps/NewMap_Flat.umap`. The hardware map is `Content/Maps/NewMap.umap`
(`/Game/Maps/NewMap`) and holds the LookingGlassCapture actor. CAUTION: `Content/NewMap.umap` is
only a 1.3KB REDIRECTOR to it (and `Maps/NewMap_OldVersion.umap` is an old copy) - always use the
full `/Game/Maps/NewMap` path in cook args and code; a short `-map=NewMap` cooks ambiguously and
the redirector does not get staged. When the plugin is loaded (hardware build only), the game
module redirects GameDefaultMap to `/Game/Maps/NewMap` at PostEngineInit (see HoloVCS.cpp), so the
packaged HoloVCSLKG.exe boots the hardware map when double-clicked while the shared ini keeps the
flat map as default.

**One-way door:** any .uasset/.umap resaved by the 5.8 editor becomes unreadable to UE 5.6. The
restore point for the old 5.6 setup is the "UE 5.6 migration baseline" commit. NewMap.umap is still
in its 5.6-saved form; resaving it in 5.8 is now safe content-wise (the capture actor class exists)
but burns the 5.6 escape hatch like any other resave.

## Looking Glass status (ported to 5.8, vendored)

- The plugin is VENDORED in this repo at `Plugins/LookingGlass` (MIT licensed), ported to UE 5.8 in
  Aug 2026. Base: upstream `feat/5.6` branch commit 9472c22 (the real source of the 2.1.1 release;
  the 2.1.1 GitHub tag confusingly points at main, and the uplugin VersionName still says "1.6").
  Upstream: github.com/Looking-Glass/Looking-Glass-Unreal-Plugin - still no official 5.7/5.8 release.
- The old 1.6-era copy in the engine remnant at `F:\UnrealEngine\UE_5.6\Engine\Plugins\LookingGlass`
  is now only of historical interest.
- The C++ game module has ZERO compile-time dependency on the plugin. The only coupling is the
  uproject plugin entry, the capture actor in NewMap.umap, and the
  `[/Script/LookingGlassRuntime.LookingGlassSettings]` block in DefaultEngine.ini.
- 5.8 port changes (all in Plugins/LookingGlass, look for `UE_VERSION_OLDER_THAN` / 5.8 comments):
  PostInterpChange removed (guarded out for 5.7+); windows.h macro leak from bridge headers fixed
  with AllowWindowsPlatformTypes wrapper in LookingGlassBridge.cpp; RenderTarget member became
  TObjectPtr (incremental-GC crash otherwise); FSceneViewport::Create replaces deprecated ctor;
  GetOnPostEngineInit()/EWindowType::Normal deprecation fixes; SCOPED_GPU_STAT removed (no-op in
  5.8); FIntPoint Resolution default-init (5.8 CDO determinism check); Shipping-only include fixes
  (RendererInterface.h, ModuleManager.h, WITH_EDITOR guard on ISequencerObjectChangeListener.h);
  null-check GUnrealEd in ViewportClient Draw (crashed editor-binary -game runs, pre-existing bug).
- Verified without hardware (Bridge running, no display): module loads, Bridge SDK connects and
  enumerates calibration templates, logs "No Looking Glass displays found" gracefully, NewMap loads
  with the capture actor, plugin takes over the game viewport (draws black without a device; that is
  expected - use the flat uproject for screen dev). The surprising good news: the scene-capture fork
  (`LookingGlassSceneCaptureRendering.cpp`) compiled against 5.8 without changes.
- VERIFIED WORKING on the Portrait (Aug 2026): the hologram renders full-size on the device. Two
  more fixes were needed beyond compiling:
  - The map's capture actor was placed/sized for the old 5.6-era world scale. The game module now
    auto-fits it to the layer AABB on every layer rebuild (FitLookingGlassCaptureToLayers in
    LibretroManagerActor.cpp - moves the actor to the AABB center, calls SetSize via reflection,
    and puts the capture in PRM_UseShowOnlyList with only the "Layers"-tagged actors so the pawn's
    tint plane and other scene junk can never leak into the hologram). `-lkgsize=N` on the command
    line overrides the computed capture size for tuning.
  - The plugin fork's show-only path was broken upstream (added show-only prims to HiddenPrimitives,
    fixed in LookingGlassSceneCaptureRendering.cpp).
  The F9 quilt-screenshot hotkey did not respond in -game main-viewport mode.
- Tiling: the game forces the plugin's Automatic preset at fit time (resolves per-device via
  Bridge; Portrait = 8x6 tiles, 3360x3360, aspect 0.75). Pass `-lkgmaptiling` to keep the tiling
  saved in the map (the 5.6-era Custom 11x6/4092/0.5625). Framing also crops to the USED portion
  of the emulator texture (base_width/height vs max_width/height) - without that, Pitfall (320x228
  used of 568x312) rendered small and off-center. EXCEPTION: Virtual Boy skips the crop
  (EMULATOR_VB check in FitLookingGlassCaptureToLayers) - beetle-vb delivers pre-split layers via
  the custom refresh callback that FILL the whole texture, so cropping to base/max showed a
  quarter of the screen zoomed in. VB games also read 48-51 FPS on the counter: that is the VB's
  native ~50.27Hz refresh, correct behavior, not a performance bug.
- Startup ROM: `-rom=partialname` (e.g. `-rom=itfall`) overrides the hardcoded startup ROM - much
  faster than cycling with ","/"." for testing. NOTE: the ROM-cycle keys stopped responding via
  SendKeys after the VB core loaded; unresolved, use -rom= instead.
- FPS counter: hardware builds show FPS on the in-world status text (5x size) once per second and
  log "N FPS" to the log; the plugin also logs per-phase frame times ("LKG frame phases") once a
  second from the viewport client.
- PERFORMANCE: SOLVED at 60 fps via the custom sprite-quilt renderer
  (FLookingGlassViewportClient::RenderSpriteQuilt in the plugin fork). UE 5.8's deferred renderer
  costs ~1.4ms render-thread CPU per scene-capture view no matter how simple the scene (measured:
  view count, family count, Bridge, vsync, post effects, capture source, Lumen/VSM all eliminated
  as causes - quilt scene captures were a hard ~85ms/frame). The sprite path draws each show-only
  quad as a projected rectangle per view (one render pass, ~240 rect draws, 0.7ms total) instead
  of scene-rendering 48 views. Requirements/limitations: capture must be in show-only mode with
  unrotated actor; every listed primitive needs a UMaterialInstanceDynamic whose emulator texture
  is the "Texture" parameter (HoloVCS convention) or the first set texture param; layers render
  UNLIT with texture colorkey alpha (no scene lighting/shadows/material effects in the hologram);
  status text (TextRenderComponent in the show-only list) is overlaid per-tile via FCanvas.
  Anything not satisfying the constraints falls back to the scene-capture path automatically, and
  `lkg.SpriteQuilt 0` (cvar) forces the old path for A/B. MaxView is 64 so the fallback renders
  the whole quilt as one view family. Lumen/VSM/reflections stay disabled in LKG runs
  (HoloVCS.cpp) - harmless for sprites, saves memory.
- Sprite-quilt lighting/shadows: the scene's visible directional light drives a flat tint
  (camera-facing quads share one normal; `lkg.SpriteAmbient`, default 0.7, is the ambient floor)
  and each layer stamps a black silhouette drop shadow onto the layer behind it
  (`lkg.SpriteShadow`, default 0.6 opacity, 0 disables). Hiding the light renders unlit.
  THE LIGHT RIG: the light of record is the map's POINT light (PointLight_1 at -705,1,14 -
  the old build's original rig; the port had disabled it and substituted a straight-on 0.3 lux
  directional whose shadows hid exactly behind their casters, which is why the 2D view seemed
  to have no shadows at all). InitLayers re-enables it per lighting mode and keeps the helper
  directional OFF whenever a point light exists; the 7 key toggles ITS CastShadows now.
  CRITICAL: the map saved the light as STATIC - a static light contributes NOTHING at runtime
  without baked lighting, so InitLayers forces Mobility=Movable or the whole 2D scene renders
  as if unlit no matter what gets toggled. Virtual shadow maps are back ON (HoloVCS.cpp no
  longer disables them - they were only a cost when 48 scene captures existed; the sprite path
  scene-renders just the single 2D window) because legacy shadow maps swallow the NES
  diorama's 2-unit layer gaps in bias and the 2D view shows no shadows at all; the light also
  gets a tight ShadowBias 0.05 / SlopeBias 0.15. The lit sprite tint with a point light is
  FULL brightness x normalized light color (the old rig lights the camera-facing layers
  head-on at near-uniform distance, so the old lit look was full-bright + shadows; the
  tonemap-curve calibration only applies to the directional fallback). Note: layer materials
  are BLEND_Masked and meshes cast shadows - both fine; per-ROM lighting settings do NOT
  exist in old or new source (only SetTintBG colors, SetBGPic, m_layerSetupInfo shadow flags).
  lkg.SpriteDepthDim now defaults 0 (it was a pre-shadow crutch; the old build had no such
  dimming). Also: the port had flipped SMB's SetTintBG bAllowShadows from the old true to
  false - old source is the spec for those flags.
- THE LKG BUILD HAS NO 2D WORLD VIEW: the main window is just an input/focus target
  (bDisableWorldRendering when the plugin is loaded) - the "2D spectator view" was never a
  requested feature, and keeping its scene shadows presentable was a parallel workstream that
  produced VSM flicker, blotch, and bias sagas. Pass -lkg2dview to re-enable the scene render
  for side-by-side debugging (the 2D-vs-panel diff method found many hologram bugs).
- TESTING GOTCHA: the help/splash screen only EXISTS in Shipping FLAT builds now (dev builds
  destroy the "SplashScreen"-tagged actor at BeginPlay, and the LKG build destroys it too -
  it's invisible on the device but floated over the 2D spectator view waiting for a click).
  Its primitives also get SetCastShadow(false). When verifying "Shipping-only" visuals, test
  the STAGED build, not editor -game.
- SPRITE SHADOWS (rewritten Aug 2026 - one mechanism, NO thresholds): each receiving layer
  gets a per-frame 512px mask in its own quad-UV space whose alpha = (union of every nearer
  caster's silhouette, projected from the light onto the receiver's plane) x (the receiver's
  own texture alpha). Each view then applies ONE darkening stamp of that mask over the layer,
  right after the layer draws (nearer layers draw later and cover it, so occlusion works).
  That is exactly what a real shadow map computes: every caster always casts, every pixel
  darkens at most once no matter how many casters overlap it, and shadow only lands where the
  receiver has pixels. There are NO content-size thresholds - the previous dense/sparse split
  gated by SolidFrac/RectArea silently killed the player's shadow (a sprite layer's content
  rect spans the screen whenever any other sprite is on screen) and made bush layers pop
  dark/light as scrolling moved them across the cutoffs. Casting honors Mesh->CastShadow,
  receiving honors bReceiveMobileCSMShadows (per game profile); the game reports each layer's
  populated texel bounds via custom primitive data floats 0-3 (zero rect = empty layer casts
  nothing; float 4 is unused now). Mask build: pass 1 into a shared scratch RT clears alpha
  to 1 and draws casters with SE_BLEND_AlphaHoldout (alpha *= 1-caster) = inverted union;
  pass 2 into the receiver's pooled RT draws the receiver's alpha (GWhiteTexture for the
  backdrop/opaque wall) with SE_BLEND_AlphaBlend then the scratch RT with AlphaHoldout,
  leaving alpha = receiver x union. ENGINE GOTCHA that broke the old wall mask: canvas
  SE_BLEND_Translucent NEVER writes dest alpha (its blend state is BF_Zero/BF_One on alpha),
  so a mask "accumulated" with it stays at the clear value and stamps draw nothing - use
  SE_BLEND_AlphaBlend / SE_BLEND_AlphaHoldout when a canvas RT's alpha channel matters. Each
  mask flush is followed by an explicit RTV->SRV transition before another canvas samples it.
  The projection scale (Lr-Lx)/(Lc-Lx) stays near 1 for the far-forward light - shadows hug
  their casters instead of towering. A directional fallback synthesizes a far-away light
  position so the same code path serves maps without a point light. lkg.SpriteShadow
  (default 0.6) is the only shadow cvar left; lkg.SpriteShadowBlur is gone. 2D scene shadows
  are LEGACY shadow maps (VSM flickered/blotched on the per-frame-updating masked textures;
  the tight ShadowBias 0.05/SlopeBias 0.15 on the point light is what makes legacy resolve
  the 2-unit layer gaps).
  Backdrop tint semantics: the BGLayer materials REPLACE their texture with ColorTint as
  TintStrength approaches 1 (SMB's solid sky blue) - at strength >= 0.99 the sprite path draws
  GWhiteTexture x ColorTint instead of tinting the material's (mostly black RTsoft logo)
  texture. NoShadow-family backdrops draw UNLIT (the old "lighting turned off on the bg").
  THE GAMMA CHAIN (calibrated, don't break it): the sprite quilt RT has TargetGamma=1 and the
  lenticular pass encodes nothing (lkg.SelfRenderGamma default 1.0) - emulator textures are
  SRGB=0 display-encoded values and the chain passes them through UNTOUCHED. Before this,
  FCanvas applied a 1/2.2 encode and the lenticular another, so vertex tints and shadow alphas
  reached the panel as value^(1/4.84): a 0.47 tint displayed as 0.86 and ALL lighting looked
  disabled (Seth: "like I turned lighting completely off").
  LIT vs UNLIT: each layer's material shading model drives the sprite path (FSpriteLayer.bLit) -
  the game's 8 key swaps layer materials between lit/unlit variants and the hologram follows
  (unlit layers draw raw, no tint/dim/shadows). The lit tint matches the tonemapped 2D window:
  display multiplier = 0.864 * pow(illuminance/pi, 0.2545), a TWO-POINT fit against the real
  scene render (0.095 linear -> white texels at 121/255; 0.979 linear -> 219/255, the ACES
  shoulder flattening the bright end). Point-light illuminance = candela/dist(m)^2 at the focal
  plane (lumens converted by /4pi).
  Additional depth cues (added after Seth compared with the old build's lit look):
  - `lkg.SpriteDepthDim` (default 0.3): deeper layers render darker, back wall at 70%.
  - `lkg.SpriteTilt` (default 8 degrees): vertical off-axis shear - the camera is effectively
    raised above the diorama looking slightly down, like the old build's framing. Implemented
    as a Z shear in NdcZ (same focal-plane-invariant form as the horizontal view offset), so
    quads stay axis-aligned rects and the fast path keeps working.
  - The "LayerBG" backdrop wall (moon photo etc, material swapped per game profile) is added to
    the capture's ShowOnly list by the fit code (NOT into the framing AABB - it's bigger than
    the game) and renders as the deepest sprite layer, so drop shadows land on it. Its material
    is a plain UMaterial: the sprite gather falls back to Mat->GetReferencedTextures() for the
    texture (GetUsedTextures returns NOTHING at runtime, wasted an hour) and draws opaque-blend
    when the material is BLEND_Opaque (photo textures have no meaningful alpha).
  - Sprite-path bails log a throttled "SpriteQuilt bail:" warning naming the offending
    actor/component/material - if the hologram drops to ~14 fps, grep for that. The path also
    dumps a one-shot "Sprite layer N: ..." listing (actor, material, depth, flags) whenever the
    layer set changes shape - the fastest way to see what the hologram is actually drawing.
- PER-GAME LAYER SETUP (the debugging method that found all of these: capture the 2D window
  AND the panel for the same game via scratchpad capture_both.ps1 and diff them - the 2D scene
  render is the ground truth for what the hologram should show):
  - The LayerBG backdrop wall's map position suited the NES span (+-4) but sat INSIDE the wider
    Atari (+-40) and VB (+-200) stacks, covering every layer behind it (Pitfall lost its
    background layer, VB games lost most layers). FitLookingGlassCaptureToLayers now parks the
    wall at box.Max.X + 5 every layers rebuild (fixes flat view AND hologram).
  - The ported map lost the pawn blueprint's m_pBGMatNormal/m_pBGNoShadowMat references AND the
    LayerBG mesh's "StaticMeshComponent" component tag, AND the game profiles run before the
    pawn's BeginPlay: SetTintBG/SetBGPic silently no-oped, leaving the wall on WorldGridMaterial
    (giant red/teal noise blocks on the panel, near-invisible in the dim 2D view). Fixed with
    constructor-loaded material fallbacks, a lazy FindBGMeshIfNeeded(), and a null-parent guard.
  - Backdrop flavors in the sprite path, judged by BASE material name: "backdrop" materials
    (SetBGPic pictures) draw unlit/full-bright; the BGLayer family draws LIT and colored by the
    MID's ColorTint/TintStrength (SetTintBG); "NoShadow" in the name = receives no shadow stamps.
  - Per-layer shadow receive honors the mesh's bReceiveMobileCSMShadows (set from
    m_layerSetupInfo per game profile); every receiving layer takes its own mask stamp.
  - Transient layer textures start as uninitialized VRAM: SetupLayer now uploads the zeroed
    buffer once so layers a game never blits stay transparent instead of showing garbage.
- Capture auto-fit sizing: the capture's Size = half-WIDTH of the frame at the focal plane
  (measured empirically). Fit formula: Size = max(halfW, halfH * deviceAspect) * 1.10, where
  deviceAspect (0.75 Portrait) comes from GetAspectRatio - made a UFUNCTION in the fork so the
  game can ProcessEvent it. The 10% margin keeps the game off the lens edges (Seth: edge-to-edge
  reads as "too wide" on the device).
- On-quilt text renders identically in every tile, which the lens reconstructs as ONE crisp
  screen-locked overlay - use this trick for any HUD text. FPS counter (`lkg.ShowFPS`, default 1)
  draws top-left; status messages draw at the tile bottom, auto-shrunk to fit the tile width.
- The Bridge window steals keyboard focus whenever it opens or is clicked (users naturally click
  the hologram), killing all pawn hotkeys and game controls (everything routes through UE input).
  KeepGameWindowFocused() in LibretroManagerActor::Tick polls every 0.3s and bounces focus back
  to the game window whenever another window OF THIS PROCESS holds it; focus in other apps is
  left alone. Logs "Bounced focus..." each time.
- THE FREEZES (SOLVED architecturally): gameplay freezes of 0.7-20s were PROVEN to be Bridge SDK
  calls stalling (field logs: every HITCH matched a Bridge stall to the hundredth of a second;
  the final fingerprint was DrawInteropQuiltTextureDX itself stalling 19.75s, on the latest
  Bridge 2.6.3 whose own changelog mentions fixing "device query latency spikes"). The Bridge
  SDK ALSO has hard thread affinity - every call must come from the thread that initialized the
  controller (split-thread attempts produced idle-logo-only, then no window). Solution: the
  ENTIRE Bridge lifecycle now lives on one dedicated thread (FLookingGlassBridgeThread in
  LookingGlassBridge.cpp) - controller init, window creation (the thread pumps Windows
  messages for it), texture registration and draws. Game-thread API: Initialize/Shutdown block
  (boot/exit only), QueueDraw is fire-and-forget latest-wins, RequestReadDisplays /
  RequestStopRendering are async. Bridge stalls now pause only the hologram; the game, audio
  and input keep running, and stalls >0.25s are logged to lkg_diag.txt next to the top-level
  exe with the exact sub-call name. Known minor caveats: quitting during an active Bridge stall
  can block shutdown until the stall ends; Displays[] refresh on display hot-plug is async
  (stale reads briefly possible).
- STALL ROOT CAUSE (established via unattended soak repro + mid-stall stack capture): the plugin
  walks the stuck bridge thread's callstack after 2s and appends it to lkg_diag.txt - it shows
  DrawInteropQuiltTextureDX parked in an ntdll WAIT inside bridge_inproc.dll while the Bridge
  service sits idle: a blocked synchronization wait (looks like a synchronous IPC round-trip
  with a ~19-20s timeout; short stalls cluster at ~1.4s). Looking Glass's bug on their latest
  Bridge 2.6.3; upstream report draft with the stall table and module-relative stack offsets:
  docs/lkg_bridge_stall_report.md. Our side is fully mitigated (the game never freezes); the
  hologram still holds its last frame during a stall - that part is only fixable upstream.
- STALLS SOLVED FOR REAL - SELF-RENDERED LENTICULAR OUTPUT (lkg.SelfRender, default 1): the
  "Bridge version regression" theory was DISPROVEN by Seth running his old packaged build
  (C:\temp\HoloVCS) against the same Bridge 2.6.3 with zero stalls. The real difference: the old
  build's plugin (HoloPlay 1.2 via HoloPlayCore.dll, source still at
  F:\Unreal\HoloVCS_old\Plugins\HoloPlay, UE 4.27) queried calibration ONCE at boot and rendered
  the lenticular output ITSELF with its own shader in its own window - no per-frame Bridge IPC
  existed to stall. The open-source 1.6 plugin replaced that with per-frame
  DrawInteropQuiltTextureDX, which is the thing that blocks. We ported the legacy lenticular
  pipeline back into the fork:
  - Shaders/Private/LookingGlassLenticular.usf (port of HoloPlayLenticularShader.usf, PS only;
    the engine FScreenVS + IRendererModule::DrawRectangle with V0=1/SizeV=-1 replaces the old
    custom VS/vertex buffer). Mapped via AddShaderSourceDirectoryMapping in StartupModule
    (module already loads PostConfigInit).
  - RenderLenticular_RenderThread + the self-render branch in
    FLookingGlassViewportClient::VisualizeRenderTarget: quilt RT -> lenticular PS -> the
    plugin's own SWindow viewport. Bridge is ONLY used for boot-time calibration now.
  - Calibration math (raw Bridge values -> shader values, from the legacy display manager):
    screenInches = W/DPI; pitch = rawPitch*screenInches*cos(atan(1/rawSlope));
    slope = H/(W*rawSlope); center = rawCenter; subp = 1/(3*W) (negated by FlipX).
    Verified: our Portrait's raw pitch 52.579 derives to 246.86 = Bridge's own logged
    "Shader Pitch: 246.868".
  - GOTCHA that cost hours: the lenticular sampler MUST be AM_Wrap. FlipYTexCoords=1 negates
    the view coordinate so texArr() produces negative/overflowing UVs by design (legacy code
    sampled with the render target's default wrap sampler). With AM_Clamp the panel output is
    ~33x too dark (sparse subpixel dots) - diagnosed by comparing panel-capture brightness
    stats against the Bridge interop path rendering the same game.
  - Quilt RT is 16-bit linear, the window backbuffer is gamma-space: the PS encodes with
    OutputGamma (lkg.SelfRenderGamma, default 2.2). lkg.SelfRenderQuilt 1 shows the raw quilt
    in the device window (debug). Verified colors/brightness/fringe pattern match the Bridge
    interop output on-panel almost exactly.
  - The device window: borderless FixedSize SWindow at the display position from
    GetWindowPositionForDisplay (new XPos/YPos in FLGDeviceCalibration), WindowedFullscreen,
    FocusWhenFirstShown(false) + topmost (it opens unfocused, so without topmost it starts
    BEHIND desktop windows sitting on the device). The Bridge window no longer exists, so the
    old click-the-hologram focus-steal problem is gone too; clicks on our window are bounced
    back by KeepGameWindowFocused as before.
  - lkg.SelfRender 0 switches back to the Bridge interop draw path (A/B or fallback).
  - Self-render needs the plugin's own window, so when lkg.SelfRender is on the code FORCES
    PlayMode_InSeparateWindow (StartPlayerSeparateProccess), overriding saved/cooked config -
    necessary because the game's DefaultEngine.ini is baked into the pak and an INCREMENTAL
    cook can ship a stale copy (bit us: the ini said InSeparateWindow but the staged build
    still ran InMainViewport). DefaultEngine.ini says InSeparateWindow too, for consistency.
    Side effect: the main 1280x720 window now renders the world normally (a free 2D spectator
    view) instead of being black. StartPlayer runs at game-viewport creation, which is BEFORE
    PostEngineInit initializes the Bridge, so StartPlayer now calls Bridge.Initialize() on
    demand (made idempotent) - without this the display list is empty and the plugin falls
    back to the 800x800 debug window at 200,200.
  The Unity plugin uses the same Bridge interop, so porting to it would have inherited the
  same stalls; moot now.
- Emulator pacing (LibretroManager::Update) is hybrid sleep+spin: SleepNoStats until ~1.5ms
  before the frame deadline, then spin the rest. It was a pure busy-wait burning a full core;
  the stalls reproduced while that was active AND while idle, so pacing was ruled out as a
  stall cause.
- The LKG build forces the MAIN window to windowed 1280x720 at BeginPlay - Shipping defaults to
  fullscreen, and the focus-bounce guardian was slamming a fullscreen black window over the
  whole main monitor whenever the hologram window was clicked.
- Sprite shadows project onto the DEEPEST layer's plane (the background wall), not the next layer
  back - middle layers are often empty where the silhouette lands, and a shadow floating at an
  unoccupied depth plane looks broken on the device.
- Hotkey 0 toggles the fps cap (vsync + t.MaxFPS) to measure true throughput; 1-5 set frameskip.
- Benign noise: "Failed to load ... LookingGlassCore.dll" at startup is upstream legacy (the DLL
  never shipped); Bridge does the real work.
- The flat camera: `APlayerPawn` owns a `UCameraComponent` root (FOV 14).
  `FitFlatCameraToLayers()` (called at the end of `InitLayers`) captures the layer stack's AABB -
  essential because each system uses wildly different world scales (NES layers are ~41 units,
  Atari ~445 units offset +69 in Y, VB ~310). `UpdateFlatCamera()` (Tick) then orbits the camera
  around that AABB's center: idle mode is an isometric-ish view (pitch -18) with a slow +/-30
  degree yaw sweep; any mouse movement takes over for free 360 orbit (pitch clamped +/-85), and
  5 idle seconds later it blends back to the sweep. The fit distance is recomputed per frame from
  the AABB corners for the current orientation (zoom out is instant so nothing ever crops, zoom
  back in is eased). Orbit speeds/angles/sensitivities are EditAnywhere on APlayerPawn.
  IMPORTANT: `UCameraComponent::FieldOfView` is the HORIZONTAL fov (engine default constraint is
  AspectRatio_MaintainXFOV); vertical fov is derived from the live viewport aspect. Treating it as
  vertical (plus a hardcoded 16:9) was the old bug that cropped the bottom of tall games like
  Castlevania. When the LG plugin is active it takes over the viewport, so the pawn camera is
  inert on hardware.

## Emulator cores (all three work, all dynamic DLLs)

- The patched core sources are VENDORED IN THIS REPO under `cores/` - this is the canonical home now.
  The old scattered trees at `d:\projects\libretro\*` are retired (and the stella one there had
  post-release .bak experiments; do not copy from them again).
  - `cores/fceumm` - NES (GPL-2.0, license in `Copying`). MSVC project in `msvc/`.
  - `cores/stella` - Atari 2600 (GPL-2.0, `License.txt`). Libretro core project at `src/libretro/Stella.vcxproj`.
  - `cores/beetle-vb` - Virtual Boy (GPL-2.0, `COPYING`). Project in `visualstudio/`. Also owns
    `mednafen/vb/HoloVB.h`, the layer ABI struct; the game module includes it straight from there
    (see HoloVCS.Build.cs include path), so there is exactly one copy.
- `BuildCores.bat` builds all three (msbuild, Release x64, v143 toolset) into `cores/_built/` and
  copies the DLLs to `Binaries/Win64`. Verified building with VS2026's MSVC 14.44 and running on UE 5.8.
- `LibretroManager::LoadCore` loads them with LoadLibraryA - bare name first (packaged builds, exe
  sits next to the DLLs), then `<ProjectDir>/Binaries/Win64/` (editor runs).
- **NEVER statically link the cores into the game module again.** GPL-2.0 and the Unreal Engine
  license cannot coexist in one binary. The v1.3-era static NES experiment was removed for this
  reason (the source moved from Source/HoloVCS/nes_core_src to cores/fceumm/src, keeping git history).
- Known-good v1.2-era DLLs remain recoverable from git history and the gitignored `prebuilt_cores/`.
- Stella hacks are also captured as `StellaModifications/StellaModification.dif`.
- ROMs go in `atari2600/`, `nes/`, `vb/` in the project root. ROM files are gitignored
  (`*.nes`, `*.a26`, `*.vb`); never commit commercial ROMs.

## How the 3D trick works (short version)

`GameProfileManager` (keyed by ROM MD5) re-renders each emulator frame N times with different
render-plane masks (custom env vars `stella_video_flags` / `fceumm_video_flags`), using in-RAM save
states to rewind between passes, and blits each pass to a different depth layer with colorkey alpha.
Virtual Boy is different: the patched beetle-vb returns pre-split layers via a nonstandard
`retro_video_refresh_callback_ex`. `NesHacker` edits NES nametables inside the save-state buffer to
split backgrounds. Startup ROM is hardcoded by partial name ("astle") near the top of
LibretroManager.cpp.

## Gotchas learned the hard way

- `g_pLibretroManager` is a raw global set in `LibretroManager::Init` and cleared in the destructor.
  The destructor must only clear it when it is the owner (`if (g_pLibretroManager == this)`), because
  stale LibretroManager instances inside actor CDOs get GC-purged (first purge is ~61s in) and used
  to null the global under the live instance, crashing the per-frame input handlers.
- UE 5.8 IWYU is strict: most 5.6-to-5.8 compile fixes were adding missing includes
  (Engine/Texture2D.h, RHITypes.h, Components/InputComponent.h, Components/MeshComponent.h,
  Materials/Material.h, Engine/Engine.h, HAL/FileManager.h, Misc/FileHelper.h). The Shipping/game
  build has a leaner include graph than the editor build and needs its own pass (fwd-declare in
  headers, include in cpps).
- `ISoundGenerator::GetNumChannels()` is `const` in 5.8; a non-const override compiles but silently
  never gets called (audio reports 0 channels).
- Target.cs files must use `DefaultBuildSettings = BuildSettingsVersion.V7` on 5.8 or the editor
  shows a "Target Upgrade Required" popup on every launch.
- `Config/DefaultEngine.ini` has `Compiler=Default` under WindowsTargetSettings; do not pin a VS
  version there (the machine has VS2026, and pinning VisualStudio2022 breaks the build).
- The map instance's stored root transform overrides class-default component transforms; the flat
  camera ignores that by setting an absolute world transform every Tick (UpdateFlatCamera).
- Third-party headers that include windows.h/Winsock2.h must be wrapped in
  `Windows/AllowWindowsPlatformTypes.h` + `Windows/HideWindowsPlatformTypes.h`. A hand-rolled
  #undef list is not enough on 5.8: winnt.h's InterlockedAdd macro breaks engine headers
  (TypedElementData.h) later in the same unity TU.
- After a crash of a launched game/editor instance, CrashReportClientEditor.exe keeps the crashing
  module's DLL open and the next link fails with LNK1104; kill that process before rebuilding.
- A running editor -game instance blocks Build.bat with "Unable to build while Live Coding is
  active" - kill all UnrealEditor processes before building.
- Looking Glass panels are horizontal-parallax-only hardware: the quilt is a horizontal camera
  sweep, so vertical head movement never changes the view. Not a bug.
- An editor-binary `-game` run has WITH_EDITOR code compiled in but GUnrealEd == nullptr; any
  WITH_EDITOR block reachable in game mode must null-check it.
- The UAT cook (`BuildCookRun`) uses the EDITOR plugin/game DLLs and `-nocompileeditor` does NOT
  rebuild them: after touching plugin or game module code, build HoloVCSEditor before cooking or
  the cook runs stale code.
- Code that runs at PostEngineInit runs in the COOK COMMANDLET too; there is no Slate application
  there (`FSlateApplication::Get()` asserts and kills the cook). Guard with
  `FSlateApplication::IsInitialized()` - this is why the LookingGlass runtime module has that check.
- `bUseLoggingInShipping` cannot be enabled with the installed engine (Core.lib is prebuilt with
  NO_LOGGING; LogTemp fails to link). Debug packaged-build startup problems with the game's own
  log.txt (written next to the top-level exe) and remember it only starts after the game module
  loads - death before the first line means engine-init-level failure (bad boot map, missing pak
  content, etc).
- PrintWindow LIES about the "Looking Glass Bridge Window" (returns solid white - it's a DX
  swapchain that ignores WM_PRINT). To verify device output, CopyFromScreen the Portrait's desktop
  region instead (it was at 3440,0 size 1536x2048 last time; enumerate screens if it moved). This
  cost hours of chasing phantom all-white output that was actually rendering fine.
- Looking Glass debug recipes (editor -game runs accept -ini overrides, packaged Shipping does not):
  raw quilt in a desktop window = `-ini:Engine:[/Script/LookingGlassRuntime.LookingGlassSettings]:LookingGlassRenderingSettings=(bVsync=True,QuiltMode=True,bRender2D=False)`
  plus `...LookingGlassWindowSettings=(PlacementMode=AlwaysDebugWindow,...)` to bypass Bridge
  entirely (that window screenshots correctly).

## Testing

- When possible, design automated tests for new features and bug fixes.
- Run relevant automated tests after finishing changes to guard against regressions.
- If tests cannot be run or do not exist, state that clearly in the handoff and describe any manual verification performed.
- Current reality: this project has no automated tests. Manual verification is the burst workflow below.

## Verifying the game (AI agents take note)

Do quick bursts, not long soaks: launch, screenshot the specific emulator/screen you are checking,
send the ROM-cycle keys ("," / ".") for the next one, screenshot, then KILL the process. The whole
pass should take well under a minute. Never leave a game instance running while doing other work
(Seth's machine, Seth's GPU). The staged/shipping build writes a Proton-style log.txt next to the
top-level exe; the editor build logs to Saved/Logs/HoloVCS_Flat.log.

## Computer Control

- Never take over or control the user's desktop without express permission in
  the current request. This includes Computer Use, desktop UI automation,
  SendInput, clicking, typing, or any other mechanism that controls visible apps.
- Permission to complete a task, inspect an app, compare behavior, or proceed
  autonomously does not imply permission to control the desktop.
- Standing exception granted by Seth: the game verification bursts above (foregrounding the HoloVCS
  game window, sending its hotkeys, screenshotting it, killing the process). Nothing beyond that.

## Security

- Never commit sensitive data, including credentials, tokens, passwords, private keys, cookies, customer data, personal data, or machine-specific authentication material.
- If an AI assistant needs authentication data or other secrets for local work, use `agents_secret.md` for those notes.
- `agents_secret.md` must stay ignored by git and must not be committed.
- Do not put secrets in commit messages, logs, issue text, pull request descriptions, generated docs, or other tracked files.
- Before committing, review staged changes for accidental secrets.
- This repo's specifics: Android signing (KeyStore path/alias/passwords) and the AndroidFileServer
  SecurityToken live in `Config/UserEngine.ini`, which is gitignored. NEVER put them in
  Config/DefaultEngine.ini or any tracked file; this repo has a public GitHub remote. The keystore
  file itself lives outside the repo (`D:\projects\protonGITFull\AppleStuff\android\`) and must stay
  out of git everywhere.

## Git

- Create a local git repo and use it.  Commit features/etc as needed.
- Never add OpenAI/Codex/Claude etc as a co-author on git commits.
- NEVER `git push` unless explicitly told to push. "Commit" means commit locally only; committing is not permission to push.
- Commits and PRs are authored by Seth only; no AI attribution lines anywhere (no Co-Authored-By, no "Generated with" footers).

## Packaging / release

- `PackageWin64Release.bat` packages the FLAT version for UE 5.8 (stages into `dist\win64_release\Windows`,
  scrubs ROMs before zipping, signs binaries, produces HoloVCS_Win64.zip). It needs
  `..\base_setup.bat` (defines PROJECT_DIR, RT_PROJECTS, PROTON_DIR) and `app_info_setup.bat`
  (APP_NAME/APP_DIR/UE_DIR).
- `BuildAndRunWin64Release.bat` is the local test loop: incremental Shipping cook/stage to
  `dist\win64_test` with roms included (never distribute that folder), then launches it.
- `BuildAndRunWin64LKG.bat` is the same loop for the Looking Glass hardware build (HoloVCS.uproject,
  target HoloVCSLKG, boots /Game/Maps/NewMap): stages to `dist\win64_lkg_test` (top exe
  `HoloVCSLKG.exe`), then launches. Pass `nolaunch` to skip the launch. Never run the two stage
  bats concurrently - they share Saved\Cooked (alternating flavors just recook, which is safe but
  slower).
- Standing preference (Seth): after finishing code changes, ALWAYS build the relevant Shipping
  test build (BuildAndRunWin64Release.bat / BuildAndRunWin64LKG.bat). file:/// links to exes are
  NOT clickable in the VSCode chat panel (security) - don't bother with them. Instead, END THE
  HANDOFF BY LAUNCHING the staged build so it's already running for Seth (kill any instance you
  launched for verification first, then launch fresh as the final action). Also print the plain
  path for reference. `RunLKG.bat` in the project root launches the staged LKG build manually.
- `UploadReleaseToRTsoft.bat` SCPs the zip to rtsoft.com.
- The Android port was dropped entirely (scripts, config, and the static-core hack that existed for
  it). If it ever comes back, the cores must ship as separate .so files there too, same GPL reason.

## Writing style for this repo

No em-dashes in prose, commits, or docs.

## Asset creation tools

- If seths_game_asset_creation.txt exists in the same folder as this file, read it
