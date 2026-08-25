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

NOT yet done: per-game depth-band profiles, UI band handling (HUD currently lands on the
far plane), touch input, sound verification, release-bat/core-build integration, and the
roadmap "true layers" per-band re-render (fills the remaining silhouette-edge disocclusion
slivers). Cutscenes that render as tilted flat cards (the letter) look janky as dioramas -
faithful but unflattering; per-scene flattening is a future profile knob.

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
