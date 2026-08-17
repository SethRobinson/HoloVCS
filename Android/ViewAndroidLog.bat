@echo off
echo Connecting to your Android device...
adb connect

echo Starting logcat with filter...
adb logcat | findstr HoloVCSLog
:adb logcat

echo Press any key to exit...
pause
