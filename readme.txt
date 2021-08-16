HoloVCS - The Stella Atari 2600 emulator running under Unreal Engine and modified to display Pitfall! in 3D on a Looking Glass Portrait

To run this, you need:

- A Looking Glass holographic display device connected to a Windows computer via hdmi
- The Holoplay driver installed
- A beefy ass graphics card
- The pitfall 2600 rom (.a26 file) which needs to be put in the /atari2600 dir.

I've only tested with the Looking Glass Portrait and the OG 8.9.

** SETUP **

When it starts, it will show a menu with the hotkeys, but here they:

Gamepad/Arrow keys: Move
Ctrl/Button A: Jump
Return: Reset game
Num 0 through 5:  Set frameskip (higher makes the game run faster)
A: Adjust audio to match game speed (experimental but can help with audio problems)
-/+:  Zoom in/out.  Hoping this will help with other Looking Glass sizes.
S: Save state
L: Load state

Speed is a bit too slow even on my 3090, so good luck.

If you have problems, check the log.txt file.  (created in the root dir where HoloVCS.exe is)

-Seth A. Robinson (seth@rtsoft.com)

Q. Does it support other games besides Pitfall!?

A. Well, yes and no, yes it should emulate any VCS game, but the 3D layer processing is designed for Pitfall! so I’m sure it would be… quite the experience

Q. I noticed you’re using Stella’s libretro interface, does this mean I can pop in a NES emulator or whatever by replacing the dll?

A. In theory yes, but because pixel sizes/input/etc are kind of hard coded for the VCS I’m sure it’s going to hilariously explode.

Q. The snake is mostly invisible!

A. This is a known bug, sorry. I mean, it’s a ghost snake now

If you want to help add support for more games and cores, please check out the project's github.

www.rtsoft.com
www.codedojo.com