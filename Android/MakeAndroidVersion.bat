cd ..
call app_info_setup.bat

REM Set the paths to your Unreal Engine installation and project
set UE4_ROOT=%UE5_DIR%
set PROJECT_ROOT=%APP_PATH%
set CONFIGURATION="Shipping"
set PLATFORM="Android"
set COOK_FLAVOR="ASTC"

REM Clean intermediate and saved directories
cd %PROJECT_ROOT%
:rd /s /q Intermediate
:rd /s /q Saved

REM Change directory to the Unreal Engine root
cd %UE4_ROOT%

REM Run the Unreal Build Tool to build the project
echo Building the project...
%UE4_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe HoloVCS Android Shipping -Project="%PROJECT_ROOT%\HoloVCS.uproject" -Manifest="%PROJECT_ROOT%\Intermediate\Build\Manifest.xml" -remoteini="%PROJECT_ROOT%" -skipdeploy -distribution -log="%USERPROFILE%\AppData\Roaming\Unreal Engine\AutomationTool\Logs\HoloVCS-Android-Shipping.txt"

if %ERRORLEVEL% NEQ 0 (
    echo Building project files failed!
    pause
    exit /b %ERRORLEVEL%
)

REM Change directory to the project root
cd %PROJECT_ROOT%

REM Run the UnrealEditor-Cmd.exe to cook the project
echo Cooking the project...
"%UE4_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT_ROOT%\HoloVCS.uproject" -run=Cook -Map=/Game/Maps/NewMap+/Game/Maps/NewMap -CookCultures=en -TargetPlatform=%PLATFORM% -cookflavor=%COOK_FLAVOR% -unversioned -fileopenlog -abslog="%PROJECT_ROOT%\Saved\Cook.log" -stdout -CrashForUAT -unattended -NoLogTimes -UTF8Output

REM Run the AutomationTool to package the project
echo Packaging the project...
call "%UE4_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" -ScriptsForProject="%PROJECT_ROOT%\HoloVCS.uproject" BuildCookRun -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -project="%PROJECT_ROOT%\HoloVCS.uproject" -target=HoloVCS -unrealexe="%UE4_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -platform=%PLATFORM% -cookflavor=%COOK_FLAVOR% -installed -stage -archive -package -build -pak -iostore -compressed -prereqs -archivedirectory="%PROJECT_ROOT%\dist/android" -distribution -clientconfig=%CONFIGURATION% -nodebuginfo

if %ERRORLEVEL% NEQ 0 (
    echo Packaging failed!
    pause
    exit /b %ERRORLEVEL%
)

cd dist/android
call Uninstall_HoloVCS-Android-Shipping-arm64.bat
call Install_HoloVCS-Android-Shipping-arm64.bat
cd ../../android
call RunApp.bat

echo Done.
pause
