call app_info_setup.bat
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
cd ..
del %FNAME%


%PROTON_DIR%\shared\win\utils\7za.exe a -r -tzip %FNAME% %APP_NAME%\* Signing\* base_setup.bat -x!*.zip -x!*.svn -x!*.ncb -x!%APP_NAME%\.vs -x!Browse.VC.db -x!*.bsc -x!*.pdb -x!*.sbr -x!*.ilk -x!*.idb -x!.o -x!*.obj -x!*.DS_Store -x!._* -x!%APP_NAME%\dist -x!%APP_NAME%\Binaries -x!%APP_NAME%\bin -x!%APP_NAME%\Build -x!%APP_NAME%\Intermediate -x!%APP_NAME%\Saved
cd %FNAME%