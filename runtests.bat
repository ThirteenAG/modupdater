@echo off
setlocal
cd /d "%~dp0"

rem Builds the library and the test projects, then runs the automated tests.
rem   runtests.bat                        Release x64, all tests
rem   runtests.bat Debug Win32            other configuration
rem   runtests.bat Release x64 Zip Http   only tests whose name contains "Zip" or "Http"
rem Manual tests: bin\<platform>\<configuration>\TestInstallerApp.exe (scenario picker) and TestApp.exe (updater)

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=x64"
shift
shift
set "FILTERS="
:args
if "%~1"=="" goto build
set "FILTERS=%FILTERS% %1"
shift
goto args

:build
call "%~dp0premake5.bat" || exit /b 1

set "MSBUILD=msbuild"
where msbuild >nul 2>nul
if errorlevel 1 (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)

"%MSBUILD%" -m build/modupdater.slnx /t:UpdaterLib /property:Configuration=%CONFIG% /property:Platform=%PLATFORM% /verbosity:minimal /nologo || exit /b 1
"%MSBUILD%" -m build/test.slnx /property:Configuration=%CONFIG% /property:Platform=%PLATFORM% /verbosity:minimal /nologo || exit /b 1

"bin\%PLATFORM%\%CONFIG%\UnitTests.exe"%FILTERS%
exit /b %ERRORLEVEL%
