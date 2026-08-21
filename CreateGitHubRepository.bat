@echo off
rem One-time GitHub setup for this project.  Pauses at each confirmation point and at the end;
rem set the NOPAUSE env var to run it unattended (see PauseHelper.bat).
pushd "%~dp0"
call "%~dp0app_info_setup.bat"
if errorlevel 1 goto :fail
echo You have to have already created the account on Github with the name %APP_NAME%.  Target base repository set with the GITHUBNAME var in ../setup_bars.bat
call "%~dp0PauseHelper.bat"
echo Warning: This will add ALL files in the tree of the folder %APP_NAME% and commit them to https://github.com/%GITHUBNAME%/%APP_NAME%.git, you should check nothing sensitive (passwords,etc) was added before you do the final push command.
call "%~dp0PauseHelper.bat"
@echo on
git init
echo Need to add at least one file for initial commit
:git add README.md
git remote add origin https://github.com/%GITHUBNAME%/%APP_NAME%.git
git remote -v
git pull --allow-unrelated-histories
:git commit -m "First commit"
@echo off
echo All done.  Check that the committed files look ok, then do a final push.

REM guess I'll do the final push manually for safety, so commented out
REM git push -u origin main
call "%~dp0PauseHelper.bat"
popd
exit /b 0

:fail
echo.
echo *** GitHub setup FAILED ***
call "%~dp0PauseHelper.bat"
popd
exit /b 1
