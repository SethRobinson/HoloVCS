call app_info_setup.bat

SET ZIP_FILE_NAME=HoloVCS_Win64
SET APP_BUILD_DIR=win64_release
del %ZIP_FILE_NAME%.zip
mkdir dist
cd dist
mkdir %APP_BUILD_DIR%
cd %APP_BUILD_DIR%
echo Deleting old dist build...
rmdir WindowsNoEditor /S /Q
cd ..\..
:goto skip

call %UE4_DIR%\Engine\Build\BatchFiles\RunUAT BuildCookRun -nocompile -nocompileeditor -installed -nop4 -project=%APP_PATH%\%APP_NAME%.uproject -cook -stage -archive -archivedirectory=%APP_PATH%/dist/%APP_BUILD_DIR% -package -clientconfig=Shipping -ue 4exe=UE4Editor-Cmd.exe -compressed -pak -distribution -nodebuginfo -targetplatform=Win64 -utf8output
#unused:  -CrashReporter 
#echo on

:skip
echo Copy some other stuff we need
copy Binaries\Win64\stella_libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64
copy readme.txt %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor
xcopy atari2600 %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\atari2600\ /E /F /Y
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\Manifest_NonUFSFiles_Win64.txt
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\log.txt
:Better remove those test roms, don't want to commit a crime here!
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\atari2600\*.a26



echo Signing .exe's...

call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%.exe
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\%APP_NAME%-Win64-Shipping.exe "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\HoloPlayCore.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\stella_libretro.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\turbojpeg.dll "%APP_NAME%"
echo "Waiting 4 seconds because NSIS does something and ruins the signing if I don't"
timeout 4


:Rename and zip it
cd dist\%APP_BUILD_DIR%
rename WindowsNoEditor %APP_NAME%
%PROTON_DIR%\shared\win\utils\7za.exe a -r -tzip %ZIP_FILE_NAME% %APP_NAME%
:rename it back so the delete will work.  We could delete now, but if %ZIP_FILE_NAME% was blank/wrong it could be bad, so this way is safer
rename %APP_NAME% WindowsNoEditor
cd ..\..
echo Move the zip somewhere sensible
move dist\%APP_BUILD_DIR%\%ZIP_FILE_NAME%.zip ./
echo All done!
pause
