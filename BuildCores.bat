@echo off
:Builds the three GPL emulator core dlls from cores\ and copies them into Binaries\Win64.
:They are separate projects on purpose - do NOT statically link them into the game, GPL and the
:Unreal license don't mix in one binary.
:Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).
:Other scripts in this project call it with HOLO_BAT_NESTED set so it stays quiet for them.

setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if "%MSBUILD%"=="" (
	echo Couldn't find MSBuild, is Visual Studio installed?
	goto :fail
)
echo Using %MSBUILD%

set OUTDIR=%~dp0cores\_built\
set "BINARYDIR=%~dp0Binaries\Win64"

if not exist "%BINARYDIR%" mkdir "%BINARYDIR%"
if errorlevel 1 (
	echo Couldn't create %BINARYDIR%
	goto :fail
)

"%MSBUILD%" "%~dp0cores\fceumm\msvc\fceumm_libretro.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:OutDir=%OUTDIR% /m /v:m /nologo
if errorlevel 1 (
	echo fceumm build FAILED
	goto :fail
)

"%MSBUILD%" "%~dp0cores\beetle-vb\visualstudio\beetle-vb-libretro.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:OutDir=%OUTDIR% /m /v:m /nologo
if errorlevel 1 (
	echo beetle-vb build FAILED
	goto :fail
)

"%MSBUILD%" "%~dp0cores\stella\src\libretro\Stella.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:OutDir=%OUTDIR% /m /v:m /nologo
if errorlevel 1 (
	echo stella build FAILED
	goto :fail
)

copy /Y "%OUTDIR%fceumm_libretro.dll" "%BINARYDIR%\fceumm_libretro.dll"
if errorlevel 1 (
	echo Couldn't copy fceumm_libretro.dll
	goto :fail
)
copy /Y "%OUTDIR%beetle-vb-libretro.dll" "%BINARYDIR%\beetle-vb-libretro.dll"
if errorlevel 1 (
	echo Couldn't copy beetle-vb-libretro.dll
	goto :fail
)
copy /Y "%OUTDIR%stella_libretro.dll" "%BINARYDIR%\stella_libretro.dll"
if errorlevel 1 (
	echo Couldn't copy stella_libretro.dll
	goto :fail
)

echo All three cores built and copied to Binaries\Win64
call "%~dp0PauseHelper.bat"
endlocal
exit /b 0

:fail
echo.
echo *** Core build FAILED ***
call "%~dp0PauseHelper.bat"
endlocal
exit /b 1
