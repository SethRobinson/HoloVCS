# HoloVCS

Atari 2600, NES, Virtual Boy, and Nintendo 3DS games rendered as stacked 3D layers on Looking Glass holographic displays.

![HoloVCS](Media/holovcs.gif)

<a href="#download-and-play"><img src="https://img.shields.io/badge/Download_for_Windows-v1.5-2ea44f?style=for-the-badge&logo=windows&logoColor=white" alt="Download HoloVCS for Windows" height="48"></a>

## What is it?

HoloVCS turns classic games into small 3D dioramas. Instead of displaying a single 2D emulator image, it separates backgrounds, platforms, characters, effects, and foreground objects into textured layers placed at different depths. On a Looking Glass display, the result can be viewed from different horizontal angles without glasses or a headset.

The Looking Glass build has been tested on three devices:

- Looking Glass Portrait
- Looking Glass Go
- Original 8.9-inch Looking Glass

Also note that the 3D effects mostly only work with a couple hand-tweaked games at the time of this writing. (The exceptions are Virtual Boy and Nintendo 3DS, which are inherently 3D, so every game gets real depth)

## Features

- Atari 2600, NES, and Virtual Boy emulation through customized libretro cores.
- Hand-tuned 3D profiles for supported Atari 2600 and NES games, identified by checksum rather than filename.
- Native Virtual Boy depth layers captured directly from the customized Beetle VB core.
- Nintendo 3DS support: a customized Azahar-based core captures true per-pixel depth from the emulated 3D GPU, so games need no hand-tuned profiles.
- Layered lighting, shadows, backdrop images, adjustable depth, save states, keyboard controls, and gamepad support.
- A customized Looking Glass Unreal Engine 5.8 plugin with automatic device calibration and a project-specific high-speed quilt renderer.
- Complete HoloVCS, emulator-core, and Looking Glass plugin source included in this repository.

The custom sprite-quilt renderer reduced the measured render-thread cost of generating a quilt from about 85 ms to 0.7 ms, roughly 100 times faster than the plugin's general scene-capture path for this project. It achieves that speed by drawing HoloVCS's known stack of textured rectangles directly instead of rendering a complete Unreal scene for every view. It is not a general-purpose replacement for the normal Looking Glass renderer and falls back to that renderer when a scene does not meet its constraints.

## Nintendo 3DS support

Unlike the 2D systems, the 3DS is a true 3D console, so no hand-tuned per-game profiles are needed. A customized emulator core captures real per-pixel depth from the emulated GPU: during the game's own rendering, every drawn fragment is also routed into one of 24 depth-banded layers, so even geometry hidden behind foreground objects is preserved on its own layer and the diorama holds up when viewed from an angle. The bottom screen appears on its own panel below the 3D stack, with touch input driven by the mouse or gamepad.

The core is based on [Azahar](https://github.com/azahar-emu/azahar), the open-source continuation of Citra; the modified source lives on the `holo` branch of [SethRobinson/azahar](https://github.com/SethRobinson/azahar). Like the other cores it is GPL-2.0 and remains a separate DLL loaded at runtime. The core accepts only decrypted ROM dumps, and as with the rest of HoloVCS, no ROMs, encryption keys, or decryption capability are included.

## Videos

**Super Mario Bros. and Castlevania**

<a href="https://www.rtsoft.com/files/HoloNes.mp4"><img src="Media/holones_video_thumb.jpg" alt="Play HoloVCS running Super Mario Bros. and Castlevania on a Looking Glass Portrait" width="640"></a>

**Virtual Boy**

<a href="https://www.youtube.com/watch?v=QpQSTgjk4N4"><img src="Media/vboy_youtube_thumb.jpg" alt="Play HoloVCS running Virtual Boy games" width="640"></a>

## Latest versions

### Version 1.5 - August 27, 2026

- Added Nintendo 3DS support through a customized Azahar core: every game gets true per-pixel depth captured from the emulated GPU during its own rendering, so no per-game profiles are needed. Includes the bottom screen on its own panel, a mouse or stick driven touch cursor, and analog circle pad support.
- 3DS dumps that are merely flagged as encrypted (common with dumping tools) now load automatically; only genuinely encrypted dumps are refused, with a clear on-screen message.
- The F and G keys now save and load state directly as documented, and the savestate hotkeys say so on systems without savestate support instead of failing silently.
- The 3D depth defaults to 90% on the 3DS for better framing on the device; manual depth adjustments still stick for the session.
- Added a debug fly camera (V key or Start + left stick click) and scripted camera moves for capturing footage.

### Version 1.4 - August 20, 2026

- Ported HoloVCS and its customized Looking Glass plugin to Unreal Engine 5.8.
- Added the complete modified source for all three emulator cores to the repository.
- Added a layer profile for Legend Of Zelda (NES).
- Added much faster project-specific quilt rendering and self-rendered Looking Glass output for smoother play.
- Confirmed support on the Looking Glass Portrait, Looking Glass Go, and original 8.9-inch Looking Glass, and improved checksum-based ROM startup.

## Download and play

Download the ready-to-run Looking Glass Windows build: [HoloVCS_Win64.zip](https://www.rtsoft.com/files/HoloVCS_Win64.zip).  (The executable/dlls are codesigned by me, Seth A. Robinson)

The release does not contain copyrighted game ROMs. Put your own `.a26`, `.nes`, `.vb`, and `.3ds`/`.cci` files in the `atari2600`, `nes`, `vb`, and `3ds` directories respectively. (3DS dumps must be decrypted.)

HoloVCS recognizes games by ROM checksum instead of filename, so you can name the files anything, but the revision has to match. These are the exact versions with hand-tuned 3D layer profiles:

| Directory | Game | ROM version |
| --- | --- | --- |
| `atari2600` | Pitfall! | `Pitfall! (1982) (Activision) [!].a26` |
| `nes` | Super Mario Bros. | `Super Mario Bros. (World).nes` |
| `nes` | Castlevania | `Castlevania (USA) (Rev A).nes` |
| `nes` | Tetris | `Tetris (USA).nes` |
| `nes` | The Legend of Zelda | `Legend of Zelda, The (PRG 1).nes` |
| `vb` | Virtual Boy Wario Land | `Virtual Boy Wario Land (Japan, USA).vb` |
| `vb` | Jack Bros. | `Jack Bros.vb` |

Any Virtual Boy or Nintendo 3DS game works, no per-game profile needed: the customized Beetle VB and Azahar cores hand HoloVCS real depth layers. Other Atari 2600 and NES games load and play fine, they just get default layering instead of a hand-tuned depth effect.

Press `?` in the game to see the current controls, and `,` or `.` to switch between the ROMs you installed.

HoloVCS requires a connected Looking Glass display and [Looking Glass Bridge](https://lookingglassfactory.com/software-downloads).


## Looking Glass integration

The project uses Unreal Engine 5.8. `HoloVCS.uproject` enables the vendored, UE 5.8-ported Looking Glass plugin in `Plugins/LookingGlass` and renders to Looking Glass hardware. No separate Unreal plugin download is required.

The [upstream Looking Glass Unreal plugin](https://github.com/Looking-Glass/Looking-Glass-Unreal-Plugin) does not currently ship official UE 5.8 support, so this repository carries the tested port and its HoloVCS-specific rendering improvements.

## Build from source on Windows

Everything HoloVCS-specific needed to compile is checked into this repository, including the complete modified source for all three emulator cores and the Looking Glass plugin. There are no submodules or Git LFS source dependencies.

Prerequisites:

- Unreal Engine 5.8
- Visual Studio 2022 or newer with Desktop development with C++, the MSVC v143 toolset, and a Windows 10 or 11 SDK
- Looking Glass Bridge 2.5.1 or newer
- Your own legally obtained ROMs for runtime testing

Build steps:

1. Run `BuildCores.bat`. It builds all three libretro DLLs from `cores/` and copies them to `Binaries/Win64`.
2. Right-click `HoloVCS.uproject` and choose **Generate Visual Studio project files**.
3. Open `HoloVCS.sln`, select **Development Editor**, and build `HoloVCSEditor`.
4. Open `HoloVCS.uproject`, or run one of the Shipping build scripts below.

Useful scripts:

- `BuildAndRunWin64LKG.bat` incrementally stages and launches the Looking Glass Shipping build for local testing. Pass `nolaunch` to build without launching.
- `PackageWin64LKGRelease.bat` performs a clean Looking Glass release stage, rebuilds the cores, signs the executables and core DLLs, excludes ROMs and save states, and creates `HoloVCS_Win64.zip`.

The local test stage includes ROMs from your working directories and must never be distributed. The release packaging script only copies the placeholder text files from the ROM directories.

## Emulator cores and licensing

The patched core sources are vendored under `cores/`:

- `cores/stella`: Atari 2600, based on [Stella](https://github.com/stella-emu/stella), with selective VCS hardware-sprite rendering. The project-specific changes are also captured in [StellaModification.dif](StellaModifications/StellaModification.dif).
- `cores/fceumm`: NES, based on [FCEUmm](https://docs.libretro.com/library/fceumm), with controllable background and sprite rendering plus HoloVCS layer metadata.
- `cores/beetle-vb`: Virtual Boy, based on [Beetle VB](https://github.com/libretro/beetle-vb-libretro), returning pre-split 3D layers through `mednafen/vb/HoloVB.h`.

The Nintendo 3DS core is based on [Azahar](https://github.com/azahar-emu/azahar) and is maintained on the `holo` branch of [SethRobinson/azahar](https://github.com/SethRobinson/azahar) rather than under `cores/`.

HoloVCS code and media use the BSD-style attribution license in [LICENSE.md](LICENSE.md). The emulator cores are GPL-2.0 projects with their own license files. They remain separate DLLs loaded at runtime so GPL code is not linked into the Unreal binary. The vendored Looking Glass plugin is MIT licensed.

ROMs are not included.

## AI Disclosure (applies to code written in August 2026+)

This project was developed with assistance from AI tools. I mean, you can still blame me (Seth) for bugs, but just wanted to mention it.

## Credits

- Written by Seth A. Robinson (my sites: [rtsoft.com](https://www.rtsoft.com/) and [Codedojo](https://www.codedojo.com/))
- Atari 2600 emulation via [Stella](https://github.com/stella-emu/stella)
- NES emulation via [FCEUmm](https://github.com/libretro/libretro-fceumm)
- Virtual Boy emulation via [Beetle VB](https://github.com/libretro/beetle-vb-libretro)
- A big thanks to Looking Glass Factory for making their [Unreal plugin](https://github.com/Looking-Glass/Looking-Glass-Unreal-Plugin) open source
- Moon image by Stephen Rahn under a [public-domain license](https://www.flickr.com/photos/srahn/16542943668/in/photostream)
