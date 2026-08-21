rem Shared variable setup for the other .bat files in this project.  It is always called by another
rem script, so it never pauses on its own - the caller checks the errorlevel, reports the failure
rem and pauses.
if not exist "%~dp0..\base_setup.bat" (
	echo base_setup.bat was not found one folder above the project
	exit /b 1
)
call "%~dp0..\base_setup.bat"
SET APP_NAME=HoloVCS
SET APP_DIR=HoloVCS_UE56
SET APP_PATH=%PROJECT_DIR%\%APP_DIR%
SET UE_DIR=F:\UnrealEngine\UE_5.8
