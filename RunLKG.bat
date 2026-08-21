@echo off
:Launches the most recently staged Looking Glass test build (BuildAndRunWin64LKG.bat stages it).
:Lives in the project root so it's one keystroke away in any terminal: .\RunLKG.bat
:This one has nothing to report when it works, so it only pauses if the build is missing.
:As everywhere else, NOPAUSE suppresses that too (see PauseHelper.bat).
setlocal
set "HOLO_LKG_EXE=%~dp0dist\win64_lkg_test\Windows\HoloVCSLKG.exe"
if not exist "%HOLO_LKG_EXE%" (
	echo No staged Looking Glass build found at %HOLO_LKG_EXE%
	echo Run BuildAndRunWin64LKG.bat first.
	call "%~dp0PauseHelper.bat"
	exit /b 1
)
start "" "%HOLO_LKG_EXE%"
