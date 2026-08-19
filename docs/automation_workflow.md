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
| `key <FKeyName> [ticks]` | Synthesized KEYBOARD press through the full UE input path (Slate -> viewport -> PlayerInput -> pawn bindings), held N engine ticks (default 2). FKey names: `SpaceBar`, `LeftControl`, `Enter`, `Tab`, `W`, `One`, `Slash`... Use this to test hotkeys and the real key-to-joypad mappings headlessly; `press` bypasses all of that. Works without window focus and while paused (so it can dismiss the help screen). |
| `shot [name]` | Engine screenshot (FScreenshotRequest) to `Saved/Automation/shots/<name>.png`. Captures the viewport even when the window is behind other windows. Do NOT minimize the window though. |
| `video <frames> [name]` | Per-frame screenshots to `Saved/Automation/video/<name>/frame_%05d.png`. Halves the frame rate while active (~28 fps); fine for short clips. No ffmpeg on this machine; read frames directly or assemble elsewhere. |
| `dump` | NES state dump (see below). |
| `dumplayers` | Writes each depth layer's CPU texture buffer to `Saved/Automation/layers/layer_N.png` (layer 0 = deepest/back wall side, highest index = nearest the viewer; NES games use 5 layers, VB 16). Ground truth for what each plane holds and where content sits in 3D; this is the tool that found the Zelda colorkey bug and the transition stripe after composite screenshots had been ambiguous. Transparent areas = png alpha. |
| `pause` / `unpause` | Emulator pause (UE keeps ticking, shots still work). |
| `help [on\|off]` | Show/hide the help screen (no arg toggles). Same as the ? hotkey: pauses the game while up; `unpause` also closes it. |
| `savestate` / `loadstate` | The same `<rom>.sav0` files as the S/L hotkeys. |
| `rom <partial>` | Live ROM switch by partial filename match. |
| `exec <console cmd>` | Anything the UE console accepts (`r.ShadowQuality 0`, `HighResShot 2`, cvars...). |
| `quit` | Clean exit. |

Workflow rules learned:
- Launch is the only focus grab: `UnrealEditor.exe HoloVCS_Flat.uproject -game -windowed -resx=1280 -resy=720 -rom=<partial>`. After that, never foreground the window; the old SendKeys/CopyFromScreen approach is retired (Seth uses the machine while automated work runs).
- Wait for `harness ready` in `Saved/Automation/ai_log.txt` before sending commands (delete the log first for a clean signal).
- STAGED SHIPPING BUILDS BOOT PAUSED: the help screen auto-shows on the first live frame and pauses
  the emulator. Send `help off` first (its ack `help now off` is unreliable evidence though - it
  prints the current state even when there was nothing to hide), and confirm via log.txt's
  `SetGamePaused: 1 -> 0` line. While paused, `press` holds sit pending and the game looks
  input-dead. Editor -game runs never auto-show.
- A running -game instance still blocks Build.bat; kill the process, build, relaunch, `loadstate`.
- A running STAGED instance locks `dist\win64_test` and fails BuildAndRunWin64Release.bat with Error_FailedToDeleteStagingDirectory: `Get-Process HoloVCS*` and kill before staging. That bat has NO `nolaunch` flag (only the LKG one does) and always launches the staged exe at the end.
- Both test bats preserve `*.sav0` save states across restages (dist\savstate_keep_* backup/restore around the UAT call); PackageWin64Release.bat scrubs *.sav0 from real releases.
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
- The `.sav0` checkpoints work the same (files sit next to HoloVCSLKG.exe in
  `dist\win64_lkg_test\Windows\`; the Zelda overworld one is seeded there and both test bats
  preserve them across restages).
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
- Overworld tile IDs (from N-key dumps): `0x24` blank, `0x26` walkable ground, `0xf3` cave mouth. Extrusion runs TWO masked re-renders: wall trees (`d8-db`) keyed on black only, and a ground-keyed set (standalone trees `c4-c7`, standalone rocks `c8-cb`, forest-edge diagonals `cc-d7`/`dc-df`) keyed on black PLUS the ground color `(255,255,74)` (NES `$37` as the rgb palette renders it, sampled from the layer buffer) so ground-painted parts stay flat and only the object shape extrudes. The ground color must NOT be keyed on the wall-tree pass: their art uses it as a highlight (24% of pixels, measured by scanning a dumplayers png). Tombstones/armos/dungeon walls still to be dumped and appended.
- Game mode byte RAM `$12` (all values dumped empirically): 0 = title/attract, 1 = file select menus, 5 = gameplay incl. subscreen, 7 = screen-scroll transition (`$13` holds a phase/direction value; 4 and 6 are treated as scroll too), 8 = game over.
- Item subscreen (Start) detection (dumped Aug 2026): RAM `$e1` = 0 closed, 7 sliding open, 8 open at rest, 9 sliding closed (`$12` stays 5 throughout, so `$e1` is the only signal). While `$e1` != 0, `$fc` holds the PPU vertical scroll; the HUD strip's top OUTPUT row = `(240 - $fc) % 240` on every frame of the slide (175 at rest-open, 0 when closed). UpdateZelda uses that as a dynamic HUD split: HUD strip -> layer 4 wherever it is, item screen above + play area below -> layer 1, subscreen sprites (cursor) -> layer 3; extrusion sits out whenever `$e1` != 0. UpdateZelda renders title/menus/game-over as simple flat BG + sprites, and extrusion sits out the scroll modes (2D slide, pops back on arrival): Zelda copies fresh nametable rows DURING the frame the masked re-render emulates, bypassing the tile filter. A 3D-through-the-scroll variant (ground-keying the wall trees during scrolls) was tried Aug 2026 and looked bad on the device - don't re-attempt without a new idea. Diagnosed by pausing mid-transition and reading dumplayers output.
- GOTCHA: the `dump` command executes inside UpdateNES, which is skipped while paused - a `dump` sent after `pause` stays pending until `unpause`. `dumplayers` and `shot` work fine while paused (they read existing buffers).
- Overworld PALRAM[0] (backdrop) = 0x0f BLACK. Ground is real tile art (tan via palette 2), NOT the backdrop color.
- GOTCHA: `SetTileColorIndex` (forcing PALRAM[0] in the save-state buffer) does NOT survive a BG-on re-render in Zelda: the game rewrites the palette from its RAM shadow every frame, so the masked obstacle pass renders the true black backdrop no matter what. It DOES hold for the sprites-off fill (`"01"` pass), same as Castlevania. Hence: obstacle pass keys COLOR_KEY_STYLE_BLACK (safe: Zelda's backdrop is black everywhere), sprite pass keeps the grey $00 trick. Proven with `dumplayers`.
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
