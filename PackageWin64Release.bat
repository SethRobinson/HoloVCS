@echo off
:Packages the flat (non Looking Glass) release: cook, stage, sign, zip.  This script never uploads.
:Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).
setlocal
rem every path below is relative to the project dir, so make sure we are in it
pushd "%~dp0"

call "%~dp0app_info_setup.bat"
if errorlevel 1 goto :fail

SET ZIP_FILE_NAME=HoloVCS_Win64
SET APP_BUILD_DIR=win64_release
SET UPROJECT=%APP_PATH%\HoloVCS_Flat.uproject
:UAT names the staged project folder after the uproject, not the target
SET STAGED_PROJ=HoloVCS_Flat
del %ZIP_FILE_NAME%.zip
del Binaries\Win64\%APP_NAME%-Win64-Shipping.exe
mkdir dist
cd dist
echo Deleting old dist build...
rmdir %APP_BUILD_DIR% /S /Q
mkdir %APP_BUILD_DIR%
cd ..

:Note - this packages the flat (non Looking Glass) version.  UE5 stages into a "Windows" dir, not "WindowsNoEditor" like UE4 did.
:-prereqs bundles vc_redist so the bootstrap exe can offer to install the MSVC runtime instead of
:showing a dead-end "component required" error on machines without it; -applocaldirectory stages the
:CRT dlls next to the Shipping exe so the game (and the libretro cores) run even without it installed.
call %UE_DIR%\Engine\Build\BatchFiles\RunUAT -ScriptsForProject=%UPROJECT% BuildCookRun -project=%UPROJECT% -noP4 -clientconfig=Shipping -nocompileeditor -installed -unrealexe=%UE_DIR%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe -utf8output -platform=Win64 -targetplatform=Win64 -build -cook -map=NewMap_Flat -unversionedcookedcontent -pak -distribution -prereqs -applocaldirectory="%UE_DIR%\Engine\Binaries\ThirdParty\AppLocalDependencies" -SkipCookingEditorContent -compressed -stage -package -stagingdirectory=%APP_PATH%/dist/%APP_BUILD_DIR%/
if errorlevel 1 (
	echo ERROR: BuildCookRun failed
	goto :fail
)

rem Fail loudly if the runtime shipping pieces did not stage - their absence is exactly the bad
rem first-run experience this guards against.
if not exist "%APP_PATH%\dist\%APP_BUILD_DIR%\Windows\Engine\Extras\Redist\en-us\vc_redist.x64.exe" (
	echo ERROR: vc_redist.x64.exe missing from stage
	goto :fail
)
if not exist "%APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64\msvcp140.dll" (
	echo ERROR: app-local CRT dlls missing from stage
	goto :fail
)

echo deleting pdb files to make things smaller
del /s "dist\%APP_BUILD_DIR%\*.pdb"

echo Copy some other stuff we need
copy Binaries\Win64\stella_libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64
copy Binaries\Win64\fceumm_libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64
copy Binaries\Win64\beetle-vb-libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64
copy readme.txt %APP_PATH%\dist\%APP_BUILD_DIR%\Windows
xcopy atari2600 %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\atari2600\ /E /F /Y
xcopy nes %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\nes\ /E /F /Y
xcopy vb %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\vb\ /E /F /Y
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\Manifest_NonUFSFiles_Win64.txt
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\Manifest_DebugFiles_Win64.txt
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\log.txt
:Better remove those test roms, don't want to commit a crime here!
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\atari2600\*.a26
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\nes\*.nes
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\vb\*.vb
del %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\*.sav0
rmdir /S /Q %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\saves 2>nul
echo Signing .exe's...
:sign.bat pauses after every run unless NO_PAUSE is set - without this each signed file needs an ENTER press
rem (that is the signing helper's own variable, unrelated to this project's NOPAUSE)
SET NO_PAUSE=1

call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%APP_NAME%.exe
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64\%APP_NAME%-Win64-Shipping.exe "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64\stella_libretro.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64\fceumm_libretro.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\%STAGED_PROJ%\Binaries\Win64\beetle-vb-libretro.dll "%APP_NAME%"

echo "Waiting 4 seconds because NSIS does something and ruins the signing if I don't"
timeout 4


:Rename and zip it
cd dist\%APP_BUILD_DIR%
rename Windows %APP_NAME%
%PROTON_DIR%\shared\win\utils\7za.exe a -r -tzip %ZIP_FILE_NAME% %APP_NAME%
set "ZIP_ERROR=%ERRORLEVEL%"
:rename it back so the delete will work.  We could delete now, but if %ZIP_FILE_NAME% was blank/wrong it could be bad, so this way is safer
rename %APP_NAME% Windows
cd ..\..
if not "%ZIP_ERROR%"=="0" (
	echo ERROR: zipping failed
	goto :fail
)
echo Move the zip somewhere sensible
move dist\%APP_BUILD_DIR%\%ZIP_FILE_NAME%.zip ./
if not exist "%APP_PATH%\%ZIP_FILE_NAME%.zip" (
	echo ERROR: %ZIP_FILE_NAME%.zip was not created
	goto :fail
)
echo Ok, now that we're done packing, let's move some roms into the dist dir so we can easily test it
copy atari2600\*.a26 %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\atari2600
copy nes\*.nes %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\nes
copy vb\*.vb %APP_PATH%\dist\%APP_BUILD_DIR%\Windows\vb

echo.
echo All done!  Release package ready: %APP_PATH%\%ZIP_FILE_NAME%.zip
call "%~dp0PauseHelper.bat"
popd
endlocal
exit /b 0

:fail
echo.
echo *** Flat release packaging FAILED ***
call "%~dp0PauseHelper.bat"
popd
endlocal
exit /b 1
