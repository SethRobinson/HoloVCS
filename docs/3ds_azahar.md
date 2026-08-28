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
  - The capture gate REOPENS at the top-screen BUFFER SWAP (the frame's true in-stream
    boundary), so every game frame packs. The intermediate reopen-on-non-top-ship rule
    leaked bottom content: in-level the game runs a SECOND bottom batch through the
    shared RT after the first one ships, including the stored-item medallion's
    full-height depth-tested model draw, which captured into the bands every frame as
    a giant mushroom standing in the level (fixed Aug 2026).
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
  the front of the diorama while debugging band content. BAND MODE ONLY since Aug 27
  2026: on 3DS multiview the same keys drive the CUTAWAY plane instead (see the
  debug-visualization section below), and peel-hidden actors now stay in the capture
  framing AABB (tagged "PeelHidden", exempted in FitLookingGlassCaptureToLayers) so
  peeling can never move the focal plane off the quilt carrier. GOTCHAS: PlayerPawn.cpp's
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

Round 6 (Aug 2026): TRUE MULTIVIEW - WORKING END-TO-END ON DEVICE (capture mode 2,
the DEFAULT whenever the Looking Glass plugin is active). The insight: the 3DS has
real 3D geometry, so instead of slicing into 24 depth bands the core renders the
scene once per Looking Glass view with a per-view sheared camera - pixel-perfect
parallax, no depth quantization, no seams, no band routing heuristics. Neither Unreal
nor any Looking Glass plugin/SDK can do this "directly" (they only render geometry
they own or display quilts they are handed), so the per-view rendering lives in the
core and the frontend stays Unreal; a raw C++ libretro harness would need the
identical core work and was rejected.
- Core side (see the fork's AGENTS.md for mechanics): every scene-scoped draw is
  re-submitted instanced (one instance per view) into a private layered FBO with real
  fixed-function depth/stencil/blend per view; per-view shear is linear in NDC depth
  and derives from the same smoothed range the bands used; UI/depth-less draws are
  screen-locked. The scoping (top-RT address, viewport gate, ship gate) is REUSED
  unchanged from the band capture. In mode 2 the band capture is OFF entirely; a
  compute pass packs the views into one quilt (LKG tile order), read back through a
  PBO ring and delivered via ABI v4 (`HoloLayerInfo.quilt`, packSeq dirty gate).
- Mode negotiation (LibretroManager.cpp holo_capture_mode): LKG builds default to 2
  and serve the DEVICE tile grid (`holo_view_count`/`holo_quilt_cols`/`holo_quilt_rows`
  from the plugin's resolved TilingValues via GetLookingGlassTiling, which also
  triggers the one-shot Automatic-tiling apply for boot-straight-into-3DS); flat
  builds serve 1 (band diorama unchanged). Flags: `-holobands` forces 1 for on-device
  A/B, `-hololegacy` 0, `-holomultiview` forces 2 anywhere (flat-build quilt debug).
- Frontend display: the quilt lands on a carrier quad ("HoloQuilt"-tagged, created
  lazily at m_layerInfo[count+1] by EnsureQuiltCarrier) parked AT the layer stack's
  center depth = the capture focal plane, so RenderSpriteQuilt projects it with zero
  added parallax and blits each lens tile's own view sub-rect (CPD floats 4-6 carry
  viewCount/cols/rows; 0 views = dormant). The bottom screen stays the existing
  sprite quad; help/FPS/status overlays unchanged. The 18MB quilt copy+upload is
  packSeq-gated; the carrier skips the per-frame alpha scan and is
  VisibleInSceneCaptureOnly so the raw collage never shows in the flat/2D views.
- Live depth: `[` `]` / holo.DepthScale push through the ABI v4
  `retro_holo_set_view_params` export in ApplyLayerDepth (scale 0 = flat; the core
  ignores repeats). 2D screens: the core parks quilt.used=0 (flat-wipe) and the
  frontend falls back to the middle-band composite via SetQuiltCarrierActive(false).
- VERIFIED on the Looking Glass Go (Aug 2026): 66 views negotiated (11x6), the saved
  device quilt shows a true per-view render in every tile with depth-proportional
  parallax and screen-locked logo, 60 fps at the SM3DL title, live depth scaling
  works, NES-on-LKG and flat-build 3DS mode 1 regressions clean.
- Known gaps: mirror FS still writes gl_FragDepth (no early-Z; in-level perf on
  weaker GPUs may want the FS variant without it), SW-vertex-path draws not mirrored
  (counted in holo.mv_sw_skipped), mid-scene 2D sheets sit at the convergence plane
  instead of far (revisit per-game on device), the scene-capture fallback path
  (rotated capture / fly cam) draws the raw quilt collage quad. Phase D options
  (extension-probed VS gl_Layer, stereo-pair auto calibration) in the plan file.
- THE WRONG-DEPTH BUG (Aug 27 2026, Seth: "wrong depths, hurts my eyes"): FOUND AND
  FIXED, Seth-verified and committed in both repos (HoloVCS 9608d43, fork 97b20ddc7).
  Root cause was suspect 1: the VIEW SWEEP WAS PSEUDOSCOPIC. The shear sign
  (g_view_strength positive) had been picked from a dump that "showed parallax" but
  swept the views backwards. KEY CONVENTION FACT (proven by the band path, where
  band 0 = lowest buffer value displays nearest on device): gl_Position.z/w ASCENDS
  with distance (PICA vertex ndc z: 0 = near plane, -1 = far plane), and SM3DL's
  recorded PICA depth mapping is scale=-1/offset=0, i.e. buffer == z/w exactly. The
  handoff notes' premise "z/w = +1 at the near plane" was backwards, which is how
  the wrong sign originally looked plausible on paper. Suspect 2 (space mismatch)
  was real but SECONDARY for SM3DL (identity mapping); the conversion now exists
  anyway: the rasterizer records viewport_depth_range/near_plane + WBuffering with
  the scene depth record (holo_hook depth_scale_rec/depth_offset_rec) and
  UpdateViewShearParams converts the smoothed buffer range into z/w space through
  them (W-buffer falls back to -1/0). Also fixed while in there: conv comes from the
  RAW span so the zero-parallax plane always lands inside the scene (the old 0.02
  range floor pushed it outside on shallow scenes = wholesale drift), and sep
  normalizes by the raw span like the bands (the floor made shallow scenes nearly
  flat); g_view_strength recalibrated 0.12 -> 0.20 to approximate the band
  diorama's parallax on the same scene/knob.
  OBJECTIVE EVIDENCE (SM3DL title screen, quilts at holo.DepthScale 5, patch
  cross-correlation view 0 -> view 13, scratchpad quilt_patch_shift.py): band mode
  (ground truth): far sky +73..84px RIGHT, near room LEFT, FPS text 0. Multiview
  BEFORE: whole scene -16..-22px LEFT (floating far in front of the panel) and far
  sky MORE left than near blocks = depth-inverted. Multiview AFTER: sky +2..+4
  RIGHT, near content at/below 0, no wholesale drift - matches the band direction
  and convergence. Numeric check: `holo_view_log_request.txt` in the game process
  working dir -> 120 frames of buf-range/mapping/conv/sep lines in
  holo_view_log.txt (conv must sit inside z=[near..far]; sep is NEGATIVE now by
  design).
  Live knobs for Seth: holo.DepthScale (parallax strength, [ ] hotkeys) and NEW
  holo.Convergence <0..1|-1> (zero-parallax plane as a fraction of scene depth;
  core default 0.35 = 35% of the range pops out; 0.5 matches the band diorama's
  focal-plane framing; -1 returns to default).
  Suspect 3 (native factor_3d stereo-pair calibration) was NOT needed - direction
  and convergence are objectively matched to the proven band path; keep it in the
  back pocket for absolute-magnitude calibration if Seth still dislikes the feel.
- Debug: drop `holo_quilt_request.txt` in the game process working directory (editor
  runs: the ENGINE Binaries\Win64 dir) -> 3 raw-array stitches (holo_quilt_NN.bmp,
  view 0 TOP-left) + 3 packed ABI quilts (holo_quiltpk_NN.bmp, view 0 BOTTOM-left).
- SEEING THE RAW QUILT ON THE DEVICE (educational, no build flags): pause, then turn on
  the fly camera (V / L3+R3) and rotate with the right stick. A rotated capture makes the
  sprite fast path bail to the scene-capture quilt, which renders the real scene - and the
  scene contains the HoloQuilt carrier quad wearing the core's packed collage, so the whole
  per-view grid shows up on the panel. D-pad up/down then magnifies it and d-pad left/right
  pans across it (see the DEBUG FLY CAMERA bullet in AGENTS.md): the magnifier is a framing
  crop with the focal plane pinned, so tiles stay sharp - flying closer instead pushes the
  quilt off the focal plane and the lens blurs it. Harness twin for repeatable shots:
  `exec holo.FlyCam 1`, `exec holo.FlyPose <yaw> <pitch>`, `exec holo.FlyZoom 8`.
- ROM partial gotcha rediscovered while testing: `-rom=3d` matches the `.3ds`
  EXTENSION (loaded Metroid), and `-rom=Land` matched Virtual Boy Wario Land; use a
  quoted unique fragment like `-rom="3D Land"` (FParse handles quoted values).

Round 7 (Aug 27 2026): SETTINGS PERSISTENCE FIXES + DEBUG VISUALIZATIONS.
- VIEW-PARAM DELIVERY FIXED: nothing used to push depth/convergence to the core after a
  load, so the frontend's 3DS 90% default NEVER arrived (the core ran at its own 1.0
  until the first [ ] press), and FreeLibrary on rom/core switch reset the core statics
  while nothing re-sent them. Now ALibretroManagerActor::PushHoloViewParams() (extracted
  from ApplyLayerDepth) is called from every InitLayers (whose tail is now a single
  ApplyLayerDepth call), from ResetRom, and from a 1Hz self-heal in Tick (the core
  dedupes repeats). It also logs each changed push and warns ONCE (log + status) when
  mode 2 was negotiated but the export is missing = stale azahar_libretro.dll, which
  used to be a totally silent "depth keys do nothing".
- ZOOM REWORKED (all systems): = and - used to multiply actor scales, which the very
  next AABB-driven camera/capture fit normalized right back out (why zoom "reset" on
  every depth press, rebuild, or R). Zoom is now a persistent m_userZoomFactor
  (clamp 0.2..5) applied INSIDE the fits (captureSize /= zoom in
  FitLookingGlassCaptureToLayers, dist /= zoom in ComputeFlatCameraFitDist); console
  twin holo.Zoom <factor>. Quads never change size, so zoom cannot cause screen overlap.
- BOTTOM SCREEN PLACEMENT deterministic: RepositionBottomScreen() computes the quad
  height from the static mesh ASSET bounds through the live component transform (the
  old just-spawned-actor bounds could read 0 = bottom screen landed exactly ON the top
  screen after a reset) and recomputes the device-aspect gap at call time; re-run from
  ApplyLayerDepth and the 1Hz self-heal, so a late-resolving panel aspect self-corrects.
- DEBUG VISUALIZATIONS (Shift+number, 3DS only; console twins holo.Viz
  <wire|clay|unlit|depthbw|heat|rainbow|xray|off|mask> and holo.Cutaway <0..1>):
  - Shift+1 wireframe (glPolygonMode wrap around scene draws, primary + mirror pass)
  - Shift+2 clay: white textures, real lighting (UserConfig FS variant)
  - Shift+3 unlit: lighting forced full-bright (UserConfig FS variant)
  - Shift+4 depth B&W: per-pixel scene depth as grayscale, near = white
  - Shift+5 depth heatmap: same normalized depth through an orange->yellow->blue ramp
    (4/5 are mutually exclusive, frontend-enforced). While either depth view is on,
    modulate-only draws (blend src RGB factor Zero: the stencil-masked shadow/dim
    quads) are SKIPPED core-side - they deposit no color of their own and only
    stamped dark blotches over the depth color coding (Seth's report).
  - Shift+6 slice rainbow: band mode tints each depth band a hue (THE shadow-buffer
    showpiece); multiview tints each VIEW a hue, so head movement sweeps the rainbow
    on device (pack compute uniform, zero shader recompile)
  - Shift+7 x-ray: multiview only; mirrored scene draws skip the depth test and blend
    additively, so occluded geometry glows through with true per-view parallax
  - Shift+0 all off. ; and ' on 3DS MULTIVIEW drive the CUTAWAY plane (hold to sweep,
    4%/step): an FS discard clips everything nearer than the plane, revealing occluded
    geometry that genuinely re-renders per view (the layer-peel successor; in band
    mode ; ' keep the classic peel). Since Aug 28 2026 the cut also removes
    SCREEN-LOCKED UI draws (title logo, HUD) once the plane passes 0.03 - they sit AT
    the screen, the nearest depth there is (core-side kHoloCutLockedThresh, counted as
    the M line's uicut skip; see the fork's AGENTS.md).
    CUTAWAY FREEZE (fix, Aug 27 2026 after Seth's "it keeps cutting by itself"): the
    plane derives from the smoothed depth range, but the cut removes the near
    fragments the next readback would measure, so the range crept deeper and the
    re-derived plane chased it = a runaway that ate the scene in seconds. While the
    cut is active UpdateBandRange HOLDS the smoothed range (plane, shear convergence,
    and band split all pin to the scene as it was when the cut began; ; ' nudges
    still move the plane within that pinned range). Updates resume the moment the cut
    returns to 0. Side effect to know: a scene CHANGE while a cut is held can land
    mostly or entirely on one side of the pinned plane (the title attract's scene
    swaps go fully black at deep cuts) - that is the pin working, not the runaway;
    judge cutaway in-level where the scene is continuous.
    THE PIN WENT STALE - FIXED Aug 27 2026 (Seth: "; adds to the cutaway but ' no
    longer removes, it seems to do nothing"). The FRONTEND was innocent and was proven
    so first: with -keydiag the physical ' logs `KEYDIAG: Quote`, OnApostropheKey runs,
    and NudgeCutaway logs a clean 0.04 -> 0.08 -> 0.12 up then 0.08 -> 0.04 -> 0.00 back
    down, ApplyHoloViz pushing each one. The defect was the pin: it held g_smooth_min/max
    for as long as viz_cut01 > 0.001 and released only at EXACTLY 0, so any scene change
    while the cut was held left the plane mapped to a dead scene's depth range and cut01
    stopped meaning anything in the current one. Measured on device before the fix: with
    a stale pin holo.Cutaway 0.6 left the scene UNCUT while 0.05 cut half of it away -
    effectively inverted. ' read as dead because each 4% step moved the plane inside a
    stale range, and the range could not re-sync until you walked all the way to exactly 0.
    THE FIX (core-side, two files): the cutaway discard now rides the MULTIVIEW MIRROR
    PASS ONLY. gl_rasterizer.cpp skips the primary assign when holo_mirror is set, and
    MirrorDrawToMultiview assigns holo_cutaway around its own UseFragmentShader and clears
    it after so it can never leak into the next primary draw. The mirror pass is what the
    panel actually shows, so the picture is unchanged, but the primary depth buffer that
    UpdateBandRange measures is never cut - no runaway, so the freeze is not needed and is
    now BAND-MODE ONLY (holo_slicer.cpp gates it on `multiview == 0`; band mode has no
    mirror pass, still cuts the primary, still needs the freeze).
    VERIFIED on the Go with the core's own view log (holo_view_log_request.txt), which is
    the scene-robust way to check this - SM3DL's attract loop changes scene every few
    seconds and makes side-by-side quilt captures useless:
      - the range stays LIVE under a held cut: at cutaway 0.5, buf=[0.8424..0.9974] crept
        to [0.8428..0.9974] over 120 frames instead of freezing, and the near end stayed
        at 0.842 rather than climbing toward the plane (~0.92) as a cut primary would.
      - the range is INDEPENDENT of cut depth: buf=[0.9894..0.9914] identical at cutaway
        0.05 and 0.85, i.e. the cut no longer feeds back into the measurement at all.
    Gotcha found while testing: turning the cut on over shaders that have not been seen
    yet costs the usual lazy variant compile, so the first frame or two after a scene
    change can capture UNCUT. Judge the cutaway in-level after it settles, never off a
    single quilt grabbed right after a scene cut.
  PAUSED CHANGES ARE REFUSED (Aug 27 2026, the end of a four-round saga): the core
  only renders inside retro_run, so a viz/cutaway change (any capture mode) or a
  depth/convergence change (multiview) on a P-paused screen has no frame to show
  itself on. A savestate-pin re-render was built for this (pin the state, render,
  rewind; then split into a fast per-keystroke path plus a debounced settle when the
  per-keystroke rewinds locked the app for 10+ seconds), and Seth CUT the whole thing
  as bad UI - the state round-trips are seconds-long hitches no matter how they are
  scheduled. Rule now: ALibretroManagerActor::RefusePausedHoloChange(bMode2Only) sits
  at the top of every such setter (ToggleHoloViz/ClearHoloViz/NudgeCutaway any-mode;
  SetUserDepthScale/NudgeConvergence and the holo.Convergence cvar mode-2-only, since
  band-mode depth respreads engine-side and works fine paused) and shows "Can't
  change that while paused" (plus a "Refused holo change while paused" log line).
  The core-side export was removed again; the serialize round-trip stays for the F/G
  savestates, and Load3DSStateFromFile still runs four muted frames after a load
  while paused so the frozen screen shows the loaded state (that one is not a state
  trick - you just loaded, the 4/60s shift is meaningless).
  Plumbing: frontend state on ALibretroManagerActor (m_holoVizFlags/m_cutaway01,
  session-sticky within 3DS, cleared on system switch; ApplyHoloViz pushes, re-pushed
  at InitLayers/reset/1Hz) -> new optional export retro_holo_set_debug(mask, cutaway01)
  (ABI v4 addition, HOLO_VIZ_* bits in both holo_layer_abi.h copies, NO version bump;
  old DLL degrades to a status message) -> HoloSlicer::SetDebugViz -> HoloHook state
  (viz_flags + derived buffer-depth cutaway/near/inv_range from the smoothed range) ->
  gl_rasterizer per-draw UserConfig bits (scene-scoped draws only, so HUD/UI stays
  clean; 5 new bits in pica_fs_config.h, variants excluded from the disk cache; UBO
  binding 8 carries the cutaway/depth values) and pack-shader u_viz uniforms for the
  rainbow. Shift chords work headlessly via harness `key LeftShift 8` + `key Four 2`
  (the number handlers poll live modifier state, no latch). Enabling a UserConfig-bit
  mode costs a one-time lazy shader-variant compile hitch on first use; toggling back
  is free.

Round 8 (Aug 27 2026, late): METROID: SAMUS RETURNS MULTIVIEW FIXED (was totally flat
with x-ray/cutaway dead; Seth's report). TWO stacked core-side causes, pinned in ONE
armed run by the new mirror-gate diagnostics (below). Neither was the suspected
SW-vertex-path gap: MSR hardware-accelerates everything (skip sw=0).
- POST-PROCESS FEEDBACK STAMPS: MSR renders the scene INTO the shipped RT (BOTH eyes,
  ~95 depth-tested draws/frame each - it really stereo-renders and ships both), copies
  both eyes out, builds bloom pyramids through display-transfer downscale chains, then
  stamps the post-processed MONO composite back over the scene RT with depth-less
  full-screen draws (a replace-blend one plus friends), EVERY frame. The mirror
  re-drew those screen-locked over every view = byte-identical tiles regardless of
  what the 91 sheared draws did underneath; x-ray/cutaway only alter sheared draws,
  so they were invisible too. FIX: HoloHook tracks "scene-derived" buffers per frame
  (texture copies + non-LCD display transfers whose INPUT is the scene RT, a scene
  PARTNER, or an already-derived buffer - transitive, covers the mip chains).
  Partners = inputs of top-LCD-destined transfers, i.e. every RT presented on the top
  screen INCLUDING the right eye - needed because MSR's left-eye stamps sample the
  RIGHT eye's bloom copies and top_sources only ever learns the left scan-out chain.
  The mirror gate skips depth-less post-scene draws that SAMPLE any of these (log
  reason "stamp"); pre-scene draws and genuine overlays (MSR's VRAM scanline texture)
  still mirror screen-locked. SM3DL: stamp=0 every frame - its fog compose reads
  DESTINATION alpha via blending and samples no scene texture, so its verified look
  is untouched.
- W-BUFFER SHEAR DEGENERACY: MSR W-buffers (recorded mapping scale=-5.7e-5 off=0
  wbuf=1); its z_ndc is degenerate and the real depth lives in W, so the z-linear
  shear produced identical views even where unobstructed. FIX: the shear is
  DISPARITY-LINEAR for every game (the physical camera-shift model, parallax
  proportional to 1/distance). HoloViewShear reproduces each vertex's buffer depth
  through the recorded viewport mapping exactly as the hardware computes it, then:
  z-buffer values (SM3DL scale=-1/off=0, buffer == z/w) are ALREADY affine in 1/w
  and pass through - ALGEBRAICALLY IDENTICAL to the old (z - conv*w) form, the
  regression run reproduces the recorded title numbers (far sky up to +74px RIGHT,
  near room LEFT, logo 0, DepthScale 5 views 0->13). W-buffer values are linear in
  DISTANCE and convert to p = -1/buffer in both the shader and UpdateViewShearParams
  (conv/sep live in the same p space; the view log prints shear=[..] conv= in it).
  An intermediate buffer-LINEAR attempt looked depth-reversed on the MSR title
  (Seth: "the planet is in front instead of behind"): a distant starfield stretches
  the linear range so 35% conv landed beyond the WHOLE planet (all of it popped in
  front of the screen-locked logo) while the far field ate the parallax budget.
- STEREO GROUND-TRUTH PROBE + CONVERGENCE DEFAULT = NEAR END (Seth: "on a real 3ds
  the title is OVER the planet... test against the stereo output from azahar").
  New core debug facility: drop holo_stereo_probe_request.txt in the game process
  working directory - the emulated 3D slider jumps to 100 (the game then renders its
  own TRUE stereo pair; MSR renders both eyes even at slider 0, so this costs it
  nothing) and three pairs are written at ship: holo_stereo_L/R_NN.bmp (landscape)
  + holo_stereo_D_NN.bin (float depth, same orientation; one frame stale, probe on
  static scenes). scratchpad stereo_fit.py correlates L vs R per patch and fits
  parallax vs depth. MEASURED (MSR title AND in-level): the game's own zero-parallax
  plane sits AT the scene's nearest content (zero crossing buffer 0.067, scene floor
  0.066; NOTHING renders crossed - the whole world is behind the screen, logo/HUD at
  the screen), and its parallax-vs-depth curve is disparity-linear (predicted 25px
  vs measured 26px at mid-depth) - independently validating the shear model. So the
  core conv default changed 0.35 -> 0.02 (the old default popped 35% of the scene
  out - the "planet sticking out" report; 0.02 keeps the nearest sliver from
  flickering across the plane as the smoothed range wobbles). This applies to ALL
  3DS multiview (SM3DL now also sits behind the focal plane = the real-3DS look);
  { } / holo.Convergence raise it live when someone wants pop-out, and the
  NudgeConvergence first-press baseline + cvar help text follow the new default.
  VERIFIED in-level (gunship-arrival savestate, flat build -holomultiview, 175%
  default, views 0->24 measured with quilt_patch_shift.py): far sky +69px RIGHT,
  near gunship -36px LEFT, depth-ordered spread between; x-ray now floods visibly
  (MSR's overdraw at the constant-alpha 0.28 washes very bright - per-game tuning
  knob if it bothers anyone). Band mode (-holobands) untouched by both fixes.
- MIRROR-GATE DIAGNOSTICS (landed first; found all of the above in one run): the
  armed draw log now carries V lines (one per top-sized draw: addr, dt, acc,
  t0=first sampled texture addr, verdict CAP/fb/vp/gate/addr/stamp, shear mode),
  an M line per ship (cap/mv/sheared/locked/skip[sw fail fb vp gate addr stamp]/
  gadget/srcs/depth_addr/sep/conv/mvtex) and an MP line per present (live counters,
  transfer_ever/has_src/quilt_ever/packseq). The old mv counters only printed from
  the quilt dump, which early-outs when mv_color_tex==0 - unreachable EXACTLY when
  every draw skipped and the numbers mattered. holo_view_log gained mvtex=/swtot=
  and prints the mapping at full precision (the old %.4f showed MSR's scale as
  -0.0000). Frontend log lines (Saved/Logs / log.txt): "3DS quilt live/dormant ..."
  from the video callback (a quilt that never goes live IS the flat-hologram
  signature), "Quilt carrier -> ACTIVE/dormant", and change-gated "Holo viz push:
  mask=0x.. cutaway=..".

Round 9 (Aug 28 2026): LANDSCAPE-PANEL LAYOUT (for the original 8.9" Looking Glass) +
touch-tap latch fix.
- Landscape Looking Glass panels (device aspect > 1.0 from the plugin's GetAspectRatio;
  `-lkglandscape` forces it with no device, for testing) show ONE 3DS screen at a time
  instead of the stacked two-screen layout: the 3D top screen by default, and the B key /
  bare gamepad LEFT TRIGGER / `holo.BottomScreen` swap the bottom screen in and back.
  Portrait panels and the flat build keep the stacked layout; the toggle explains itself
  there ("Both screens already shown") instead of doing nothing.  Implementation lives in
  ALibretroManagerActor::RepositionBottomScreen (single chokepoint: called from every
  ApplyLayerDepth and the 1Hz self-heal, so the layout self-heals): the inactive screen's
  actors are SetActorHiddenInGame (the capture fit and the sprite path both skip hidden
  actors), and with the bottom screen focused the bottom quad moves to the top stack's
  center depth so the refit frames it alone, on the focal plane.  Gotchas encoded there:
  peel-hidden band layers get their PeelHidden tag stripped while the bottom screen is
  focused (tagged actors stay in the framing AABB by design; restoring goes through
  SetLayersPeeled so the peel comes back), and EnsureQuiltCarrier repositions before its
  fit so a carrier born during bottom focus starts hidden.  The focus resets to the 3D
  screen on every game switch like depth/zoom/convergence.  On the pad, bare LT only
  toggles when the landscape layout is live - everywhere else it stays 3DS ZL (L2), and
  the L3+LT prev-game chord and fly-cam trigger axes are checked first, unchanged.
  Verified headlessly (no device, `-lkglandscape -rom="3D Land"`, quilt captures): default
  = top screen only, `key B` = bottom screen alone framed full-size, `holo.BottomScreen`
  = back, portrait run unchanged, NES unaffected.
- TOUCH-TAP LATCH FIX (Seth: "if you click, it sometimes clicks the wrong spot"): the
  press-position latch rewound the cursor FIVE frames (~83ms) into its history, well past
  the real display latency of the stamped cursor (~2 frames), so clicking shortly after a
  move tapped a stale mid-flight position behind the visible arrow.  framesBack is 2 now
  (LibretroManager::SetTouchDown).  The latch mechanism itself (hold until a >15px drag,
  arrow drawn at the latched point while pressing) is unchanged.

NOT yet done: per-game depth-band profiles, near-band HUD routing knob, sound
verification, release-bat/core-build integration, non-uniform bands via depth01.
Fallbacks: no interlock extension -> packed RGB565 opaque capture; no GL4.3 compute ->
legacy CPU slicing automatically (multiview mode falls back to band mode when its GL
requirements are missing).

## Where things live

- The emulator source is Seth's fork of Azahar (Citra successor), checked out at
  `f:\Unreal\azahar` (github.com/SethRobinson/azahar, branch `holo`, based on the upstream
  2126.0 tag). NEVER push it without Seth's permission. Its `AGENTS.md` has the MSVC build
  recipe; output is `build-lr\bin\Release\azahar_libretro.dll`, currently copied by hand to
  `Binaries\Win64` (BuildCores.bat integration pending). `f:\Unreal\azahar\holo_tools\` has
  a RetroArch-based automation harness (UDP input/screenshots) and the depth-slice analysis
  scripts from the proof of concept.
- ROMs: `3ds/` in the project root, extensions `.3ds`/`.cci`, DECRYPTED dumps only. The
  core does NOT trust the NCCH crypto flags anymore (they are often stale on decrypted
  dumps): flagged-encrypted dumps load anyway and the contents are verified instead
  (exheader jump ID, ExeFS section names, RomFS IVFC magic - NCCHContainer::Load in the
  fork). A REALLY encrypted dump is refused with the core's "really is encrypted"
  message, which the frontend now shows on the status display (the env callback forwards
  RETRO_ENVIRONMENT_SET_MESSAGE / _EXT, and InitEmulator re-shows a core-explained load
  failure sticky instead of the generic "Place rom" text).

## How the integration differs from the other cores

- SAVESTATES (corrected Aug 27 2026): the old "core has no savestates / retro_serialize
  always fails" claim was WRONG - the fork's libretro layer implements a real round-trip
  (retro_serialize_size drains async ops then System::SaveStateBuffer; retro_unserialize
  loads); the boot-time probe in InitEmulator just fails before the game runs, which is
  where the myth came from. What remains true: the state is a VARIABLE-SIZE zstd blob
  (tens of MB), so the fixed-slot buffer machinery is unsuitable - InitEmulator still
  FORCES `m_maxSaveStateSize = 0` for 3DS, SaveState/LoadState(slot) still no-op, and
  the profile update (`UpdateDefault3DS`) stays a single RenderFrame per frame with no
  rewind multi-pass. F/G/L and harness `savestate`/`loadstate` DO work on 3DS now via
  Save3DSStateToFile/Load3DSStateFromFile (fresh serialize through a temporary buffer
  into the normal saves/3ds/<rom>.sav0 path; loading while paused auto-refreshes the
  frozen screen and re-pins the paused-refresh state). Old bogus 0-byte .sav0 files
  fail the load with a clear message. CROSS-BUILD LOADS (fixed Aug 27 2026): the stock
  LoadStateBuffer hard-failed when the state's git revision differed from the running
  core, so every core rebuild invalidated every existing .sav0 ("works in the session I
  saved it, fails after restart" = a restage had swapped the DLL in between). The fork
  now WARNS and attempts the load; a genuinely incompatible archive still throws and
  fails with the frontend's message. The 3DS depth scale DEFAULTS to 175%
  (SetEmulatorData; 90% then 155% until Seth's Aug 27 requests; a user [ ] /
  holo.DepthScale adjustment lasts only until the next game switch - every switch
  resets depth/convergence/zoom to the new game's defaults, Seth request Aug 27 2026).
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

- Debug fly camera (V / Start + L-stick click, Aug 2026, see AGENTS.md): while flying, the
  right stick looks around instead of moving the touch cursor, the trigger tap and R-stick
  click taps are blocked (triggers fly up/down), and the pad's circle-pad feed is withheld
  from the game - but the MOUSE stays the touch cursor (mouse fly-look is non-3DS only) and
  keyboard WASD still walks. `touch x y` from the harness works as always.

- In-level game shadows (Mario's blob shadow, the W1-1 sign's projected shadow) WORK
  as of Aug 2026 (core-side fix, no frontend change). SM3DL draws them exactly like its
  save-load darkening (shadow silhouettes rasterized into the stencil buffer, then
  full-screen dst x (1-constant) multiply quads with stencil func NotEqual darkening
  the marked region), and the capture used to skip every stencil-func != Always draw
  (the gray-card / save-dim fix), which erased all real shadows from the bands. The
  core now emulates the stencil test inside the capture shader against a pre-draw
  stencil snapshot; the src-factor-Zero dim quads modulate every band's existing
  pixels where the test passes. Detail in the fork's AGENTS.md (gray-sky item 3).
  Draw-log signature when armed: the two per-frame dim quads log "CAPs", the
  stencil-mutating func-Never draw logs "stencil0"; a plain "stencil" skip no longer
  exists. Verified in W1-1/W1-2 (Mario blob, sign quad, enemy shadows; layer dump
  shows the blob stamped into the ground band).
- OPERATIONAL, learned the hard way (Aug 2026): NEVER hard-kill a running 3DS session
  (Stop-Process) - the core holds live cartridge save data (GameData.bin under
  saves/Azahar/sdmc/...) and there are no savestates; always use the harness `quit`
  command. Blind A-presses through SM3DL's boot land on the FILE SELECT whose cursor
  defaults to the last-used slot and will happily open/create the wrong file
  ("The file has been created." = you just made a new one) - screenshot before
  pressing. Driving the SM3DL pause menu through the harness is unreliable (mixed
  circle-pad/touch acceptance, and after "To Course Selection" Mario stands on the
  course podium where a nudge re-enters the level); when a specific level must be
  reached, prefer a fresh boot and map navigation with screenshots between steps.
- Movement in 3D games needs the circle pad; the frontend is digital-only today, so games
  like Mario 3D Land can navigate menus but not walk. Planned fix is core-side
  (dpad-to-circle-pad option) rather than frontend analog.
- Audio arrives via retro_audio_sample_batch but possibly from a non-game thread (the
  core's DSP thread); RTBufferGenerator thread-safety unverified. Verify before shipping.
- The depth-slice proof of concept (validated Aug 2026): the emulated GPU keeps a clean
  240x400 D24S8 depth buffer per frame that slices into good-looking diorama layers.
  Details, captures, and the parallax previews: f:\Unreal\azahar\holo_tools\poc_out.
