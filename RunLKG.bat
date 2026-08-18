@echo off
:Launches the most recently staged Looking Glass test build (BuildAndRunWin64LKG.bat stages it).
:Lives in the project root so it's one keystroke away in any terminal: .\RunLKG.bat
start "" "%~dp0dist\win64_lkg_test\Windows\HoloVCSLKG.exe"
