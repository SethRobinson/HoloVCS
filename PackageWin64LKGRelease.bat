@echo off
setlocal
pushd "%~dp0"

rem Builds the distributable Looking Glass package. This script never uploads anything.
rem The public download keeps the historical HoloVCS_Win64.zip filename.
rem Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).

call "%~dp0app_info_setup.bat"
if errorlevel 1 goto :fail

set "UE_DIR=F:\UnrealEngine\UE_5.8"
set "PRODUCT_NAME=HoloVCS"
set "TARGET_NAME=HoloVCSLKG"
set "STAGED_PROJ=HoloVCS"
set "UPROJECT=%~dp0HoloVCS.uproject"
set "STAGE_DIR=%~dp0dist\win64_lkg_release"
set "WINDOWS_DIR=%STAGE_DIR%\Windows"
set "ZIP_NAME=HoloVCS_Win64.zip"
set "ZIP_PATH=%~dp0%ZIP_NAME%"
set "SEVEN_ZIP=%PROTON_DIR%\shared\win\utils\7za.exe"
set "SIGN_SCRIPT=%RT_PROJECTS%\Signing\sign.bat"
set "SIGNTOOL=C:\PROGRA~2\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set "EDITOR_MAKEFILE=%~dp0Intermediate\Build\Win64\x64\HoloVCSEditor\Development\Makefile.bin"
set "PAK_LIST=%STAGE_DIR%\pak_contents.txt"

if not exist "%UE_DIR%\Engine\Build\BatchFiles\Build.bat" (
	echo Unreal Engine 5.8 was not found at %UE_DIR%
	goto :fail
)
if not exist "%SEVEN_ZIP%" (
	echo 7-Zip was not found at %SEVEN_ZIP%
	goto :fail
)
if not exist "%SIGN_SCRIPT%" (
	echo Signing script was not found at %SIGN_SCRIPT%
	goto :fail
)
if not exist "%SIGNTOOL%" (
	echo Signature verification tool was not found at %SIGNTOOL%
	goto :fail
)

echo Rebuilding all emulator cores from the checked-in source...
set "HOLO_BAT_NESTED=1"
call "%~dp0BuildCores.bat"
rem Clearing HOLO_BAT_NESTED clobbers ERRORLEVEL, so stash the core build result first.
set "CORES_ERROR=%ERRORLEVEL%"
set "HOLO_BAT_NESTED="
if not "%CORES_ERROR%"=="0" goto :fail

rem The flat and LKG uprojects share one HoloVCSEditor makefile. Remove only that makefile so
rem plugin changes cannot be skipped after a flat editor build.
if exist "%EDITOR_MAKEFILE%" del /Q "%EDITOR_MAKEFILE%"
if exist "%EDITOR_MAKEFILE%" (
	echo Could not remove stale editor makefile
	goto :fail
)

echo Building the Looking Glass editor modules used by the cook...
call "%UE_DIR%\Engine\Build\BatchFiles\Build.bat" HoloVCSEditor Win64 Development -project="%UPROJECT%" -waitmutex
if errorlevel 1 goto :fail

if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
if exist "%STAGE_DIR%" (
	echo Could not clean %STAGE_DIR%
	goto :fail
)
if exist "%ZIP_PATH%" del /Q "%ZIP_PATH%"
if exist "%ZIP_PATH%" (
	echo Could not replace %ZIP_PATH%
	goto :fail
)

echo Cooking and staging the Looking Glass Shipping build...
rem -prereqs bundles vc_redist so the bootstrap exe can offer to install the MSVC runtime instead of
rem showing a dead-end "component required" error on machines without it; -applocaldirectory stages
rem the CRT dlls next to the Shipping exe so the game (and libretro cores) run even without it installed.
call "%UE_DIR%\Engine\Build\BatchFiles\RunUAT.bat" -ScriptsForProject="%UPROJECT%" BuildCookRun -project="%UPROJECT%" -target=%TARGET_NAME% -noP4 -clientconfig=Shipping -nocompileeditor -installed -unrealexe="%UE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -utf8output -platform=Win64 -targetplatform=Win64 -build -cook -map=/Game/Maps/NewMap -unversionedcookedcontent -pak -distribution -prereqs -applocaldirectory="%UE_DIR%\Engine\Binaries\ThirdParty\AppLocalDependencies" -SkipCookingEditorContent -compressed -stage -package -stagingdirectory="%STAGE_DIR%"
if errorlevel 1 goto :fail

if not exist "%WINDOWS_DIR%\%TARGET_NAME%.exe" (
	echo Staged bootstrap executable is missing
	goto :fail
)
if not exist "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\%TARGET_NAME%-Win64-Shipping.exe" (
	echo Staged Shipping executable is missing
	goto :fail
)
if not exist "%WINDOWS_DIR%\Engine\Extras\Redist\en-us\vc_redist.x64.exe" (
	echo Visual C++ runtime installer is missing
	goto :fail
)
if not exist "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\msvcp140.dll" (
	echo App-local Visual C++ runtime dlls are missing
	goto :fail
)

echo Adding emulator cores, player documentation, and licenses...
copy /Y "Binaries\Win64\stella_libretro.dll" "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\" >nul
if errorlevel 1 goto :fail
copy /Y "Binaries\Win64\fceumm_libretro.dll" "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\" >nul
if errorlevel 1 goto :fail
copy /Y "Binaries\Win64\beetle-vb-libretro.dll" "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\" >nul
if errorlevel 1 goto :fail
rem The 3DS core is built by hand from the separate Azahar fork (see AGENTS.md), not by
rem BuildCores.bat. It is REQUIRED: v1.5+ advertises 3DS support, so a zip without it is broken.
if not exist "Binaries\Win64\azahar_libretro.dll" (
	echo azahar_libretro.dll is missing from Binaries\Win64 - build the Azahar fork and copy it there first
	goto :fail
)
copy /Y "Binaries\Win64\azahar_libretro.dll" "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\" >nul
if errorlevel 1 goto :fail
copy /Y "readme.txt" "%WINDOWS_DIR%\" >nul
if errorlevel 1 goto :fail

mkdir "%WINDOWS_DIR%\atari2600" 2>nul
mkdir "%WINDOWS_DIR%\nes" 2>nul
mkdir "%WINDOWS_DIR%\vb" 2>nul
mkdir "%WINDOWS_DIR%\3ds" 2>nul
copy /Y "atari2600\*.txt" "%WINDOWS_DIR%\atari2600\" >nul
copy /Y "nes\*.txt" "%WINDOWS_DIR%\nes\" >nul
copy /Y "vb\*.txt" "%WINDOWS_DIR%\vb\" >nul
copy /Y "3ds\*.txt" "%WINDOWS_DIR%\3ds\" >nul

mkdir "%WINDOWS_DIR%\licenses" 2>nul
copy /Y "LICENSE.md" "%WINDOWS_DIR%\licenses\LICENSE-HoloVCS.md" >nul
if errorlevel 1 goto :fail
copy /Y "Plugins\LookingGlass\License.md" "%WINDOWS_DIR%\licenses\LICENSE-LookingGlass-MIT.md" >nul
if errorlevel 1 goto :fail
copy /Y "cores\fceumm\Copying" "%WINDOWS_DIR%\licenses\LICENSE-FCEUmm-GPL-2.0.txt" >nul
if errorlevel 1 goto :fail
copy /Y "cores\stella\License.txt" "%WINDOWS_DIR%\licenses\LICENSE-Stella-GPL-2.0.txt" >nul
if errorlevel 1 goto :fail
copy /Y "cores\beetle-vb\COPYING" "%WINDOWS_DIR%\licenses\LICENSE-BeetleVB-GPL-2.0.txt" >nul
if errorlevel 1 goto :fail
rem The Azahar source is not vendored here (SethRobinson/azahar branch holo); take the license
rem from the fork checkout the distributed DLL was built from.
copy /Y "F:\Unreal\azahar\license.txt" "%WINDOWS_DIR%\licenses\LICENSE-Azahar-GPL-2.0.txt" >nul
if errorlevel 1 goto :fail

del /S /Q "%WINDOWS_DIR%\*.pdb" >nul 2>&1
del /Q "%WINDOWS_DIR%\Manifest_UFSFiles_Win64.txt" >nul 2>&1
del /Q "%WINDOWS_DIR%\Manifest_NonUFSFiles_Win64.txt" >nul 2>&1
del /Q "%WINDOWS_DIR%\Manifest_DebugFiles_Win64.txt" >nul 2>&1
del /Q "%WINDOWS_DIR%\log.txt" >nul 2>&1

set "FORBIDDEN_FILE="
for /R "%WINDOWS_DIR%" %%F in (*.a26 *.nes *.vb *.vboy *.3ds *.cci *.sav0) do set "FORBIDDEN_FILE=%%F"
if defined FORBIDDEN_FILE (
	echo Forbidden ROM or save-state file found: %FORBIDDEN_FILE%
	goto :fail
)

echo Verifying that per-machine config was excluded from the cooked container...
"%UE_DIR%\Engine\Binaries\Win64\UnrealPak.exe" "%WINDOWS_DIR%\%STAGED_PROJ%\Content\Paks\%STAGED_PROJ%-Windows.pak" -List > "%PAK_LIST%"
if errorlevel 1 goto :fail
findstr /I /C:"UserEngine.ini" "%PAK_LIST%" >nul
if not errorlevel 1 (
	echo UserEngine.ini was found in the cooked package
	goto :fail
)
del /Q "%PAK_LIST%"

rem UE 5.8 inherits an invalid certificate-table entry from BootstrapPackagedGame.exe. It points
rem beyond EOF and makes Authenticode return 0x800700C1. Clear only that invalid entry before signing.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ClearInvalidPeCertificateTable.ps1" -Path "%WINDOWS_DIR%\%TARGET_NAME%.exe"
if errorlevel 1 goto :fail

echo Signing release executables and emulator cores...
call :sign "%WINDOWS_DIR%\%TARGET_NAME%.exe"
if errorlevel 1 goto :fail
call :sign "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\%TARGET_NAME%-Win64-Shipping.exe"
if errorlevel 1 goto :fail
call :sign "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\stella_libretro.dll"
if errorlevel 1 goto :fail
call :sign "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\fceumm_libretro.dll"
if errorlevel 1 goto :fail
call :sign "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\beetle-vb-libretro.dll"
if errorlevel 1 goto :fail
call :sign "%WINDOWS_DIR%\%STAGED_PROJ%\Binaries\Win64\azahar_libretro.dll"
if errorlevel 1 goto :fail

timeout /T 4 /NOBREAK >nul

echo Creating %ZIP_NAME%...
pushd "%STAGE_DIR%"
ren "Windows" "%PRODUCT_NAME%"
if errorlevel 1 goto :zip_fail
"%SEVEN_ZIP%" a -r -tzip "%ZIP_PATH%" "%PRODUCT_NAME%"
set "ZIP_ERROR=%ERRORLEVEL%"
ren "%PRODUCT_NAME%" "Windows"
set "RESTORE_ERROR=%ERRORLEVEL%"
popd
if not "%ZIP_ERROR%"=="0" goto :fail
if not "%RESTORE_ERROR%"=="0" goto :fail
if not exist "%ZIP_PATH%" (
	echo Zip was not created
	goto :fail
)

echo Release package ready: %ZIP_PATH%
call "%~dp0PauseHelper.bat"
popd
endlocal
exit /b 0

:sign
rem sign.bat pauses after every run unless the NO_PAUSE env var is non-empty (its arguments have no
rem say in that, despite what an older comment here claimed - the "pause" looked like a hang because
rem the output is suppressed). setlocal keeps the variable from leaking out of this script.
rem Suppress its output because the third-party helper echoes its hardware-token PIN.
set "NO_PAUSE=1"
call "%SIGN_SCRIPT%" %~1 "%PRODUCT_NAME%" "https://www.rtsoft.com" >nul 2>&1
if errorlevel 1 (
	echo Signing failed for %~1
	exit /b 1
)
"%SIGNTOOL%" verify /pa %~1 >nul 2>&1
if errorlevel 1 (
	echo Signature validation failed for %~1
	exit /b 1
)
echo Signed %~nx1
exit /b 0

:zip_fail
popd
goto :fail

:fail
echo.
echo *** Looking Glass release packaging FAILED ***
call "%~dp0PauseHelper.bat"
popd
endlocal
exit /b 1
