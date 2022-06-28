HoloVCS - Atari 2600 and NES emulation that can play specific pre-setup games in a weird psuedo 3D on the Looking Glass Portrait

To run this, you need:

- A Looking Glass holographic display device connected to a Windows computer via hdmi
- The Holoplay driver installed
- A beefy ass graphics card
- A game rom (.a26/.nes/.vb file) which needs to be put in the /atari2600/vb/ness dir, depending on the system it's for.

If you don't have a holographic device the screen will look like blurry garbage.  If you want a build that works in 2D for some reason, I guess I could do one though...

Supported games (doesn't mean they are perfect but... work 'enough'):

  Pitfall! (Atari 2600)
  Super Mario Bros (NES)
  Castlevania (NES)

 -= Virtual Boy =-

  Because the 3D data is not hand coded (it comes from the game itself) they all do 'something', but some work better than others.

  Games that look half decent:

  Jack Bros
  Panic Bomer
  Teleroboxer (flickers between rounds)
  Vertical Force
  Wario Land
  

The actual filenames don't matter, it detects supported games by checksum.  Note: There are two versions of Castlevania NES roms out there, if it's not detected and playing in 3D you might have the wrong one.

It will run unsupported games in a normal 2D mode.

I've only tested with the Looking Glass Portrait and the OG 8.9.

** SETUP **

When it starts, it will show a menu with the hotkeys:

Arrow keys: Move
Ctrl, Space, Enter: A and B and Start buttons (gamepad supported too)
Return: Reset game
Num 0 through 5:  Set frameskip (higher makes the game run faster by not showing every frame)
A: Adjust audio to match game speed (experimental but can help with audio problems)
-/+:  Zoom in/out.  Hoping this will help with other Looking Glass sizes.
S: Save state
L: Load state
> and <:  Cycle through detected games (any roms you've placed in the atari2600 and nes directories)

About speed: I can get 60 fps with NES games but only around 45 on 2600 games.  (5ghz with Nvidia 3090)  If a game is too slow, press 1 or 2 for frame skipping modes.  If audio is weird, press A to cause
audio to sync with the recent framerate.  (gets rid of pops and scratches usually, but pitch/speed will be wrong)

If you have problems, check the log.txt file for clues.  (created in the root dir where HoloVCS.exe is)

Q. Does it support other games besides these?

A. It will play unsupported games without 3d plane effects, so not really

Q. I noticed you’re using emulators via a libretro dll interface, does this mean I can pop in more emulator types?

A. Yes!  Will, no.  I mean, each requires customizations to work properly and do 3d stuff.

Q. The snake in Pitfall! is mostly invisible!

A. This is a known bug, sorry. I mean, it’s a ghost snake now

Q. Why do some levels look weird or broken?

A. Sorry, I only made it so the first levels works, didn't worry about later stuff.  It is possible to detect current level/environments via PPU memory and adjust rendering to match the situation though.

Q. Why is it called HoloVCS?

A. It originally only supported Atari VCS emulation.  Too lazy to change it

If you want to help add support for more games and cores, please check out the project's github.

-Seth A. Robinson (seth@rtsoft.com)

www.rtsoft.com
www.codedojo.com