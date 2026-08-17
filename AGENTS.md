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

| File | Purpose | Engine |
|---|---|---|
| `HoloVCS_Flat.uproject` | Flat build for a normal monitor, no plugin. Day-to-day dev. | UE 5.8 (`F:\UnrealEngine\UE_5.8`) |
| `HoloVCS.uproject` | Looking Glass hardware build (vendored ported plugin enabled) | UE 5.8 (same engine) |

Build the flat editor target from the command line:

```
F:\UnrealEngine\UE_5.8\Engine\Build\BatchFiles\Build.bat HoloVCSEditor Win64 Development -project="f:\Unreal\HoloVCS_UE56\HoloVCS_Flat.uproject" -waitmutex
```

The hardware variant builds the same way with `-project="f:\Unreal\HoloVCS_UE56\HoloVCS.uproject"`
(also verified with `HoloVCS Win64 Shipping`).

Run flat: `F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe HoloVCS_Flat.uproject -game -windowed -resx=1280 -resy=720`
The default map is `Content/Maps/NewMap_Flat.umap`. The hardware map `NewMap.umap` (project root
`/Game/NewMap`) holds the LookingGlassCapture actor; its class exists again now that the plugin is
vendored, so it loads fine in 5.8.

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
- NOT yet verified: actual holographic output (quilt on device) - needs the display plugged in.
  The F9 quilt-screenshot hotkey did not respond in -game main-viewport mode without a device.
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
- An editor-binary `-game` run has WITH_EDITOR code compiled in but GUnrealEd == nullptr; any
  WITH_EDITOR block reachable in game mode must null-check it.

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
- Standing preference (Seth): after finishing code changes to the flat version, ALWAYS build the
  Shipping test build (BuildAndRunWin64Release.bat) and end the handoff with a clickable link to
  the staged exe so Seth can try it. The link must use the FULL absolute path as a file:/// URI
  (`file:///F:/Unreal/HoloVCS_UE56/dist/win64_test/Windows/HoloVCS.exe`), not a workspace-relative
  path - relative links open nothing. Also print the plain path for copy/paste. Kill any instance
  you launched for verification first.
- `UploadReleaseToRTsoft.bat` SCPs the zip to rtsoft.com.
- The Android port was dropped entirely (scripts, config, and the static-core hack that existed for
  it). If it ever comes back, the cores must ship as separate .so files there too, same GPL reason.

## Writing style for this repo

No em-dashes in prose, commits, or docs.

## Asset creation tools

- If seths_game_asset_creation.txt exists in the same folder as this file, read it
