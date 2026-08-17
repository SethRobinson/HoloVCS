HoloVCS - Atari 2600, NES, & Virtual Boy games with 3D layers on the Looking Glass Portrait holographic device

![](Media/holovcs.gif)


<a href="https://www.youtube.com/watch?v=QpQSTgjk4N4"><img align="top" src="Media/vboy_youtube_thumb.jpg" width=800></a>

License:  BSD style attribution, see LICENSE.md.  Exception: the emulator cores under `cores/` (FCEUmm, Stella, Beetle VB) are GPL-2.0 projects with their own license files.  They build as separate DLLs and are loaded at runtime, keeping the GPL code out of the Unreal binary.

[A twitter movie of Super Mario Bros/Castlevania in action](https://twitter.com/rtsoft/status/1489125302877900806)

[A twitter movie of Virtualboy in action](https://twitter.com/rtsoft/status/1542285198443683841)


**You a player?** Then you probably want to visit the [user page](https://www.codedojo.com/?p=2704), it has the ready to run [download version](https://www.rtsoft.com/files/HoloVCS_Win64.zip).

Check the user page linked above for which games are currently supported.

You a developer and want to compile the project?  Read on

**Two versions from one source**

The project now has two .uproject files sharing the same Source/ and Content/:

* `HoloVCS_Flat.uproject` - Renders to a normal monitor with a regular camera (no special hardware needed). Uses Unreal Engine 5.8. This is the one to use for day to day dev and testing.
* `HoloVCS.uproject` - The Looking Glass hardware version. The Looking Glass plugin only supports up to UE 5.6 (no 5.7/5.8 release exists as of this writing), so using this requires installing UE 5.6 and the [Looking Glass Unreal plugin](https://github.com/Looking-Glass/Looking-Glass-Unreal-Plugin). Warning: assets resaved by the 5.8 editor can't be opened in 5.6 anymore, so if you go back to 5.6 use content from the "UE 5.6 migration baseline" commit or earlier.

**Steps to compile (flat version)**

* You should have Visual Studio 2022 or newer installed (I use the free community edition)
* You should have Unreal Engine 5.8 installed
* Run `BuildCores.bat` - this builds the three emulator core DLLs from `cores/` and drops them into Binaries/Win64
* Right click the HoloVCS_Flat.uproject file and choose "Generate Visual Studio project files"
* Open the HoloVCS.sln file, set to Development Editor and press F5 to run, or just double click HoloVCS_Flat.uproject to open the editor

It should warn you about missing .rom files which you should add (nes, atari2600 and vb dirs in the project root).

The patched emulator sources all live in this repo under `cores/`:

* `cores/stella` - Atari 2600.  [Stella](https://github.com/stella-emu/stella) with a few small hacks so we can render VCS hardware sprites selectively per pass.  The changes are also captured as a .dif in [StellaModifications](StellaModifications/StellaModification.dif).
* `cores/fceumm` - NES.  A barely modified FCEUmm, I added a way to enable/disable bg/sprite rendering.
* `cores/beetle-vb` - Virtual Boy.  A modified beetle-vb core that returns pre-split 3D layers (see mednafen/vb/HoloVB.h for the shared struct).


**Cool stuff to have**

I'm lazy and probably won't do much, but here are some things that would be nice:

* Improve 3d effects/compatibility with games
* Make faster
* Support more systems (not too hard considering we support the libretro core format, but each would require customizations to make 3D type stuff work)
* Do builds for other 3D displays like Vive/Oculus/Hololens?  It would actually be pretty simple and run much faster considering we only need stereo rendering vs the 30+ frames we render for Looking Glass.  Not sure how to set things up to be able to do builds for all of those at once with the same project though.
* Improve game/audio timing, it's tricky because you can't always assume you'll hit 60fps or a reasonable frame division with how intensive the holographic rendering is

Have some free time and the wherewithal to fix things up to work better or add features? Please feel free to submit submit bug reports, code, patches or whatever!

Credits and links
- Written by Seth A. Robinson (seth@rtsoft.com) twitter: @rtsoft - [Codedojo](https://www.codedojo.com), Seth's blog
- Can't get enough Pitfall!? Who the heck can?!  Check out my [Atari console that runs Pitfall! off a piece of paper](https://www.codedojo.com/?p=2251)
- Atari 2600 emulation via [Stella](https://github.com/stella-emu/stella)
- NES emulation via [FCEUmm](https://docs.libretro.com/library/fceumm)
- Virtual Boy emulation via [Beetle VB](https://github.com/libretro/beetle-vb-libretro)
- Moon image by Stephen Rahn [Public domain license](https://www.flickr.com/photos/srahn/16542943668/in/photostream)
