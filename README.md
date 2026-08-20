# HoloVCS

Atari 2600, NES, and Virtual Boy games rendered as stacked 3D layers on Looking Glass holographic displays.

![HoloVCS](Media/holovcs.gif)

## What is it?

HoloVCS turns classic games into small 3D dioramas. Instead of displaying a single 2D emulator image, it separates backgrounds, platforms, characters, effects, and foreground objects into textured layers placed at different depths. On a Looking Glass display, the result can be viewed from different horizontal angles without glasses or a headset.

The Looking Glass build has been tested on three devices:

- Looking Glass Portrait
- Looking Glass Go
- Original 8.9-inch Looking Glass

Looking Glass Bridge supplies the calibration for the connected display, and HoloVCS automatically selects the matching quilt layout and framing.

## Features

- Atari 2600, NES, and Virtual Boy emulation through customized libretro cores.
- Hand-tuned 3D profiles for supported Atari 2600 and NES games, identified by checksum rather than filename.
- Native Virtual Boy depth layers captured directly from the customized Beetle VB core.
- Layered lighting, shadows, backdrop images, adjustable depth, save states, keyboard controls, and gamepad support.
- A customized Looking Glass Unreal Engine 5.8 plugin with automatic device calibration and a project-specific high-speed quilt renderer.
- Complete HoloVCS, emulator-core, and Looking Glass plugin source included in this repository.

The custom sprite-quilt renderer reduced the measured render-thread cost of generating a quilt from about 85 ms to 0.7 ms, roughly 100 times faster than the plugin's general scene-capture path for this project. It achieves that speed by drawing HoloVCS's known stack of textured rectangles directly instead of rendering a complete Unreal scene for every view. It is not a general-purpose replacement for the normal Looking Glass renderer and falls back to that renderer when a scene does not meet its constraints.

## Videos

**Super Mario Bros. and Castlevania**

<a href="https://www.rtsoft.com/files/HoloNes.mp4"><img src="Media/holones_video_thumb.jpg" alt="Play HoloVCS running Super Mario Bros. and Castlevania on a Looking Glass Portrait" width="640"></a>

**Virtual Boy**

<a href="https://www.youtube.com/watch?v=QpQSTgjk4N4"><img src="Media/vboy_youtube_thumb.jpg" alt="Play HoloVCS running Virtual Boy games" width="640"></a>

## Latest versions

### Version 1.4 - August 20, 2026

- Ported HoloVCS and its customized Looking Glass plugin to Unreal Engine 5.8.
- Added the complete modified source for all three emulator cores to the repository.
- Added a layer profile for Legend Of Zelda (NES).
- Added much faster project-specific quilt rendering and self-rendered Looking Glass output for smoother play.
- Confirmed support on the Looking Glass Portrait, Looking Glass Go, and original 8.9-inch Looking Glass, and improved checksum-based ROM startup.

## Download and play

Download the ready-to-run Looking Glass Windows build: [HoloVCS_Win64.zip](https://www.rtsoft.com/files/HoloVCS_Win64.zip).

The release does not contain copyrighted game ROMs. Put your own `.a26`, `.nes`, and `.vb` files in the `atari2600`, `nes`, and `vb` directories respectively. Press `?` in the game to see the current controls.

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

HoloVCS code and media use the BSD-style attribution license in [LICENSE.md](LICENSE.md). The emulator cores are GPL-2.0 projects with their own license files. They remain separate DLLs loaded at runtime so GPL code is not linked into the Unreal binary. The vendored Looking Glass plugin is MIT licensed.

ROMs are not included.

## Current game support

HoloVCS has hand-tuned profiles for Pitfall!, Super Mario Bros., Castlevania, Tetris, and The Legend of Zelda. Wario Land and Jack Bros. have Virtual Boy-specific setup, and other Virtual Boy games still use the core's native split-layer data. Unrecognized Atari 2600 and NES games run with default layering rather than a hand-tuned effect.

## AI Disclosure

This project was developed with assistance from AI tools. I mean, you can still blame me (Seth) for bugs, but just wanted to mention it.

## Credits

- Written by Seth A. Robinson (my sites: [rtsoft.com](https://www.rtsoft.com/) and [Codedojo](https://www.codedojo.com/))
- Atari 2600 emulation via [Stella](https://github.com/stella-emu/stella)
- NES emulation via [FCEUmm](https://github.com/libretro/libretro-fceumm)
- Virtual Boy emulation via [Beetle VB](https://github.com/libretro/beetle-vb-libretro)
- A big thanks to Looking Glass Factory for making their [Unreal plugin](https://github.com/Looking-Glass/Looking-Glass-Unreal-Plugin) open source
- Moon image by Stephen Rahn under a [public-domain license](https://www.flickr.com/photos/srahn/16542943668/in/photostream)
