@echo off
rem Ramen VRM MOD installer.
rem Usage: install.bat [-DryRun] [-GamePath "<folder>"] [-Yes]
setlocal
set "PS1=%~dp0ramen_vrm_setup.ps1"
if not exist "%PS1%" set "PS1=%~dp0scripts\ramen_vrm_setup.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Mode Install %*
set "RC=%ERRORLEVEL%"
if not defined RVRM_NOPAUSE pause
exit /b %RC%
