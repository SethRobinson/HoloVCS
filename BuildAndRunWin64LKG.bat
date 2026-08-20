@echo off
:Builds the Looking Glass hardware version (LookingGlass plugin enabled) as a Shipping release,
:stages it to dist\win64_lkg_test, copies the core dlls and roms in, then runs it.  For local
:testing - no signing, no zip, roms are NOT scrubbed, so never distribute this folder.
:The packaged exe boots the hardware map (NewMap) automatically - the game module redirects
:GameDefaultMap when the LookingGlass plugin is loaded.
:Pass "nolaunch" as the first argument to skip launching at the end.

setlocal
SET UE_DIR=F:\UnrealEngine\UE_5.8
:The hardware build uses its own game target (HoloVCSLKG) so its monolithic exe cannot collide
:with the flat one - see Source\HoloVCSLKG.Target.cs.  The top-level staged exe is named after it.
SET APP_NAME=HoloVCSLKG
:UAT names the staged project folder after the uproject, not the target
SET STAGED_PROJ=HoloVCS
SET UPROJECT=%~dp0HoloVCS.uproject
SET STAGE_DIR=%~dp0dist\win64_lkg_test

:Make sure the emulator core dlls exist, they get staged along with everything else
if not exist "%~dp0Binaries\Win64\fceumm_libretro.dll" call "%~dp0BuildCores.bat"
if not exist "%~dp0Binaries\Win64\fceumm_libretro.dll" echo Core dlls missing and BuildCores failed && exit /b 1

:Note - not wiping the old staged build on purpose, incremental is much faster for a test loop.
:Delete dist\win64_lkg_test yourself if you want a from-scratch stage.

:Preserve save states (the S/L hotkeys write <rom>.sav0 next to the top-level exe) across restages -
:UAT sometimes cleans the whole stage dir.  Real releases (PackageWin64Release.bat) scrub *.sav0.
SET SAV_KEEP=%~dp0dist\savstate_keep_lkg
if exist "%STAGE_DIR%\Windows\*.sav0" (
	mkdir "%SAV_KEEP%" 2>nul
	copy /Y "%STAGE_DIR%\Windows\*.sav0" "%SAV_KEEP%" >nul
)

:-prereqs bundles vc_redist so the bootstrap exe can offer to install the MSVC runtime instead of
:showing a dead-end "component required" error on machines without it; -applocaldirectory stages the
:CRT dlls next to the Shipping exe so the game (and the libretro cores) run even without it installed.
call "%UE_DIR%\Engine\Build\BatchFiles\RunUAT" -ScriptsForProject="%UPROJECT%" BuildCookRun -project="%UPROJECT%" -target=HoloVCSLKG -noP4 -clientconfig=Shipping -nocompileeditor -installed -unrealexe="%UE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -utf8output -platform=Win64 -targetplatform=Win64 -build -cook -map=/Game/Maps/NewMap -unversionedcookedcontent -pak -prereqs -applocaldirectory="%UE_DIR%\Engine\Binaries\ThirdParty\AppLocalDependencies" -SkipCookingEditorContent -compressed -stage -package -stagingdirectory="%STAGE_DIR%"
if errorlevel 1 echo BuildCookRun FAILED && exit /b 1

echo Copying core dlls and roms into the staged build...
copy /Y "%~dp0Binaries\Win64\stella_libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
copy /Y "%~dp0Binaries\Win64\fceumm_libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
copy /Y "%~dp0Binaries\Win64\beetle-vb-libretro.dll" "%STAGE_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64"
xcopy "%~dp0atari2600" "%STAGE_DIR%\Windows\atari2600\" /E /Y /Q
xcopy "%~dp0nes" "%STAGE_DIR%\Windows\nes\" /E /Y /Q
xcopy "%~dp0vb" "%STAGE_DIR%\Windows\vb\" /E /Y /Q

:Put preserved save states back
if exist "%SAV_KEEP%\*.sav0" (
	copy /Y "%SAV_KEEP%\*.sav0" "%STAGE_DIR%\Windows" >nul
	rmdir /S /Q "%SAV_KEEP%" 2>nul
	echo Restored save states.
)

if /I "%~1"=="nolaunch" goto :done

echo Launching...
start "" "%STAGE_DIR%\Windows\%APP_NAME%.exe"

:done
endlocal
