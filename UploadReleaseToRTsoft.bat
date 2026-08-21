@echo off
rem Uploads the finished release zip to rtsoft.com.  Parms to the helper are the file itself and
rem the rtsoft subdir to put it in.
rem Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).
pushd "%~dp0"
call "%~dp0app_info_setup.bat"
if errorlevel 1 goto :fail

if not exist "%~dp0HoloVCS_Win64.zip" (
	echo HoloVCS_Win64.zip not found - build a release package first
	goto :fail
)

call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat HoloVCS_Win64.zip files
if errorlevel 1 goto :fail

echo.
echo Upload complete.
call "%~dp0PauseHelper.bat"
popd
exit /b 0

:fail
echo.
echo *** Upload FAILED ***
call "%~dp0PauseHelper.bat"
popd
exit /b 1
