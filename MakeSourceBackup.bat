@echo off
rem Zips the source tree (plus the Signing folder) into a dated archive one level above the project.
rem Pauses when it finishes or fails - set the NOPAUSE env var to skip that (see PauseHelper.bat).
pushd "%~dp0"
call "%~dp0app_info_setup.bat"
if errorlevel 1 goto :fail

REM ** Make sure american code page is used, otherwise the %DATE environmental var might be wrong

    SETLOCAL ENABLEEXTENSIONS
    if "%date%A" LSS "A" (set toks=1-3) else (set toks=2-4)
    for /f "tokens=2-4 delims=(-)" %%a in ('echo:^|date') do (
      for /f "tokens=%toks% delims=.-/ " %%i in ('date/t') do (
        set '%%a'=%%i
        set '%%b'=%%j
        set '%%c'=%%k))
    if %'yy'% LSS 100 set 'yy'=20%'yy'%
    set Today=%'yy'%-%'mm'%-%'dd'% 
    ENDLOCAL & SET v_year=%'yy'%& SET v_month=%'mm'%& SET v_day=%'dd'%

   ECHO Today is Year: [%V_Year%] Month: [%V_Month%] Day: [%V_Day%]

set FNAME=%APP_NAME%_Source_%V_Day%_%V_Month%_%V_Year%.zip
set "SEVEN_ZIP=%PROTON_DIR%\shared\win\utils\7za.exe"
if not exist "%SEVEN_ZIP%" (
	echo 7-Zip was not found at %SEVEN_ZIP%
	goto :fail
)

rem pushd/popd instead of cd so we always land back in the project dir, even on failure
pushd ..
if exist "%FNAME%" del "%FNAME%"

"%SEVEN_ZIP%" a -r -tzip %FNAME% %APP_NAME%\* Signing\* base_setup.bat -x!*.zip -x!*.svn -x!*.ncb -x!%APP_NAME%\.vs -x!Browse.VC.db -x!*.bsc -x!*.pdb -x!*.sbr -x!*.ilk -x!*.idb -x!.o -x!*.obj -x!*.DS_Store -x!._* -x!%APP_NAME%\dist -x!%APP_NAME%\Binaries -x!%APP_NAME%\bin -x!%APP_NAME%\Build -x!%APP_NAME%\Intermediate -x!%APP_NAME%\Saved
set "ZIP_ERROR=%ERRORLEVEL%"
if not "%ZIP_ERROR%"=="0" (
	popd
	echo Zipping FAILED
	goto :fail
)
if not exist "%FNAME%" (
	popd
	echo %FNAME% was not created
	goto :fail
)
popd

echo.
echo Source backup ready: %FNAME% (one folder above the project)
call "%~dp0PauseHelper.bat"
popd
exit /b 0

:fail
echo.
echo *** Source backup FAILED ***
call "%~dp0PauseHelper.bat"
popd
exit /b 1
