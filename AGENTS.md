# AGENTS.md - HoloVCS project knowledge for AI tools

Read this before working on the repo. Keep it updated when you learn something durable.

## What this is

HoloVCS runs Atari 2600 / NES / Virtual Boy emulators (libretro cores) and renders each game as
stacked textured quads at different depths ("diorama" effect), originally for Looking Glass
holographic displays. Author: Seth Robinson (rtsoft.com).

## Two uprojects, one source tree

| File | Purpose | Engine |
|---|---|---|
| `HoloVCS_Flat.uproject` | Flat build for a normal monitor, no plugin. Day-to-day dev. | UE 5.8 (`F:\UnrealEngine\UE_5.8`) |
| `HoloVCS.uproject` | Looking Glass hardware build (plugin enabled) | UE 5.6 only; 5.6 is NOT currently installed |

Build the flat editor target from the command line:

```
F:\UnrealEngine\UE_5.8\Engine\Build\BatchFiles\Build.bat HoloVCSEditor Win64 Development -project="f:\Unreal\HoloVCS_UE56\HoloVCS_Flat.uproject" -waitmutex
```

Run it: `F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe HoloVCS_Flat.uproject -game -windowed -resx=1280 -resy=720`
The default map is `Content/Maps/NewMap_Flat.umap`; the hardware map `NewMap.umap` still contains the
LookingGlassCapture actor and should NOT be resaved in 5.8 (the actor's class is missing there and a
resave would strip it).

**One-way door:** any .uasset/.umap resaved by the 5.8 editor becomes unreadable to UE 5.6. The
restore point for a future 5.6 + Looking Glass build is the "UE 5.6 migration baseline" commit.

## Looking Glass status

- The plugin supports max UE 5.6 (latest release 2.1.1, open source:
  github.com/Looking-Glass/Looking-Glass-Unreal-Plugin). No 5.7/5.8 release exists.
- A full copy WITH source survives in the engine remnant at
  `F:\UnrealEngine\UE_5.6\Engine\Plugins\LookingGlass` (the 5.6 engine itself was uninstalled).
- The C++ game module has ZERO compile-time dependency on the plugin. The only coupling is the
  uproject plugin entry, the capture actor in NewMap.umap, and the
  `[/Script/LookingGlassRuntime.LookingGlassSettings]` block in DefaultEngine.ini.
- Porting the plugin to 5.8 is possible but real work: its
  `LookingGlassSceneCaptureRendering.cpp` forks engine scene-capture internals that churn each release.
- The flat camera: `APlayerPawn` owns a `UCameraComponent` root (FOV 14).
  `FitFlatCameraToLayers()` (called at the end of `InitLayers`) auto-frames whatever layer stack the
  current emulator spawned - essential because each system uses wildly different world scales
  (NES layers are ~41 units, Atari ~445 units offset +69 in Y, VB ~310). A fixed camera distance can
  only ever fit one system; the others look "black" (staring past the layers) or hugely zoomed.
  When the LG plugin is active it takes over the viewport, so the pawn camera is inert on hardware.

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
  Materials/Material.h, Engine/Engine.h, HAL/FileManager.h, Misc/FileHelper.h).
- `ISoundGenerator::GetNumChannels()` is `const` in 5.8; a non-const override compiles but silently
  never gets called (audio reports 0 channels).
- Target.cs files must use `DefaultBuildSettings = BuildSettingsVersion.V7` on 5.8 or the editor
  shows a "Target Upgrade Required" popup on every launch.
- `Config/DefaultEngine.ini` has `Compiler=Default` under WindowsTargetSettings; do not pin a VS
  version there (the machine has VS2026, and pinning VisualStudio2022 breaks the build).
- The map instance's stored root transform overrides class-default component transforms, which is why
  the camera framing is applied additively in BeginPlay instead of via component defaults.

## Secrets policy

Android signing (KeyStore path/alias/passwords) and the AndroidFileServer SecurityToken live in
`Config/UserEngine.ini`, which is gitignored. NEVER put them in Config/DefaultEngine.ini or any
tracked file; this repo has a public GitHub remote. The keystore file itself lives outside the repo
(`D:\projects\protonGITFull\AppleStuff\android\`) and must stay out of git everywhere.

## Packaging / release

- `PackageWin64Release.bat` packages the FLAT version for UE 5.8 (stages into `dist\win64_release\Windows`,
  scrubs ROMs before zipping, signs binaries, produces HoloVCS_Win64.zip). It needs
  `..\base_setup.bat` (defines PROJECT_DIR, RT_PROJECTS, PROTON_DIR) and `app_info_setup.bat`
  (APP_NAME/APP_DIR/UE_DIR).
- `UploadReleaseToRTsoft.bat` SCPs the zip to rtsoft.com.
- The Android port was dropped entirely (scripts, config, and the static-core hack that existed for
  it). If it ever comes back, the cores must ship as separate .so files there too, same GPL reason.

## Writing style for this repo

No em-dashes in prose, commits, or docs. Commits and PRs are authored by Seth only; never add
AI attribution lines (no Co-Authored-By Claude, no "Generated with" footers).
