@echo off
rem Shared end-of-script pause used by every .bat in this project.
rem
rem The rule: a script pauses when it finishes AND when it fails, so the output stays readable when
rem the script was double-clicked from Explorer or ran long enough that you walked away.
rem
rem To skip every pause (automation, CI, AI-driven builds) define NOPAUSE before running:
rem     set NOPAUSE=1
rem     PackageWin64LKGRelease.bat
rem or for a single run:  cmd /c "set NOPAUSE=1 && PackageWin64LKGRelease.bat"
rem
rem HOLO_BAT_NESTED is set by a parent script around a "call" to another script in this project so a
rem failing child does not pause on top of the parent's own pause. Do not set it by hand.
rem
rem NOPAUSE is ours. NO_PAUSE (with the underscore) is a different variable belonging to the shared
rem signing helper, %RT_PROJECTS%\Signing\sign.bat, and the release scripts set it on their own.
if defined NOPAUSE exit /b 0
if defined HOLO_BAT_NESTED exit /b 0
pause
exit /b 0
