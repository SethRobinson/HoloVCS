# Automation workflow: seeing, controlling, and capturing the running game

How to drive HoloVCS from scripts and tools without touching the desktop, plus the NES
per-game profile authoring method built on it. Added Aug 2026 while authoring the Legend of
Zelda profile.

## The automation harness (in-game, file-based, no window focus)

`AutomationHarness` (Source/HoloVCS/AutomationHarness.cpp, owned by LibretroManager, polled at
the top of `LibretroManager::Update`) watches `<ProjectSavedDir>/Automation/commands.txt`. Drop
one command per line into that file; the game consumes and deletes it each tick and appends
results to `<ProjectSavedDir>/Automation/ai_log.txt`. In editor -game runs that is
`<project>/Saved/Automation/`; in staged SHIPPING builds ProjectSavedDir is
`C:\Users\<user>\AppData\Local\HoloVCS_Flat\Saved` (NOT the dist stage folder - cost a
wait-forever once; the game's log.txt prints the exact path in its "AutomationHarness ready,
watching ..." line). Use `tools/holo_auto.ps1` rather than writing the file by hand (it does
the write-temp-then-rename dance, takes `-SavedDir` for staged builds, and can wait for
screenshots):

```
tools\holo_auto.ps1 -Cmd "shot overworld" -WaitShot overworld  # png lands in Saved\Automation\shots\
tools\holo_auto.ps1 -Cmd "press up 120"                        # hold Up 120 frames
tools\holo_auto.ps1 -Cmd "press start 8","dump"                # multiple commands, one batch
tools\holo_auto.ps1 -Cmd "video 120 clip1"
tools\holo_auto.ps1 -Log                                       # tail ai_log.txt
```

Commands:

| Command | What it does |
|---|---|
| `press <btn>[,btn,...] [frames]` | Hold emulated joypad buttons N visible frames (default 8). Buttons: up down left right a b start select l r. Injected at the libretro input callback, so it works regardless of window focus and never touches the desktop keyboard. GOTCHA: holds only tick down while the emulator is UNPAUSED; a press sent while paused stays pending and fires on unpause. |
| `key <FKeyName> [ticks]` | Synthesized KEYBOARD press through the full UE input path (Slate -> viewport -> PlayerInput -> pawn bindings), held N engine ticks (default 2). FKey names: `SpaceBar`, `LeftControl`, `Enter`, `Tab`, `W`, `One`, `Slash`... Use this to test hotkeys and the real key-to-joypad mappings headlessly; `press` bypasses all of that. Works without window focus and while paused (so it can dismiss the help screen). GOTCHA (Aug 27 2026): `key Equals` / `key Hyphen` are ACCEPTED (FKey::IsValid passes, the log says "held N ticks") but never reach the `=` / `-` bindings - nothing happens, while e.g. `key LeftBracket` works fine. Cause unconfirmed; the likely suspect is `FInputKeyManager::GetCodesFromKey` having no keycode for those keys, so the synthesized event carries keycode 0. Real-keyboard `=` / `-` are unaffected. Use `exec holo.Zoom` / `exec holo.FlyZoom` to drive zoom headlessly. |
| `-keydiag` (command line, not a harness command) | Launch with `-keydiag` and EVERY key that reaches the pawn logs `KEYDIAG: <FKeyName>`. Key routing has bitten this project repeatedly (the physical ' arrives as `Quote` not `Apostrophe`; `Equals` never arrives at all), and this is the fastest way to tell "the binding is wrong" apart from "the handler ran and did nothing". It is what proved the frontend innocent in the Aug 27 2026 cutaway regression. |
| `shot [name]` | Engine screenshot (FScreenshotRequest) to `Saved/Automation/shots/<name>.png`. Captures the viewport even when the window is behind other windows. Do NOT minimize the window though. |
| `video <frames> [name]` | Per-frame screenshots to `Saved/Automation/video/<name>/frame_%05d.png`. Halves the frame rate while active (~28 fps); fine for short clips. No ffmpeg on this machine; read frames directly or assemble elsewhere. |
| `dump` | NES state dump (see below). |
| `dumplayers` | Writes each depth layer's CPU texture buffer to `Saved/Automation/layers/layer_N.png` (layer 0 = deepest/back wall side, highest index = nearest the viewer; NES games use 5 layers, VB 16). Ground truth for what each plane holds and where content sits in 3D; this is the tool that found the Zelda colorkey bug and the transition stripe after composite screenshots had been ambiguous. Transparent areas = png alpha. |
| `pause` / `unpause` | Emulator pause (UE keeps ticking, shots still work). |
| `help [on\|off]` | Show/hide the help screen (no arg toggles). Same as the ? hotkey: pauses the game while up; `unpause` also closes it. |
| `savestate` / `loadstate` | The same `saves/<system>/<rom>.sav0` files as the F/G hotkeys (L also loads) (loading migrates any legacy `<rom>.sav0` from next to the top-level exe into saves/). |
| `touch <x> <y> [frames]` | 3DS only: emulated stylus tap at bottom-screen pixel coords (0..320, 0..240), held N visible frames (default 8). Drives the touch input directly (ignores the virtual mouse cursor and its latch). SM3DL's yellow "Start Game" button only responds to touch - `touch 160 200` presses it; A does not. |
| `rom <partial>` | Live ROM switch by partial filename match. |
| `exec <console cmd>` | Anything the UE console accepts (`r.ShadowQuality 0`, `HighResShot 2`, cvars...). |
| `quit` | Clean exit. |

Camera / GIF-capture console commands (drive via `exec`, flat build; feedback is log-only so no
status text lands in recorded frames). Pair with `video <frames> <name>` for GIF source PNGs:

| Console command | What it does |
|---|---|
| `holo.FlyCam [0\|1]` | Toggle the debug fly camera (same as the V key / Start + L-stick click pad chord). Gamepad flies (left stick move, right stick look, triggers up/down, LB/RB speed), game keeps running, keyboard still plays. |
| `holo.CamSweep <yawA> <yawB> <sec> [pitch]` | Linear orbit yaw sweep A->B. Pitch optional (default: current). |
| `holo.CamPose <yaw> <pitch> <sec>` | SmoothStep-eased move to a pose (shortest yaw arc). |
| `holo.CamWiggle <ampDeg> <periodSec> [cycles] [pitch]` | Parallax yaw oscillation around the current yaw; whole cycles end where they started = seamless GIF loop. cycles 0/omitted = until `holo.CamStop`. |
| `holo.DepthRamp <from> <to> <sec>` | Animate the 3D depth spread (the [ ] value, 0 = flat) with the status message suppressed. Runs in its own slot, so it composes with a Cam command for the "flat screen unfolds into a diorama" shot. |
| `holo.CamStop` | Cancel the running camera script and depth ramp (settles in place, no snap). |

After a Cam script finishes, the camera holds the final angles and the normal idle-return eases
back to the sweep ~5s later - shoot while the script runs, not after. Mouse movement cancels a
running script (user wins).

## 3DS multiview GIF maker (tools/make_holo_gif.ps1)

Builds looping 3D GIFs for ANY running 3DS game in multiview mode (LKG builds default to it;
flat builds need `-holomultiview`). The frames are the core's own per-view renders at native
3DS resolution (400x240 per view), pulled with `holo_quilt_request.txt` dumps and sliced by
`tools/holo_gif.py` (Python + Pillow, both installed) - no window capture, no focus steal, no
status text in frames. Made Aug 2026 for the Metroid: Samus Returns title/surface GIFs.

```
tools\make_holo_gif.ps1 -Mode sweep   -Out Media\gifs\mygame_sweep.gif            # parallax wigglegram
tools\make_holo_gif.ps1 -Mode cutaway -Out Media\gifs\mygame_cut.gif -MaxCut 0.12 # smooth cutaway + sweep
```

- `sweep`: ONE quilt dump -> seamless sine ping-pong across all views (66 on the Go's 11x6
  grid), left-right-left. A frozen moment; only the viewpoint moves.
- `cutaway`: one quilt dump per output frame while `holo.Cutaway` ramps 0 -> MaxCut -> 0
  (smoothstep + hold, per-frame increments = no visible steps), with the view sweep running
  on top (`-Cycles`, default 2). Prewarms the cutaway once first (lazy shader-variant compile
  would otherwise capture the first ramp frames uncut). Takes ~1-2 min for ~100 frames and
  writes/deletes ~100MB of BMPs per frame's request in -DumpDir (6 files per dump, transient).
  Smoother = more frames: `-Fps 20 -Seconds 8` (160 frames, used for the final Metroid set)
  reads noticeably better than the first 14fps/7s attempt; size scales with it.
- The cutaway also cuts SCREEN-LOCKED UI (title logo, HUD) once the plane passes 0.03 -
  core-side rule added Aug 28 2026 (kHoloCutLockedThresh in MirrorDrawToMultiview, fork
  commit 01773e754) after Seth flagged the logo surviving the title cutaway. Needs that
  core DLL or newer; an older DLL just leaves the UI floating.
- `buildup` (Seth's choreography, Aug 28 2026): starts at FULL cut, builds the scene to
  0 with a decelerating curve (`-BuildPower 3`; cut = MaxCut*(1-t)^p, so most of the time
  goes to the near content landing - "it's more interesting there"), camera STATIC (center
  view) throughout the build, then the head-move sweep (`-SweepSeconds`), a still pause
  (`-PauseSeconds`, encoded as 500ms frames), and a quick smoothstep teardown back to full
  cut (`-TeardownSeconds 1`) so the loop wraps seamlessly. Only the build + teardown are
  live captures; the sweep and pause are assembled from the final cut-0 quilt, which also
  makes those phases join the build with zero scene drift. Phase lengths are all knobs;
  defaults 4.5/3/2/1s at the given -Fps.
- Game must be UNPAUSED (paused viz/cutaway changes are refused, and dumps need live ships).
- `-DumpDir` = the game PROCESS's working directory: engine `Binaries\Win64` for editor runs
  (the default), the exe's folder for staged builds. `-SavedDir` = the harness dir (staged
  LKG: `C:\Users\<user>\AppData\Local\HoloVCS\Saved`).
- Autocrop (in holo_gif.py, `--no-autocrop` to disable): the outer views carry unrendered
  black shear margins at the frame edges (up to ~32px on the MSR title, none in the gunship
  scene where draws fill every view); measured across all views and trimmed identically from
  every frame. Three filters, each earned by a real scene: left/right first then top/bottom
  inside that x-crop (side wedges otherwise fake huge vertical runs), the CENTER view's run
  subtracted per line as a baseline (the center has no margins by construction; the ruins'
  silhouette pillars are exact-zero black CONTENT and were getting cropped), and margins
  only count where their inward neighbors are bright (the ruins' top-right 44px wedge hides
  against dark stone - invisible, not worth eating an eighth of the frame over).
- Size knobs: `-Scale 1` is the DEFAULT since Aug 28 2026 (native 3DS 400x240, ~4-7MB -
  Seth: "output at the exact 3ds resolution, no 2xing"); `-Scale 2` doubles it with nearest
  neighbor (~13-27MB). `-Colors`, `-Fps`, `-Seconds`. Dithered starfields are what cost
  the MB.
- Picking `-MaxCut`: probe first (`exec holo.Cutaway <v>` + a dump) - the cut plane runs
  through the DISPARITY-linear depth range, so how much of it the playfield occupies varies
  wildly per scene. MSR title: the whole planet dissolves inside 0..0.06 (0.02-0.04 = a
  glowing ring as the sphere's front face goes), the starfield backdrop survives to ~0.9,
  1.0 is all black -> MaxCut 0.12. MSR in-level spreads much wider: gunship scene cuts
  progressively across 0..0.9 (MaxCut 0.97 = pure sky backdrop), ruins scene playfield goes
  by 0.1 revealing a hidden crashed-ship background world (MaxCut 0.95).
- The packed quilt layout (holo_quiltpk_NN.bmp) is cols x rows landscape 400x240 tiles,
  view 0 at the BOTTOM-left, row-major upward; view count = cols*rows exactly (verified:
  adjacent-view diff is uniform across all 66 views, no row-boundary spikes).
- The Metroid GIF set (Media\gifs\, gitignored - Seth: GIF output stays off the repo):
  title sweep, title cutaway (MaxCut 0.12,
  logo cuts early via the locked-UI rule), surface sweep + gunship cutaway (MaxCut 0.97;
  `loadstate` first - the shipped .sav0 IS the gunship arrival, do not overwrite it with
  another savestate), ruins cutaway (MaxCut 0.95; from the gunship, `press right 300`
  twice reaches the Chozo ruins with the crashed-ship background), and pillars buildup
  (`-Mode buildup -Fps 20 -MaxCut 1.0` at the same walked-right spot; at 1.0 this scene
  still shows a hazy far backdrop, not black).

Workflow rules learned:
- Launch is the only focus grab: `UnrealEditor.exe HoloVCS_Flat.uproject -game -windowed -resx=1280 -resy=720 -rom=<partial>`. After that, never foreground the window; the old SendKeys/CopyFromScreen approach is retired (Seth uses the machine while automated work runs).
- Wait for `harness ready` in `Saved/Automation/ai_log.txt` before sending commands (delete the log first for a clean signal).
- STAGED SHIPPING BUILDS BOOT PAUSED: the help screen auto-shows on the first live frame and pauses
  the emulator. Send `help off` first (its ack `help now off` is unreliable evidence though - it
  prints the current state even when there was nothing to hide), and confirm via log.txt's
  `SetGamePaused: 1 -> 0` line. While paused, `press` holds sit pending and the game looks
  input-dead. Editor -game runs never auto-show.
- A running -game instance still blocks Build.bat; kill the process, build, relaunch, `loadstate`.
- A running STAGED instance locks `dist\win64_test` and fails BuildAndRunWin64Release.bat with Error_FailedToDeleteStagingDirectory: `Get-Process HoloVCS*` and kill before staging. Both test bats accept `nolaunch` as the first argument now; without it they launch the staged exe at the end - and then BLOCK until that game exits, so "the bat finished" is useless as a staging-done signal. Detect staging completion by watching for the HoloVCS process appearing (or `harness ready` in ai_log.txt) instead.
- Both test bats preserve save states (`saves\` tree plus legacy top-level `*.sav0`) across restages (dist\savstate_keep_* backup/restore around the UAT call); PackageWin64Release.bat scrubs both from real releases.
- `loadstate` after boot is the checkpoint trick: menus, file registration etc survive as a `.sav0` even though the frontend has no SRAM persistence. Zelda has one saved from the overworld start; make one per game/situation being worked on.

## Looking Glass builds (staged HoloVCSLKG)

- The staged LKG build's ProjectSavedDir is `C:\Users\<user>\AppData\Local\HoloVCS\Saved` (project
  name HoloVCS, vs HoloVCS_Flat for the flat build), so pass that as `-SavedDir` to holo_auto.ps1.
- `exec lkg.SaveQuilt` saves the next rendered quilt to `<SavedDir>\Screenshots\Windows\` as
  `ScreenshotQuiltNNNNN_qs8x6a0.75.png` (Portrait tiling). This is THE way to inspect per-view
  parallax, layer depth, and colors without standing at the device: each tile is one sweep view.
  Added as a plugin console command because the upstream `LookingGlass.ScreenshotQuilt` exec only
  routes through the plugin window's own viewport client (unreachable from GEngine->Exec in the
  separate-window self-render mode) and the F9 hotkey needs that window focused.
  `exec lkg.ShowFPS 0` first for captures without the per-tile FPS overlay.
- The harness `shot` command captures the MAIN window, which renders black in LKG builds
  (world rendering off) - launch with `-lkg2dview` if a 2D spectator shot is needed, otherwise
  use lkg.SaveQuilt.
- The `.sav0` checkpoints work the same (files sit in `dist\win64_lkg_test\Windows\saves\<system>\`;
  the Zelda overworld one is seeded there and both test bats preserve them across restages).
- `tools\holo_auto.ps1 -CropQuilt <quilt.png> [-Tile N]` extracts ONE 420x560 view from a quilt
  screenshot (tile grid parsed from the `_qsCxRa` filename suffix; default = middle row/column,
  the straight-on view). Use this instead of reading the whole 3360x3360 quilt when a vision
  model only needs one frame - about 64x fewer pixels.

## NES state dump (the profile-authoring tool)

The N key in-game, the `dump` harness command, or `m_bNesDumpRequested` all write
`Saved/nes_state_dump.txt`: both nametable pages as 32x30 hex tile-ID grids, attribute
tables, the 32 palette bytes, and the 2KB CPU RAM. Correlate the grid with a `shot` taken the
same moment to learn which tile IDs are which art. That plus the layer-dump loop is the whole
authoring method: no game knowledge needs to be hand-derived in an emulator debugger anymore.

## Zelda profile facts (UpdateZelda, GameProfileManager.cpp)

- ROM: `Legend of Zelda, The (PRG 1).nes`, MD5 (16-byte header skipped) `d3f453931146e95b04a31647de80fdab`.
- Screen: rows 0-63 HUD (blitted to layer 4, front), rows 64-239 play area. Play area attributes are all palette 2.
- Layers: L1 = full play-area BG (opaque ground plane), L2+L3+L4 = obstacle extrusion (masked nametable re-render; the L4 pass covers play-area rows 64-240 only, so it shares the layer with the HUD without overlap and trees/rocks top out one layer ABOVE the sprites), L3 also gets play-area sprites, L4 = HUD plus HUD-strip sprites (the minimap blip and item icons are sprites; without that strip they would hide behind the opaque HUD). Safe because the NES blit path SKIPS colorkeyed pixels (stacked passes never erase each other).
- Overworld tile IDs (from N-key dumps): `0x24` blank, `0x26` walkable ground, `0xf3` cave mouth. Extrusion applies TWO masks to the authoritative background frame: wall trees (`d8-db`) keyed on black only, and a ground-keyed set (standalone trees `c4-c7`, standalone rocks `c8-cb`, forest-edge diagonals `cc-d7`/`dc-df`) keyed on black PLUS the ground color `(255,255,74)` (NES `$37` as the rgb palette renders it, sampled from the layer buffer) so ground-painted parts stay flat and only the object shape extrudes. The ground color must NOT be keyed on the wall-tree pass: their art uses it as a highlight (24% of pixels, measured by scanning a dumplayers png). Tombstones/armos/dungeon walls still to be dumped and appended.
- Game mode byte RAM `$12` (all values dumped empirically): 0 = title/attract, 1 = file select menus, 5 = gameplay incl. subscreen, 7 = screen-scroll transition (`$13` holds a phase/direction value; 4 and 6 are treated as scroll too), 8 = game over.
- Item subscreen (Start) detection (dumped Aug 2026): RAM `$e1` = 0 closed, 7 sliding open, 8 open at rest, 9 sliding closed (`$12` stays 5 throughout, so `$e1` is the only signal). While `$e1` != 0, `$fc` holds the PPU vertical scroll; the HUD strip's top OUTPUT row = `(240 - $fc) % 240` on every frame of the slide (175 at rest-open, 0 when closed). UpdateZelda uses that as a dynamic HUD split: HUD strip -> layer 4 wherever it is, item screen above + play area below -> layer 1, subscreen sprites (cursor) -> layer 3; extrusion sits out whenever `$e1` != 0. UpdateZelda renders title/menus/game-over as simple flat BG + sprites.
- Screen-scroll transitions stay fully 3D via the vendored FCEUmm core's optional `retro_get_holo_bg_tile_ids` extension. The first live-filter implementation still used later save-state replays: it fixed the incoming-row stripes, but FCEUmm does not serialize every live PPU pixel-pipeline latch, so a mid-transition obstacle layer measured about 27 pixels out of phase with the authoritative ground and visibly slid in from the opposite direction. Do not restore that replay path for scrolling. The current core records the source nametable tile ID for every output pixel while it renders the ONE authoritative background frame. HoloVCS uses those IDs and the existing keep lists to copy obstacle pixels from that exact RGB frame into layers 2-4 during the same video callback. Ground, obstacles, mid-frame nametable writes, and fine X/Y scroll therefore share one pixel position and one time step by construction. The older `retro_set_holo_bg_tile_filter` extension remains only for stationary compatibility with an intermediate core; a DLL without the same-frame tile-ID export falls back to 2D during scrolls. Verified Aug 2026 by exact pixel comparison: layer 2 matched layer 1 at every opaque obstacle pixel across stationary output, six horizontal transition samples, and nine vertical samples through completion. Layer 3 differed only where intended sprites overwrite it.
- GOTCHA: the `dump` command executes inside UpdateNES, which is skipped while paused - a `dump` sent after `pause` stays pending until `unpause`. `dumplayers` and `shot` work fine while paused (they read existing buffers).
- Overworld PALRAM[0] (backdrop) = 0x0f BLACK. Ground is real tile art (tan via palette 2), NOT the backdrop color.
- Historical compatibility GOTCHA: `SetTileColorIndex` (forcing PALRAM[0] in the save-state buffer) does NOT survive a BG-on replay in Zelda because the game rewrites the palette from its RAM shadow every frame. It DOES hold for the sprites-off fill (`"01"` pass), same as Castlevania. The current same-frame obstacle path needs no replacement backdrop; it copies only allowed tile pixels from the authoritative RGB frame. The sprite pass still keeps the grey $00 trick. Proven with `dumplayers`.
- Dungeon plan: same keep-list mechanism; detect overworld vs dungeon via CPU RAM $0010 in the dump (0x00 on the overworld) if tile IDs collide. Needs a dungeon `.sav0` checkpoint first.

## Unreal Engine 5.8 "Unreal MCP" editor plugin (editor-side complement)

UE 5.8 ships an Experimental first-party Model Context Protocol server
(`Engine/Plugins/Experimental/ModelContextProtocol`, plus the `AllToolsets` aggregator: editor
tools for assets, automation tests, Slate inspection, config, semantic search...). Only
**ModelContextProtocol** is enabled in **HoloVCS_Flat.uproject** (LKG uproject untouched).
GOTCHA: enabling `AllToolsets` in the uproject BREAKS THE SHIPPING COOK - it transitively
enables GameFeatures, whose GameFeatureData asset-manager rule the project lacks ("Asset
manager settings do not include a rule for assets of type GameFeatureData", 30 cook errors).
Enable AllToolsets only temporarily via Edit > Plugins for an editor session, and turn it back
off before cooking (or add the PrimaryAssetTypesToScan entry if it ever becomes permanent).
The server listens on `http://127.0.0.1:8000/mcp`, loopback only, EDITOR process only: it does
not run in -game or staged builds, which is why the file harness above exists and is the
primary channel. To use it: open the editor, enable Auto Start Server in Editor Preferences >
Model Context Protocol (or run `ModelContextProtocol.StartServer`), and generate a client
config with the console command `ModelContextProtocol.GenerateClientConfig ClaudeCode` (writes
`.mcp.json` in the project root; already done once). Coding tools started in the project root
then get the editor toolset whenever the editor is running.
