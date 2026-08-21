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
- Hardware-tested on the Looking Glass Portrait, Looking Glass Go, and original 8.9-inch Looking
  Glass. The Aug 2026 port validation and performance tuning were performed on the Portrait, where
  the hologram renders full-size on the device. Two more fixes were needed beyond compiling:
  - The map's capture actor was placed/sized for the old 5.6-era world scale. The game module now
    auto-fits it to the layer AABB on every layer rebuild (FitLookingGlassCaptureToLayers in
    LibretroManagerActor.cpp - moves the actor to the AABB center, calls SetSize via reflection,
    and puts the capture in PRM_UseShowOnlyList with only the "Layers"-tagged actors so the pawn's
    tint plane and other scene junk can never leak into the hologram). `-lkgsize=N` on the command
    line overrides the computed capture size for tuning.
  - The plugin fork's show-only path was broken upstream (added show-only prims to HiddenPrimitives,
    fixed in LookingGlassSceneCaptureRendering.cpp).
  The F9 quilt-screenshot hotkey did not respond in -game main-viewport mode; use the
  `lkg.SaveQuilt` console command instead (added Aug 2026, works from the automation harness via
  `exec lkg.SaveQuilt`, writes to Saved/Screenshots - see docs/automation_workflow.md).
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
  SendKeys after the VB core loaded; unresolved, use -rom= instead (or the harness
  `key Comma`/`key Period` commands, which need no window focus at all). The hardcoded filename
  fragment and default index are only preferences: when absent or out of range, startup falls back
  to the first discovered ROM so renamed files can still be recognized by checksum after loading.
- FPS counter: hardware builds show FPS on the in-world status text (5x size) once per second and
  log "N FPS" to the log; the plugin also logs per-phase frame times ("LKG frame phases") once a
  second from the viewport client.
- PERFORMANCE: SOLVED, locked 60 fps in normal play (vsync/pacing cap); ~250 fps measured
  uncapped (hotkey 0) on the Portrait, vs ~13 fps max before. Via the custom sprite-quilt renderer
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
- HELP SCREEN (Aug 2026, replaces the old bitmap splash): dynamically drawn from
  HelpScreen::BuildHelpText (Source/HoloVCS/HelpScreen.cpp - the single source of truth for the
  key list; update it whenever hotkeys change). The ? key (Slash) opens it; ANY key, button or
  movement closes it (every pressed-input handler starts with a HelpSwallowedInput() guard so
  the dismissing key can't also save state/switch rom/etc, plus a non-consuming EKeys::AnyKey
  catch-all for unbound keys; same-frame show/hide guards in HelpScreen keep the two from
  fighting). It PAUSES the emulator while up (closing restores the pre-open pause state, and
  any external unpause auto-closes it), and the game is FULLY HIDDEN while it's up: AHoloHUD
  draws an opaque cover and RenderSpriteQuilt skips the layer draws (game and help used to
  obscure each other). Auto-shows on the first live frame in SHIPPING builds only, so dev runs
  and the automation harness never boot paused (harness command `help [on|off]` toggles it
  headlessly). Rendering: flat window via AHoloHUD (installed at runtime with ClientSetHUD, no
  GameMode changes); hologram via per-tile canvas text in RenderSpriteQuilt that reads the
  text from the "HelpScreen"-tagged carrier actor in the show-only list (identical per tile =
  screen-locked). The old "SplashScreen"-tagged map actor is destroyed at BeginPlay in ALL
  builds now (the umaps keep the inert actor; editing them is a one-way door).
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
  (default 0.65: 0.6 read as a brown twin of the Pitfall barrel, 0.8 was "pretty black")
  and lkg.SpriteShadowSoft (default 2) are the shadow cvars. SOFTNESS: the 512px caster union
  is halved N times through a bilinear 2:1 copy chain (exact 2x2 box filters, AlphaBlend onto
  a cleared RT is a straight alpha copy) and the small result is sampled bilinearly back into
  the mask, so the silhouette edge spreads over 2^N mask texels. 0 = the old pixel-exact
  silhouette, 2 = 128px mask, 3 = 64px (very soft, thins the Pitfall player's shadow). A
  multi-tap blur is NOT possible with FCanvas: no canvas blend mode accumulates alpha
  additively (Additive/Modulate are CW_RGB only) and the holdout math needs the value in
  alpha. 2D scene shadows
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
- TEST SPRITE-PATH CHANGES ON THE DEVICE, NOT THE 2D WINDOW: Seth: the flat 2D build's
  lighting/shadows look completely different from the hologram and are useless for judging
  it. Quick loop: `UnrealEditor.exe HoloVCS.uproject -game -rom=x` (editor binary, ProjectSavedDir
  = <project>\Saved) then `holo_auto.ps1 -Cmd "exec lkg.SaveQuilt"`. Faithful loop: `BuildAndRunWin64LKG.bat nolaunch`, run `dist\win64_lkg_test\Windows\HoloVCSLKG.exe
  -rom=x`, `holo_auto.ps1 -SavedDir C:\Users\Seth\AppData\Local\HoloVCS\Saved -Cmd "help off","exec
  lkg.SaveQuilt"`, then `-CropQuilt` and zoom a crop. GOTCHA (fixed Aug 2026): the editor-binary
  run used to render every sprite-path layer BLACK in the quilt (only a "Text" label showed).
  CAUSE: the help actor's UTextRenderComponent
  defaulted to "Text", and the sprite path treats any non-empty HelpScreen text as help-up
  (layers skipped); Shipping hid it because the boot auto-show/hide cleared the text. The
  constructor now clears it. ALSO: `holo_auto.ps1 -Cmd quit` can leave the old instance alive
  for many seconds; two instances share commands.txt and whichever grabs a command runs it, so
  `Get-Process HoloVCS*` before trusting a capture (a stale instance with lkg.ShowFPS 0 produced
  a false "no FPS in Shipping" report in Aug 2026 - the FPS overlay works fine in Shipping).
- Sprite shadows project onto the DEEPEST layer's plane (the background wall), not the next layer
  back - middle layers are often empty where the silhouette lands, and a shadow floating at an
  unoccupied depth plane looks broken on the device.
- Hotkey 0 toggles the fps cap (vsync + t.MaxFPS) to measure true throughput; 1-5 set frameskip.
- Hotkeys [ and ] scale the 3D depth spread live (m_userDepthScale on ALibretroManagerActor,
  multiplies m_total3dDepth, survives rom switches; ApplyLayerDepth re-spreads the existing
  layer actors absolutely and re-runs both camera/capture fits - no InitLayers hitch). Console
  twin for the harness: `holo.DepthScale <mult>` (clamped 0.2-5.0). NES Select is Tab now (was
  Backslash), and the old A-key "auto adjust audio" hotkey is REMOVED (it also collided with
  WASD left; per-frame audio stats code went with it).
- AUDIO (Aug 2026): the libretro batch callback (LibretroManager.cpp) feeds mono float chunks to
  RTBufferGenerator with DYNAMIC RATE CONTROL: each chunk is linearly resampled by up to +/-0.5%
  so the queue hovers at AUDIO_TARGET_QUEUED_SAMPLES (2400 = 50ms at 48k); GetSamplesQueued() is
  an atomic that already counts scheduled-but-unconsumed chunks. Do NOT bring back a hard
  "skip the frame if the buffer is over N" gate: the CPU pacing clock and the DAC clock drift,
  so a plain gate drops a whole frame (or underruns) every few seconds = audible clicks, and a
  bigger buffer only changes how often. The x4 hard drop that remains is a stall safety valve.
  DefaultEngine.ini runs the mixer at 1024-frame callbacks with 1 buffer enqueued (low latency);
  raise AudioNumBuffersToEnqueue to 2 first if glitches ever come back on a slow machine.
  THE ACTUAL CLICK SOURCE (found Aug 2026, a ~90ms game-thread stall every 5.0s): UE's
  DefaultViewportMouseLockMode=LockOnCapture re-clips the cursor every frame while the cursor sits
  inside the game window (60 WM_MOUSEMOVE/s with nobody touching the mouse), and Windows answers
  that with a ~80-120ms stall in a USER32 syscall every 5 seconds. Since the core (hence audio) only
  runs on the game thread, every stall was a hole in the sound. It only reproduces with the cursor
  INSIDE the game window (which is where it is after you click the window to play), which is why
  harness runs with the cursor parked elsewhere looked clean. Fix: DefaultInput.ini
  DefaultViewportMouseLockMode=DoNotLock (the game never uses the mouse). Proven A/B in -game:
  LockOnCapture 8 stalls/30s, DoNotLock 0. Do not set it back to LockOnCapture.
  Hardening that went in alongside (keep it): the pacer deadline is phase-locked (+= n*interval;
  the old "= now" accumulated sleep overshoot and beat against vsync), Update runs however many
  emulator frames are owed by the wall clock (cap 3, resyncs after a longer stall/pause) so a
  late tick doesn't starve audio, and the audio feed uses rate control (above). Diagnostics that
  stay: the per-second FPS log line reports "audio: N queued, N underruns, N dropped, N catch-up
  frames" (healthy play = 0/0/0; the help-screen pause legitimately logs ~48 underruns/s since
  no audio is produced) and any tick gap over 35ms logs "STALL at <t>: ... (emu / pace wait /
  engine-other)". The stat dumphitches trick (-ExecCmds="t.HitchFrameTimeThreshold 40, stat
  dumphitches" on an editor -game run) showed the stall as FrameTime "Self": in 5.8 neither
  PumpMessages nor PollGameDeviceState has a stat scope, so "Self" there means the message pump
  or gamepad polling.
- BRIDGE DISCOVERY (Aug 2026, laptop field bug): the in-process Bridge SDK (`ThirdParty/.../bridge.h`)
  finds `bridge_inproc.dll` ONLY via the PER-USER `%APPDATA%\Looking Glass\Bridge\settings.json`
  `install_locations` list, which the installer writes only for the account that ran it. On a
  laptop where Bridge was installed from another account, Bridge ran and showed the display fine
  but our plugin got nothing and silently fell back to the 800x800 debug quilt window (the old
  HoloPlayCore build was immune: it talks to the Bridge service over a socket). Fix in
  `LookingGlassBridge.cpp` Initialize_BridgeThread: candidates tried in order are `-lkgbridgedir=<dir>`,
  settings.json (skip with `-lkgnosettings` to test the fallbacks), the folder of a RUNNING
  LookingGlassBridge.exe (toolhelp snapshot), then a `<Program Files>\Looking Glass\Looking Glass Bridge *`
  scan (highest version first); each real install gets 3 initialize_bridge attempts 1s apart (the
  service may still be starting at login). SHIPPING DIAGNOSTICS: UE_LOG is compiled out of Shipping,
  so `FLookingGlassBridge::Diag()` appends to `lkg_diag.txt` next to the top-level exe (same file as
  the stall log): every boot writes a "---- Bridge boot ----" block with the settings.json verdict,
  each candidate tried, Bridge version, every display (name/serial/type/size/pos/calibration), the
  placement decision (self-render window vs "NO DEVICE ... debug quilt window") and the Automatic
  tiling choice with its reason. Ask users for that file first. Raw-quilt-in-a-desktop-window ALWAYS
  means `Bridge.Displays` was empty at viewport creation. GetAutomaticTilingQuality now also matches
  the device Name and falls back by panel orientation (landscape = legacy 8.9" preset) instead of
  always assuming Portrait; Bridge 2.6.3 reports the original 8.9" as name `8.9" Looking Glass`,
  serial `LKG-2K-xxxxx`.
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
- The generated `Binaries/Win64/*libretro.dll` files are intentionally ignored and must not be
  tracked or force-added. Build them from the vendored sources with `BuildCores.bat`; release
  packaging rebuilds them before staging. The core source and build recipes, not compiled DLLs,
  are the repository artifacts.
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

Feature docs (read before working on these):
- Automation harness (file-based game control/screenshots/video, no window focus), the NES
  state dump tool (N key), and the NES profile-authoring method: `docs/automation_workflow.md`.
  Also covers the UE 5.8 "Unreal MCP" editor plugin (enabled in the FLAT uproject only).
- Legend of Zelda profile (first profile authored with the automation workflow, Aug 2026):
  tile IDs, layer scheme, palette gotcha, and the same-frame PPU tile-ID map that keeps scrolling
  transitions 3D are in that same doc. Obstacle keep-list is overworld-only so far; dungeons/rocks pending.

## Gotchas learned the hard way

- Blit passes in game profiles must use CONTIGUOUS indices starting at BLIT_PASS0: the video
  refresh callback (LibretroManager.cpp, retro_video_refresh_callback) BREAKS at the first
  inactive pass, so anything set up after a gap silently never blits (bit the Zelda subscreen
  split: with the subscreen closed, PASS1 was skipped and the PASS2 ground/sprite blits
  vanished). Use a running `pass++` index when passes are conditional.

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

USE THE AUTOMATION HARNESS: the game watches `Saved/Automation/commands.txt` for commands
(screenshots, emulated joypad input, rom switching, per-layer texture dumps, video, console exec)
and none of it needs window focus - drive it via `tools\holo_auto.ps1`. Full command list and
workflow: `docs/automation_workflow.md`. Never foreground the game window or send desktop
keystrokes while Seth is using the machine; the launch itself is the only unavoidable focus grab.
Wait for "harness ready" in Saved/Automation/ai_log.txt after launching.

Staged SHIPPING builds boot PAUSED under the auto-shown help screen: send `help off` first or ALL
emulator input looks dead (`press` holds sit pending while paused). log.txt logs every
`SetGamePaused` transition, so a wrongly-stuck pause is visible there instead of masquerading as
broken controls (that misread cost a whole debugging session in Aug 2026; the actual staged binary
was also 8 minutes older than the commit being tested, so ALWAYS restage before judging behavior).
The harness `key <FKeyName> [ticks]` command drives the REAL keyboard path (Slate -> viewport ->
PlayerInput -> pawn bindings) with no window focus needed; `press` bypasses UE input entirely.
Keyboard-to-NES mapping verified end-to-end with it (Space=A jump, Ctrl=B whip, Enter=Start,
Castlevania on-screen proof), Aug 2026.

Do quick bursts, not long soaks: launch (`-rom=partial` picks the game), check via harness
screenshots, then KILL the process. Never leave a game instance running while doing other work
(Seth's machine, Seth's GPU). A running -game instance also blocks Build.bat. The
staged/shipping build writes a Proton-style log.txt next to the top-level exe; the editor build
logs to Saved/Logs/HoloVCS_Flat.log. `savestate`/`loadstate` harness commands write/load
`saves/<system>/<rom>.sav0` checkpoints (loading migrates legacy top-level `<rom>.sav0` files into
saves/) - use them to skip menus after a relaunch (Zelda has one at the
overworld start).

## Computer Control

- Never take over or control the user's desktop without express permission in
  the current request. This includes Computer Use, desktop UI automation,
  SendInput, clicking, typing, or any other mechanism that controls visible apps.
- Permission to complete a task, inspect an app, compare behavior, or proceed
  autonomously does not imply permission to control the desktop.
- Standing exception granted by Seth: launching and killing HoloVCS game processes for the
  verification bursts above. Everything else goes through the automation harness file channel
  (docs/automation_workflow.md) - do NOT foreground the game window or synthesize desktop
  keyboard/mouse input; Seth is often using the machine while agents work (his request, Aug 2026).

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
  out of git everywhere. Unreal stages user config by default, so `Config/DefaultGame.ini` must keep
  `+DisallowedConfigFiles=HoloVCS/Config/UserEngine.ini` under `[Staging]`; release packaging also
  verifies that `UserEngine.ini` is absent from the cooked pak.

## Git

- Create a local git repo and use it.  Commit features/etc as needed.
- Never add OpenAI/Codex/Claude etc as a co-author on git commits.
- NEVER `git push` unless explicitly told to push. "Commit" means commit locally only; committing is not permission to push.
- Commits and PRs are authored by Seth only; no AI attribution lines anywhere (no Co-Authored-By, no "Generated with" footers).
- All work happens on `main`. NEVER create, switch to, or rename a branch without asking Seth first, and never leave the repo checked out on anything but `main`. 

## Packaging / release

- BAT PAUSE CONVENTION (Aug 2026): every .bat in the project root pauses when it FINISHES and when
  it FAILS, so a double-clicked window stays open long enough to read the output. Define `NOPAUSE`
  to skip every pause: `cmd /c "set NOPAUSE=1 && PackageWin64LKGRelease.bat"`, or `$env:NOPAUSE=1`
  first in PowerShell. ALWAYS set it when an AI or any other script runs these, otherwise the run
  hangs on a "Press any key" prompt with no visible reason.
- The shared implementation is `PauseHelper.bat`; scripts `call "%~dp0PauseHelper.bat"` instead of
  using `pause` directly. Each script ends with a success message plus `exit /b 0`, and has a
  `:fail` block that prints a FAILED banner, pauses, and exits 1; error checks `goto :fail` rather
  than `echo something && exit /b 1`. `RunLKG.bat` is the one exception to the success pause: it has
  nothing to report when it works, so it pauses only when the staged build is missing.
- When one of these scripts calls another (both test bats and the LKG release script call
  `BuildCores.bat`), the caller sets `HOLO_BAT_NESTED=1` around the call so the child does not pause
  on top of the parent's own pause, and stashes ERRORLEVEL into a variable BEFORE clearing that flag,
  because `set` clobbers ERRORLEVEL.
- `NOPAUSE` (ours) is not `NO_PAUSE` (the signing helper's, see below). Keep them straight: the LKG
  release script sets NO_PAUSE=1 for signing while its own NOPAUSE pause is still wanted.
- Gotcha with the old `:comment` line style used in these bats: the first word after the colon is a
  real label, so never start such a comment with "Fail" or "Done" or `goto :fail` jumps into the
  comment instead of the error handler. Use `rem` for those lines.
- Scripts call helpers as `call "%~dp0app_info_setup.bat"` so they work from any working directory
  (a plain `call app_info_setup.bat` fails in shells that set NoDefaultCurrentDirectoryInExePath,
  which is how AI-driven terminals often run).

- Keep these version references synchronized whenever bumping a release:
  `Config/DefaultGame.ini` (`ProjectVersion`, four-part form),
  `Source/HoloVCS/LibretroManager.cpp` (`G_VERSION_STRING`), and every version reference in the
  packaged `readme.txt` (including its title and any versioned section headings). Search the tracked
  tree for the old version afterward so a stale user-facing value is not missed.
- `README.md` has a `Latest versions` section. For every version bump, add the new version and its
  release date at the top of that section, followed by a short user-facing summary of what changed.
  Keep older entries below it as release history. Use the actual release date in `Month D, YYYY`
  form, not the build date of an earlier test package.
- After synchronizing the version and README entry, rebuild the appropriate Shipping release and
  release zip so the staged executable, packaged readme, and archive all contain the new version.
  Uploading or pushing still requires Seth's explicit permission.
- `PackageWin64Release.bat` packages the FLAT version for UE 5.8 (stages into `dist\win64_release\Windows`,
  scrubs ROMs before zipping, signs binaries, produces HoloVCS_Win64.zip). It needs
  `..\base_setup.bat` (defines PROJECT_DIR, RT_PROJECTS, PROTON_DIR) and `app_info_setup.bat`
  (APP_NAME/APP_DIR/UE_DIR).
- `BuildAndRunWin64Release.bat` is the local test loop: incremental Shipping cook/stage to
  `dist\win64_test` with roms included (never distribute that folder), then launches it. Both test
  bats preserve save states (F/G hotkeys (L also loads) write `saves\<system>\<rom>.sav0` since Aug 2026; legacy
  top-level `*.sav0` files are migrated into saves/ when loaded) across restages via a
  dist\savstate_keep_* backup; PackageWin64Release.bat scrubs both from real releases.
- `BuildAndRunWin64LKG.bat` is the same loop for the Looking Glass hardware build (HoloVCS.uproject,
  target HoloVCSLKG, boots /Game/Maps/NewMap): stages to `dist\win64_lkg_test` (top exe
  `HoloVCSLKG.exe`), then launches. Pass `nolaunch` to skip the launch. Never run the two stage
  bats concurrently - they share Saved\Cooked (alternating flavors just recook, which is safe but
  slower).
- `PackageWin64LKGRelease.bat` creates the signed Looking Glass distribution in
  `dist\win64_lkg_release\Windows` and the legacy public-download filename `HoloVCS_Win64.zip`.
  It rebuilds all cores, forces the LKG editor modules to rebuild before cooking, copies only ROM
  directory placeholder text files, includes project/plugin/core licenses, and fails if a ROM or
  save state reaches the release stage. It also repairs UE 5.8's out-of-bounds certificate-table
  entry in the generated bootstrap exe before Authenticode signing. It does not upload anything.
- Standing preference (Seth): after finishing code changes, ALWAYS build the relevant Shipping
  test build (BuildAndRunWin64Release.bat / BuildAndRunWin64LKG.bat). file:/// links to exes are
  NOT clickable in the VSCode chat panel (security) - don't bother with them. Instead, END THE
  HANDOFF BY LAUNCHING the staged build so it's already running for Seth (kill any instance you
  launched for verification first, then launch fresh as the final action). Also print the plain
  path for reference. `RunLKG.bat` in the project root launches the staged LKG build manually.
  Run every one of these bats with NOPAUSE set (`$env:NOPAUSE=1` in PowerShell, `set NOPAUSE=1` in cmd) so
  the end-of-script pause does not hang the automated run.
- `UploadReleaseToRTsoft.bat` SCPs the zip to rtsoft.com.
- The shared signing helper (`%RT_PROJECTS%\Signing\sign.bat`) ends with a `pause` unless the
  `NO_PAUSE` env var is non-empty; its ARGUMENTS cannot suppress that (a "nopause" 4th arg used to
  be passed and did nothing - each signed file silently waited for an ENTER because the helper's
  output is redirected to nul). Both release scripts set NO_PAUSE=1 before signing. The helper also
  echoes its hardware-token PIN, which is why its output stays suppressed.
- MSVC RUNTIME SHIPPING (Aug 2026, after a laptop hit a dead-end "MS Visual C++ redistributable is
  missing" popup): all four stage scripts (both Package*Release bats and both BuildAndRun* test
  bats) pass `-prereqs` AND `-applocaldirectory=<engine>\Engine\Binaries\ThirdParty\AppLocalDependencies`
  to BuildCookRun. `-prereqs` stages `Engine\Extras\Redist\en-us\vc_redist.x64.exe`; UE's generated
  bootstrap exe (the top-level exe) ALWAYS checks the installed VC++ runtime at launch and, when the
  check fails, offers to run that bundled installer then launches the game - without `-prereqs` the
  same check shows an unfixable error popup instead, which is exactly the bad experience. The
  app-local flag additionally copies the Microsoft CRT/UCRT dlls next to the Shipping exe (both
  staged Binaries\Win64 dirs) so the game and the libretro cores also run when launched directly
  with no runtime installed. Both release scripts fail if vc_redist or the app-local msvcp140.dll
  did not stage. The Microsoft dlls and installers are already Microsoft-signed; do not re-sign them.
  Both test bats accept `nolaunch` as the first argument now.
- The README's NES demo video is hosted at `https://www.rtsoft.com/files/HoloNes.mp4`; its source
  master is `U:\Personal Pics\MoviesFinished\HoloNes.mp4`, and the checked-in clickable thumbnail
  is `Media/holones_video_thumb.jpg`. Upload replacements through
  `%RT_PROJECTS%\UploadFileToRTsoftSSH.bat <file> files`; suppress helper output because publishing
  helpers may contain or echo credentials.
- The Android port was dropped entirely (scripts, config, and the static-core hack that existed for
  it). If it ever comes back, the cores must ship as separate .so files there too, same GPL reason.

## Writing style for this repo

No em-dashes in prose, commits, or docs.

## Asset creation tools

- If seths_game_asset_creation.txt exists in the same folder as this file, read it
