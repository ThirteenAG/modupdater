@echo off
setlocal

rem Rebuilds zlib, curl and cpr in source\external from their official source releases:
rem static libraries with the static CRT, no other dependencies (see source\external\update.ps1).
rem   updatedeps.bat              the versions pinned in source\external\deps.json
rem   updatedeps.bat -Latest      the newest releases, pinned afterwards
rem Then run runtests.bat and builddist.bat.

rem Windows PowerShell must not pick up the modules of PowerShell 7 when started from it
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0source\external\update.ps1" %*
exit /b %ERRORLEVEL%
