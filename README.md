HoloVCS - Atari 2600 and NES games with 3D layers on the Looking Glass Portrait holographic device

![](https://www.codedojo.com/wp-content/uploads/2021/08/holovcs.gif)

License:  BSD style attribution, see LICENSE.md

[A twitter movie of it in action](https://twitter.com/rtsoft/status/1426128382639501320)

**You a player?** Then you probably want to visit the [user page](https://www.codedojo.com/?p=2704), it has the ready to run [download version](https://www.rtsoft.com/files/HoloVCS_Win64.zip).


You a developer and want to compile the project?  Read on

**Steps to compile**

* You should have Visual Studio 2022 installed (I use the free community edition)
* You should have Unreal Engine installed.  (I used 4.27.2)
* Download the [Holoplay plugin for Unreal Engine](https://lookingglassfactory.com/software)
* Created a plugins subdir in the HoloVCS dir and drag the Holoplay folder into it
* Contact Seth to get two missing source files - (SynthComponentSethHack.cpp and SynthComponentSethHack.h) - You must have accepted the UE4 developer license before I can share those as they contain engine code, it's a legal thing.  What am I doing with engine code? It's a method I used to work around a UE4 bug with changing audio sample rate on the fly, an alternative is you can just rename things to use SynthComponent instead, it's pretty easy to do, you just want be able to change the sample rate of the synth generator buffer.
* Right click the HoloVCS.uproject file and choose "Generate VStudio project"
* Open the HoloVCS.sln file and make sure the active project is HoloVCS 64bit, set to Development_Editor and press F5 to run.

That should do it! It should work and warn you about a missing .rom file which you should add.

It uses special version of stella_libretro.dll which is included in Binaries/Win64.  To build this, I checked out [Stella](https://github.com/stella-emu/stella) and then made a few small hacks so we could do the 3d stuff.  Those changes I made are the .dif file in the [StellaModifications](https://github.com/SethRobinson/HoloVCS/blob/main/StellaModifications/StellaModification.dif) dir in case you want to build your own version.
For NES support, it uses a barely modified version of fceumm_libretro.dll, I added a way to enable/disable bg/sprite rendering.


**Cool stuff to have**

I'm lazy and probably won't do much, but here are some things that would be nice:

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
- Moon image by Stephen Rahn [Public domain license](https://www.flickr.com/photos/srahn/16542943668/in/photostream)