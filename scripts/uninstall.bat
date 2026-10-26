@echo off
rem Ramen VRM MOD uninstaller.
rem Usage: uninstall.bat [-DryRun] [-Yes] [-RemoveUserData]
setlocal
set "PS1=%~dp0ramen_vrm_setup.ps1"
if not exist "%PS1%" set "PS1=%~dp0scripts\ramen_vrm_setup.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Mode Uninstall %*
set "RC=%ERRORLEVEL%"
if not defined RVRM_NOPAUSE pause
exit /b %RC%
