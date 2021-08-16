call app_info_setup.bat
set FNAME=%APP_NAME%_Source_%DATE:~4,2%_%DATE:~7,2%.zip
cd ..
del %FNAME%
%PROTON_DIR%\shared\win\utils\7za.exe a -r -tzip %FNAME% %APP_NAME%\* Signing\* base_setup.bat -x!*.zip -x!*.svn -x!*.ncb -x!%APP_NAME%\.vs -x!Browse.VC.db -x!*.bsc -x!*.pdb -x!*.sbr -x!*.ilk -x!*.idb -x!.o -x!*.obj -x!*.DS_Store -x!._* -x!%APP_NAME%\dist -x!%APP_NAME%\Binaries -x!%APP_NAME%\bin -x!%APP_NAME%\Build -x!%APP_NAME%\Intermediate -x!%APP_NAME%\Saved
cd %FNAME%