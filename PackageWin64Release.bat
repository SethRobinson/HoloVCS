call app_info_setup.bat

SET ZIP_FILE_NAME=HoloVCS_Win64
SET APP_BUILD_DIR=win64_release
del %ZIP_FILE_NAME%.zip
del Binaries\Win64\%APP_NAME%-Win64-Shipping.exe
mkdir dist
cd dist
echo Deleting old dist build...
rmdir %APP_BUILD_DIR% /S /Q
mkdir %APP_BUILD_DIR%
cd ..
:goto skip

:call %UE4_DIR%\Engine\Build\BatchFiles\RunUAT BuildCookRun -nocompile -nocompileeditor -installed -nop4 -project=%APP_PATH%\%APP_NAME%.uproject -cook -stage -archive -archivedirectory=%APP_PATH%/dist/%APP_BUILD_DIR% -package -clientconfig=Shipping -ue 4exe=UE4Editor-Cmd.exe -compressed -pak -distribution -nodebuginfo -targetplatform=Win64 -utf8output
:call %UE4_DIR%\Engine\Build\BatchFiles\RunUAT BuildCookRun -nocompileeditor -installed -nop4 -project=F:/Unreal/HoloVCS/HoloVCS.uproject -cook -stage -archive -archivedirectory=F:/Unreal/HoloVCS/Build -package -ue4exe=F:\UnrealEngine\UE_4.27\Engine\Binaries\Win64\UE4Editor-Cmd.exe -ddc=In stalledDerivedDataBackendGraph -pak -prereqs -distribution -nodebuginfo -targetplatform=Win64 -build -target=HoloVCS -clientconfig=Shipping -utf8output

call %UE4_DIR%\Engine\Build\BatchFiles\RunUAT -ScriptsForProject=%APP_PATH%\%APP_NAME%.uproject BuildCookRun -project=%APP_PATH%\%APP_NAME%.uproject -noP4 -clientconfig=Shipping -serverconfig=Shipping -nocompileeditor -installed -ue4exe=%UE4_DIR%\Engine\Binaries\Win64\UE4Editor-Cmd.exe -utf8output -platform=Win64 -targetplatform=Win64 -build -cook -map=NewMap -unversionedcookedcontent -pak -createreleaseversion= -distribution -SkipCookingEditorContent -compressed -stage -package -stagingdirectory=%APP_PATH%/dist/%APP_BUILD_DIR%/ -cmdline="NewMap -Messaging" -addcmdline="-SessionId=5587C22043B68023F88EFB82AB5235C0 -SessionOwner='seth' -SessionName='SethDist' "

echo deleting pdb files to make things smaller
del /s "dist\%APP_BUILD_DIR%\*.pdb"
#unused:  -CrashReporter 
#echo on

:skip
echo Copy some other stuff we need
copy Binaries\Win64\stella_libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64
copy Binaries\Win64\fceumm_libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64
copy Binaries\Win64\beetle-vb-libretro.dll %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64
copy readme.txt %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor
xcopy atari2600 %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\atari2600\ /E /F /Y
xcopy nes %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\nes\ /E /F /Y
xcopy vb %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\vb\ /E /F /Y
copy "Put the pitfall rom in atari2600 dir!.txt" %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\Manifest_NonUFSFiles_Win64.txt
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\Manifest_DebugFiles_Win64.txt
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\log.txt
:Better remove those test roms, don't want to commit a crime here!
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\atari2600\*.a26
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\nes\*.nes
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\vb\*.vb
del %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\*.sav0
echo Signing .exe's...

call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%.exe
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\%APP_NAME%-Win64-Shipping.exe "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\HoloPlayCore.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\stella_libretro.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\turbojpeg.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\fceumm_libretro.dll "%APP_NAME%"
call %RT_PROJECTS%\Signing\sign.bat %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\%APP_NAME%\Binaries\Win64\beetle-vb-libretro.dll "%APP_NAME%"

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
echo Ok, now that we're done packing, let's move some roms into the dist dir so we can easily test it
copy atari2600\*.a26 %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\atari2600
copy nes\*.nes %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\nes
copy vb\*.vb %APP_PATH%\dist\%APP_BUILD_DIR%\WindowsNoEditor\vb

echo All done!
pause
