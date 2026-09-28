@echo off
setlocal
cd /d "%~dp0"

rem Builds the four static libraries in dist\ (Release/Debug, x64/Win32).
rem Uses msbuild from PATH (Developer Command Prompt, CI) or finds Visual Studio with vswhere.

if not exist "build\modupdater.slnx" call "%~dp0premake5.bat" || exit /b 1

set "MSBUILD=msbuild"
where msbuild >nul 2>nul
if errorlevel 1 (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)

for %%p in (x64 Win32) do (
    for %%c in (Release Debug) do (
        echo Building UpdaterLib %%c^|%%p
        "%MSBUILD%" -m build/modupdater.slnx /t:UpdaterLib /property:Configuration=%%c /property:Platform=%%p /verbosity:minimal /nologo || exit /b 1
    )
)

exit /b 0
