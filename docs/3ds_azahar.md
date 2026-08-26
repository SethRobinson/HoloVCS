# 3DS support via Azahar (in progress)

Status Aug 2026: HOLO MODE WORKS. The patched core (fork `holo` branch) runs its own
offscreen WGL 4.3 context, reads the top screen's color + depth back each frame, slices
into N depth-banded 400x240 layers on the CPU and delivers them through the new
`retro_set_video_refresh_holo` export (v2 layer ABI, `cores/holo_abi/holo_layer_abi.h`,
canonical copy synced with the fork). Verified in the flat build: world-map diorama with
correct depth ordering and orientation; layer dumps clean. A stock (unpatched) core still
falls back to the flat software-renderer path automatically.

Hardware-validated on the Looking Glass Go (Aug 2026). First-round fixes after Seth's
on-device feedback (jitter + black seams), all in the fork's holo_slicer/holo_hook:
- Color+depth now read as a SAME-FRAME pair from the render framebuffer's own attachments
  (reading the display texture paired stale color with fresh depth = motion jitter).
- Readback goes through round-robin PBOs consumed one present later; a synchronous
  glReadPixels of the live frame stalled the GL pipeline (60fps -> 28fps).
- Depth persists across depthless frames (frames without a depth-tested 240x400 draw used
  to flat-fallback, alternating flat/sliced every frame = the "every other frame wrong"
  jitter); flat fallback only after 60 depthless frames.
- Band range expands instantly, contracts slowly (edge-pixel layer flicker).
- Band overlap (kBandOverlap 0.35): edge pixels duplicate into the adjacent band's empty
  slots, closing the black seams where cut strips of continuous surfaces meet.

Round 2 (Aug 2026, same day): color capture moved to the COMPOSED frame (renderer output
into a core-owned offscreen FBO; the hidden window's default framebuffer fails GL pixel
ownership and reads back black) - correct for every scene type, killed the 30Hz
good/broken strobe. Depth persists across the game's internal 30fps cadence. Then:
- ANALOG works: JoyPadButtonStates carries stick axes (-1..1); MoveX/MoveY/RMoveX/RMoveY
  feed them, the input callback serves RETRO_DEVICE_ANALOG (left = circle pad, right =
  C-stick), harness `press left/right/up/down` folds in as full deflection so scripted
  gameplay walks. Keyboard WASD/arrows and gamepad dpad give full deflection; real sticks
  are true analog. X button enabled for non-VB systems (C key / gamepad X).
- BOTTOM SCREEN: ABI v3 adds info->bottom (composed 320x240, opaque); the frontend shows
  it on a dedicated quad at m_layerInfo[GetLayerCount()], parked one screen-height below
  the top-screen stack at mid depth (great fit on the portrait Go). ApplyLayerDepth skips
  entries past GetLayerCount().
- Depth scale reaches a TRUE 0 now ([ key / holo.DepthScale; tiny epsilon spacing keeps
  the 2D view from z-fighting); ] climbs back out of 0.
- 3DS game saves persist across restages: the env callback answers GET_SAVE_DIRECTORY
  with <root>/saves, which the test bats already preserve (core keeps its user dir at
  saves/Azahar/).

Round 3 (Aug 2026): touch input (mouse or right stick moves a bottom-screen cursor,
click / right trigger / stick-click taps; ClipCursor confines the mouse), START-held
gamepad hotkey combos, Q/E shoulder keys. Input split fix: on the 3DS the d-pad and the
circle pad are SEPARATE controls (SM3DL turns the camera with the d-pad), so the gamepad
d-pad now has its own DPadX/DPadY axes and only those set the digital d-pad bits there;
stick and keyboard reach the game through the analog circle pad alone. Other emulators
still merge stick/keys/d-pad into the digital d-pad. Harness `press up/...` feeds only
the circle pad on 3DS. Verified full 400px top-screen width reaches the display (core
composite dump vs quilt center tile: edge content present, margins both sides); the
"Mario drifts offscreen" report was the d-pad/camera double-drive, not a crop.

Round 4 (Aug 2026): layer count raised 12 -> 30, then settled at 24 after 30 dropped fps
on the device (LibretroManager.cpp SetEmulatorData; the count is negotiated to the core
via the holo_3d_layer_count option, and the core-side clamp in holo_slicer.cpp Init is
now [2, 32] - raising past 32 needs a core rebuild).
C_MAX_LAYERS went 30 -> 40 because the bottom-screen quad indexes
m_layerSetupInfo[GetLayerCount()], which was out of bounds at exactly 30 layers.
Verified in the flat build: SM3DL title screen populates 22+ distinct bands including the
deepest (a clamped-24 core would leave layers 0-5 empty). Gamepad face buttons now map by
PHYSICAL POSITION on 3DS (PlayerPawn JoyPad_X/Y handlers are key-aware): pad top = 3DS X,
pad left = 3DS Y, and the pad-left "extra B" ini mapping (NES run-on-X ergonomics, kept
for the other systems) is suppressed for 3DS - it was making the pad X button jump.
Keyboard Z/C keep their libretro ids (Z = 3DS Y, C = 3DS X).
Frameskip (keys 2-5) works on 3DS now: UpdateDefault3DS runs m_frameSkip extra full core
frames per paced update (no savestates = no cheap junk render, but a nonzero frameskip
also bypasses the pacer wait, so it still fast-forwards well - measured ~3.5x game time
at skip 4 in level 1-1). m_bDiscardVideoFrame makes the holo callback drop the skipped
frames' layer copies. Made for skipping the slow cutscenes.

Round 5 (Aug 2026): TRUE LAYERED CAPTURE (the "shadow buffer") replaces depth-buffer
slicing as the default. The core's generated fragment shaders route every top-scene
fragment into a 24-band GL image array DURING the game's own draws (per-band interlocked
depth test, the game's real blend equation baked into each capture shader variant, so
translucency arrives with real alpha). Because the PICA FS always writes gl_FragDepth,
the host depth test runs late and OCCLUDED fragments still execute - geometry hidden
behind nearer layers survives in its own band (verified: W1-1's full backdrop exists
behind the foreground strata in the layer dumps). A compute pass packs portrait bands to
landscape + does the seam-overlap fill; readback rides the existing async PBO ring.
Key architecture lessons (all learned the hard way on SM3DL, see the fork commit):
- The scene is scoped by following display transfers to the top-LCD scan-out address
  (exact base match) AND packing at the transfer moment itself, with capture gated off
  from transfer to present. SM3DL reuses the SAME render target for the bottom screen
  after shipping the top scene - vblank-aligned capture windows let bottom content
  stamp the top bands (Seth's mushroom bug, the 80px letter strip, the strobing gray
  overlay were all this or its cousins).
- Draws that blend by DESTINATION alpha (SM3DL's fog compose) or write partial color
  masks (dest-alpha prep) are never captured; band alpha is not framebuffer alpha.
- Depth-less draw routing (fixed Aug 2026, two revisions; before this the title
  logo/(c)/3D-badge overlays landed on the farthest band BEHIND the diorama and blinked
  through gaps = "the Mario overlay appearing and disappearing"): pre-scene depth-less
  draws (before any real depth-tested draw; clears with func Always don't count) are
  backdrops -> farthest band. Post-scene depth-less draws render into the NEAREST band
  and phase-tag their pixels with the draw's capture-seq; the core's pack pass settles
  them against the frame's final last-depth-draw seq - earlier tags (SM3DL's mid-scene
  sky/cloud sheets, which INTERLEAVE with the 3D draws) composite into the farthest
  band, later tags (the true UI tail: logo, HUD, badges) stay near. Per-draw guessing
  failed both ways: same-frame flag = gray wall in front; previous-frame profile =
  background flashing gray/white through gaps during animated intros (draw counts
  shift every frame). Verified: title logo, in-level HUD (timer/W1-1/lives/medals) and
  cutscene letterbox bars float in FRONT; intro-animation band logs show the far band
  fully populated every frame with smooth color transitions.
  THE BACKGROUND STROBE (found + fixed Aug 2026 via the core's per-frame band logger,
  see below - composite screenshots at half rate ALIAS these away, do not trust them
  for strobe hunting): the ship/gate cycle assumed the game's frame boundaries align
  with emulator presents, but the GPU command stream runs AHEAD of vblank, so whole
  game frames landed inside the ship-to-present capture gate, never packed, and the
  composite-cut detector (comparing against the last PACKED composite) saw a changed
  frame with no pack and wiped the diorama flat = fresh/flat alternating at 30Hz
  (background "gone/gray/correct" cycling). Three-part fix in the core:
  - Ship requires the transfer DESTINATION to be a top-LCD buffer (learned exact-base
    set backing up the lagging register check, right-eye registers included, span from
    the live stride register - the old 240*400*4 constant overlapped the bottom LCD
    buffer). Input-only matching had shipped bottom-screen reuse as the top scene.
  - The capture gate REOPENS when the scene RT ships to a non-top destination (the
    bottom-reuse pass is over), so every game frame packs.
  - The flat-wipe needs >= 3 packless frames before honoring the composite-cut
    detector (real 2D cuts stop packs entirely; one-frame cadence gaps must not wipe).
  Verified: 2x180-frame band logs at the title show zero flat frames (was 16/180) and
  the far band fully populated every frame.
  THE GRAY SKY (fixed Aug 2026, four stacked core-side capture bugs; the composed frame
  was always correct): the game fills the RT's sky base color with a GPU MemoryFill
  (invisible to draw capture; now latched and composited under the farthest band),
  draws the sky gradient with garbage alpha 0 through a replace blend (band alpha now
  forced 1 for replace writes), draws full-screen stencil-func-NEVER cards that never
  pass on hardware but stamped the bands gray (now skipped), and the farthest band's
  capture depth is primed to the far plane so depth-fail impostors also fail the band
  test. Details in the fork's AGENTS.md.
  DEBUG TOOLING: drop `holo_band_log_request.txt` in the game process's working
  directory (editor runs: the ENGINE Binaries\Win64 dir) and the core appends 180
  frames of per-band used/nonzero/meanRGB lines to `holo_band_log.txt` there - one
  line per DELIVERED frame, so 30/60Hz strobes cannot alias. `holo_draw_log_request.txt`
  logs ~120 swaps of every draw with capture/skip reasons plus fills/copies/transfers,
  and probes the far band's pixel (30,30) after every captured draw.
  `holo_dump_request.txt` still dumps 10 raw composite+depth frames to holo_dump/.
  LAYER PEEL HOTKEYS: ';' hides one more of the NEAREST layers, ''' shows one again
  (works on the device too; the sprite path skips hidden actors). For looking behind
  the front of the diorama while debugging band content. GOTCHAS: PlayerPawn.cpp's
  SetupPlayerInputComponent has a DEAD `#if PLATFORM_ANDROID` binding list ABOVE the
  live `#else` list - new hotkeys must go in the EKeys:: styled else-branch (the first
  peel attempt landed in the Android block and silently did nothing). The engine's
  default Semicolon=ToggleDebugCamera dev binding is removed in DefaultInput.ini.
  The physical ' key arrives as EKeys::Quote on Windows, NOT EKeys::Apostrophe -
  bind both (an Apostrophe-only binding never fires from the real keyboard).
  Also: on 2D-composed screens (the world map at rest is one flat compose) nearly all
  content legitimately sits in the 1-2 nearest bands, so peeling there blanks the
  screen - that is the scene's real depth layout, not a bug; judge peel in-level.
  SM3DL's save-load darkening (scene dimming over ~3s) was the stencil-masked dim
  capture bug (see the fork's AGENTS.md); sprite shadows are also OFF for all 3DS
  layers now (cast and receive) - 24 dense bands of silhouette stamps read as black
  outline lines and a compounding darkening.
  IF THE WHOLE SCENE ENDS UP ON ONE LAYER: that is the core's adaptive band range
  measuring a degenerate depth buffer, not a frontend bug. Arm the band log and read
  `spread=` / `topshare=` (healthy in-level SM3DL = spread 24, topshare ~0.37; the
  collapse signature is spread<=2, topshare>=0.5). Root cause
  and the reverted dead ends are in the fork's AGENTS.md.
  THE STORED-ITEM MEDALLION COLLAPSE (fixed Aug 2026): with an item in Mario's
  inventory the world map collapsed to spread=2 - 90% of the screen on the NEAREST
  layer under a solid-blue far band. The map clears the scene DEPTH buffer mid-frame
  and draws the held-item medallion (top-right circle) on the wiped buffer before
  shipping, so the core's ship-time depth read measured only the medallion's
  0.0001-wide depth island and the adaptive band range snapped to it, clamping every
  scene fragment into band 0. The core now snapshots the scene depth AT that
  mid-frame clear (the fill of the recorded depth address after captured scene draws
  is a positive "screen-space gadget" signal) and routes the post-clear medallion
  draws near with the UI. Fix is core-side only (no frontend change); details and the
  band-log numbers are in the fork's AGENTS.md. NOTE: this fix removed the reason the
  world map ever read spread=2 - the map is a real 3D scene there; a legit flat
  compose reading spread=2 still exists on true 2D screens (menus, letter cards).
  The LayerBG moon wall is HIDDEN for 3DS (ApplyStartingGameSpecificSetup; the 3DS
  capture has its own backdrop band, the wall only ever showed through capture gaps).
- Scene cuts drop stale layers immediately (composite-cut detection), which is what
  fixed "the letter card wears the previous scene's depth".
Frontend: honors slice `used` flags with one-shot clears + per-layer dirty gating (empty
or unchanged layers skip the GPU upload AND the shadow-rect alpha scan), staging is
ping-ponged instead of a per-frame 384KB heap alloc, and the LKG sprite tile loop skips
empty layers. `-hololegacy` on the command line forces the old CPU slicing for A/B.
Harness gained `touch <x> <y> [frames]` (bottom-screen pixel tap - the SM3DL "Start
Game" button needs it; A does not work there).

NOT yet done: per-game depth-band profiles, near-band HUD routing knob, sound
verification, release-bat/core-build integration, non-uniform bands via depth01.
Fallbacks: no interlock extension -> packed RGB565 opaque capture; no GL4.3 compute ->
legacy CPU slicing automatically.

## Where things live

- The emulator source is Seth's fork of Azahar (Citra successor), checked out at
  `f:\Unreal\azahar` (github.com/SethRobinson/azahar, branch `holo`, based on the upstream
  2126.0 tag). NEVER push it without Seth's permission. Its `AGENTS.md` has the MSVC build
  recipe; output is `build-lr\bin\Release\azahar_libretro.dll`, currently copied by hand to
  `Binaries\Win64` (BuildCores.bat integration pending). `f:\Unreal\azahar\holo_tools\` has
  a RetroArch-based automation harness (UDP input/screenshots) and the depth-slice analysis
  scripts from the proof of concept.
- ROMs: `3ds/` in the project root, extensions `.3ds`/`.cci`, DECRYPTED dumps only (the
  core refuses encrypted ones; some "decrypted" dumps still need the NCCH NoCrypto flag set,
  see holo_tools/AGENTS.md).

## How the integration differs from the other cores

- The core has NO savestates (`retro_serialize_size()==0`): InitEmulator skips the state
  buffers (a hard error for other systems), SaveState/LoadState no-op, and the profile
  update (`UpdateDefault3DS`) is a single RenderFrame per frame, no rewind multi-pass.
  Harness `savestate`/`loadstate` and the F/G/L hotkeys do nothing for 3DS.
- ROMs are huge (512MB): LoadRom does NOT read the file into RAM for 3DS; it hashes the
  first 1MB for the GameProfileManager key and passes only ginfo.path (the core loads the
  file itself, need_fullpath).
- Sample rate (32728 Hz) is applied from retro_get_system_av_info in LoadRom (3DS-scoped;
  the SET_GEOMETRY handler only reacts on rate CHANGE now because this core re-sends
  SET_GEOMETRY every frame and used to rebuild the audio buffer 60x/second).
- Pacer catch-up is clamped to 1 frame for 3DS (a frame can cost 10ms+).
- Core options are prefixed `citra_`; the env callback answers `citra_graphics_api` =
  "Software" for now. The software renderer is SLOW (~120ms/frame in heavy scenes, ~20fps
  title screens): fine for plumbing tests, not shippable. The Phase 2 plan is a core-side
  "holo mode" (own offscreen GL context + depth-slice readback delivering pre-split layers
  like the VB core, via a new HoloLayer ABI).
- Frontend env additions worth knowing: the core also calls ~15 env commands the frontend
  answers false to (core options v2, VFS, mic, sensors...); all verified harmless. The
  core's stub HLE applets auto-pick a premade Mii (no dialog).

## Known gaps / gotchas

- Movement in 3D games needs the circle pad; the frontend is digital-only today, so games
  like Mario 3D Land can navigate menus but not walk. Planned fix is core-side
  (dpad-to-circle-pad option) rather than frontend analog.
- Audio arrives via retro_audio_sample_batch but possibly from a non-game thread (the
  core's DSP thread); RTBufferGenerator thread-safety unverified. Verify before shipping.
- The depth-slice proof of concept (validated Aug 2026): the emulated GPU keeps a clean
  240x400 D24S8 depth buffer per frame that slices into good-looking diorama layers.
  Details, captures, and the parallax previews: f:\Unreal\azahar\holo_tools\poc_out.
