@echo off
:Builds the flat (non Looking Glass) version as a Shipping release, stages it to dist\win64_test,
:copies the core dlls and roms in, then runs it.  For local testing - no signing, no zip, roms
:are NOT scrubbed, so never distribute this folder.  Use PackageWin64Release.bat for real releases.
:Pass "nolaunch" as the first argument to skip launching at the end.
:Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).

setlocal
SET UE_DIR=F:\UnrealEngine\UE_5.8
SET APP_NAME=HoloVCS
:UAT names the staged project folder after the uproject, not the target
SET STAGED_PROJ=HoloVCS_Flat
SET UPROJECT=%~dp0HoloVCS_Flat.uproject
SET STAGE_DIR=%~dp0dist\win64_test

:Make sure the emulator core dlls exist, they get staged along with everything else
if not exist "%~dp0Binaries\Win64\fceumm_libretro.dll" (
	set "HOLO_BAT_NESTED=1"
	call "%~dp0BuildCores.bat"
	set "HOLO_BAT_NESTED="
)
if not exist "%~dp0Binaries\Win64\fceumm_libretro.dll" (
	echo Core dlls missing and BuildCores failed
	goto :fail
)

:Note - not wiping the old staged build on purpose, incremental is much faster for a test loop.
:Delete dist\win64_test yourself if you want a from-scratch stage.

:Preserve save states (the S/L hotkeys write saves\<system>\<rom>.sav0; old builds wrote <rom>.sav0
:next to the top-level exe and the game migrates those on load) across restages - UAT sometimes
:cleans the whole stage dir.  PackageWin64Release.bat still scrubs save states from real releases.
SET SAV_KEEP=%~dp0dist\savstate_keep_flat
if exist "%STAGE_DIR%\Windows\saves" (
	mkdir "%SAV_KEEP%" 2>nul
	xcopy "%STAGE_DIR%\Windows\saves" "%SAV_KEEP%\saves\" /E /Y /Q >nul
)
if exist "%STAGE_DIR%\Windows\*.sav0" (
	mkdir "%SAV_KEEP%" 2>nul
	copy /Y "%STAGE_DIR%\Windows\*.sav0" "%SAV_KEEP%" >nul
)

:-prereqs bundles vc_redist so the bootstrap exe can offer to install the MSVC runtime instead of
:showing a dead-end "component required" error on machines without it; -applocaldirectory stages the
:CRT dlls next to the Shipping exe so the game (and the libretro cores) run even without it installed.
call "%UE_DIR%\Engine\Build\BatchFiles\RunUAT" -ScriptsForProject="%UPROJECT%" BuildCookRun -project="%UPROJECT%" -noP4 -clientconfig=Shipping -nocompileeditor -installed -unrealexe="%UE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -utf8output -platform=Win64 -targetplatform=Win64 -build -cook -map=NewMap_Flat -unversionedcookedcontent -pak -prereqs -applocaldirectory="%UE_DIR%\Engine\Binaries\ThirdParty\AppLocalDependencies" -SkipCookingEditorContent -compressed -stage -package -stagingdirectory="%STAGE_DIR%"
if errorlevel 1 (
	echo BuildCookRun FAILED
	goto :fail
)

echo Copying core dlls and roms into the staged build...
copy /Y "%~dp0Binaries\Win64\stella_libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
copy /Y "%~dp0Binaries\Win64\fceumm_libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
copy /Y "%~dp0Binaries\Win64\beetle-vb-libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
xcopy "%~dp0atari2600" "%STAGE_DIR%\Windows\atari2600\" /E /Y /Q
xcopy "%~dp0nes" "%STAGE_DIR%\Windows\nes\" /E /Y /Q
xcopy "%~dp0vb" "%STAGE_DIR%\Windows\vb\" /E /Y /Q

:Put preserved save states back
if exist "%SAV_KEEP%" (
	if exist "%SAV_KEEP%\saves" xcopy "%SAV_KEEP%\saves" "%STAGE_DIR%\Windows\saves\" /E /Y /Q >nul
	if exist "%SAV_KEEP%\*.sav0" copy /Y "%SAV_KEEP%\*.sav0" "%STAGE_DIR%\Windows" >nul
	rmdir /S /Q "%SAV_KEEP%" 2>nul
	echo Restored save states.
)

if /I "%~1"=="nolaunch" goto :done

echo Launching...
start "" "%STAGE_DIR%\Windows\%APP_NAME%.exe" -windowed -resx=1280 -resy=720

:done
echo.
echo Flat test build staged in %STAGE_DIR%\Windows
call "%~dp0PauseHelper.bat"
endlocal
exit /b 0

:fail
echo.
echo *** Flat test build FAILED ***
call "%~dp0PauseHelper.bat"
endlocal
exit /b 1
