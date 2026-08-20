HoloVCS v1.4 - Looking Glass Windows release
================================================

HoloVCS runs Atari 2600, NES, and Virtual Boy games as layered 3D dioramas on Looking Glass holographic displays.

Requirements
------------

- Windows 10 or 11
- A connected Looking Glass display in desktop/extended-display mode
- Looking Glass Bridge 2.5.1 or newer:
  https://lookingglassfactory.com/software-downloads
- Your own legally obtained game ROMs. No ROMs are included in this package.

Put ROMs in these folders:

- Atari 2600 `.a26` files: atari2600
- NES `.nes` files: nes
- Virtual Boy `.vb` files: vb

The exact filenames do not matter. HoloVCS recognizes hand-tuned profiles by ROM checksum. Unsupported Atari 2600 and NES games use default layering. Virtual Boy games use the core's native split-layer data.

Hand-tuned profiles in v1.4
---------------------------

- Pitfall! (Atari 2600)
- Super Mario Bros. (NES)
- Castlevania, Rev A (NES)
- Tetris (NES)
- The Legend of Zelda, PRG 1 (NES)
- Wario Land and Jack Bros. have additional Virtual Boy-specific setup

Controls
--------

Press ? at any time to show the in-game control screen. The release opens this screen on startup and pauses until you dismiss it.

WASD / Arrow keys   Move / D-pad
Space               A button
Ctrl / Left click   B button
Enter               Start
Tab                 Select
, / .               Previous / next game
R                   Reset game
P                   Pause
S / L               Save / load state
[ / ]               Less / more 3D depth
= / -               Zoom in / out
0                   Toggle FPS cap
1 through 5         Frameskip
6 / 7 / 8           Smoothing / shadows / lighting

Gamepads are supported. Virtual Boy controls also use the additional gamepad buttons, shoulders, triggers, and sticks.

Troubleshooting
---------------

- Run HoloVCSLKG.exe from the top-level HoloVCS folder.
- If Windows reports a missing Microsoft Visual C++ runtime, run the bundled installer at
  Engine\Extras\Redist\en-us\vc_redist.x64.exe (HoloVCSLKG.exe normally offers this automatically).
- Keep Looking Glass Bridge installed and running so HoloVCS can read the display calibration.
- Configure the Looking Glass as an extended display at its native resolution and 100% Windows scaling.
- If no ROM is found, verify its extension and folder.
- Check log.txt next to HoloVCSLKG.exe for startup and emulator details.
- Press 1 or 2 if a game cannot maintain its native speed. Press 0 only when measuring uncapped performance.

Source code and licenses
------------------------

The corresponding HoloVCS source, the modified source for all three GPL-2.0 emulator cores, and the UE 5.8 Looking Glass plugin port are available at:

https://github.com/SethRobinson/HoloVCS

License copies are included in the licenses folder. The emulator cores are separate DLLs loaded at runtime and are not linked into the Unreal executable.

Written by Seth A. Robinson
https://www.rtsoft.com
https://www.codedojo.com
