@echo off
setlocal
cd /d "%~dp0"

rem Builds the four static libraries in dist\ (Release/Debug, x64/Win32).
rem Uses msbuild from PATH (Developer Command Prompt, CI) or finds Visual Studio with vswhere.
rem
rem A static library links with the toolset that built it or a newer one. The dist libraries are built with the
rem MSVC toolset of Visual Studio 2022 (v143) when it is installed, so every Visual Studio 2026 links them as they
rem are. Other builds (Visual Studio, runtests.bat) use the default toolset and also write to dist\: run this last
rem before committing the libraries.

call "%~dp0premake5.bat" || exit /b 1

set "MSBUILD=msbuild"
where msbuild >nul 2>nul
if errorlevel 1 (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
)

set "TOOLSET="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -property installationPath`) do set "VSDIR=%%i"
if exist "%VSDIR%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.v143.default.txt" (
    set "TOOLSET=/property:PlatformToolset=v143"
    echo Using the MSVC v143 toolset
) else (
    echo Warning: the MSVC v143 toolset is not installed, the libraries only link with this toolset or a newer one.
    echo Add "MSVC v143 - VS 2022 C++ x64/x86 build tools" in the Visual Studio Installer, Individual components.
)

rem a full rebuild: no objects of a build with another toolset end up in the libraries
for %%p in (x64 Win32) do (
    for %%c in (Release Debug) do (
        echo Building UpdaterLib %%c^|%%p
        "%MSBUILD%" -m build/modupdater.slnx /t:UpdaterLib:Rebuild /property:Configuration=%%c /property:Platform=%%p %TOOLSET% /verbosity:minimal /nologo || exit /b 1
    )
)

exit /b 0
