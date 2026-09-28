<#
    Rebuilds the libraries in source\external from the official source releases:
      zlib  deflate/inflate for the zip archives (no gz* file functions)
      curl  HTTP and HTTPS only, TLS by Windows (Schannel), no zlib or other third party libraries
      cpr   C++ wrapper of curl
    for Win32 and x64, Debug and Release, as static libraries with the static CRT (/MT, /MTd).
    Everything is optimized for size and has no debug information: the Debug libraries only differ in what
    must match a Debug build that links them, the debug CRT and the checks of the C++ library.
    With the MSVC toolset of Visual Studio 2022 (v143) when it is installed, so any newer toolset links them.

      updatedeps.bat              rebuilds the versions pinned in source\external\deps.json
      updatedeps.bat -Latest      looks up the newest releases on GitHub, builds and pins them

    Needs Visual Studio 2022 or newer with the C++ workload (its CMake is used) and an internet connection.
    Afterwards run runtests.bat and builddist.bat.
#>
param(
    [switch]$Latest
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$external = $PSScriptRoot
$root = (Resolve-Path (Join-Path $external '..\..')).Path
$work = Join-Path $root 'build\deps'
$depsFile = Join-Path $external 'deps.json'
$names = 'zlib', 'curl', 'cpr'
$platforms = @(
    @{ Arch = 'Win32'; Triplet = 'x86-windows-static' },
    @{ Arch = 'x64'; Triplet = 'x64-windows-static' }
)
$repositories = @{ zlib = 'madler/zlib'; curl = 'curl/curl'; cpr = 'libcpr/cpr' }
$licenses = @{ zlib = 'LICENSE'; curl = 'COPYING'; cpr = 'LICENSE' }

function Step([string]$text) { Write-Host "== $text" -ForegroundColor Cyan }

function Get-Sha256([string]$file)
{
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($file)
    try { return -join ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function Get-SourceUrl([string]$name, [string]$version)
{
    switch ($name)
    {
        'zlib' { "https://github.com/madler/zlib/releases/download/v$version/zlib-$version.tar.gz" }
        'curl' { "https://github.com/curl/curl/releases/download/curl-$($version -replace '\.', '_')/curl-$version.tar.gz" }
        'cpr' { "https://github.com/libcpr/cpr/archive/refs/tags/$version.tar.gz" }
    }
}

# ---- tools

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio was not found (vswhere.exe is missing)' }
$vsArgs = @('-latest', '-prerelease', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
$vs = & $vswhere @vsArgs -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio with the "Desktop development with C++" workload was not found' }
$vsMajor = ([version](& $vswhere @vsArgs -property installationVersion | Select-Object -First 1)).Major

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source }
if (-not $cmake) { throw 'CMake was not found, install the "C++ CMake tools for Windows" component of Visual Studio' }

$generator = @()
$generatorYear = @{ 16 = '2019'; 17 = '2022'; 18 = '2026' }[$vsMajor]
if ($generatorYear) { $generator = @('-G', "Visual Studio $vsMajor $generatorYear", "-DCMAKE_GENERATOR_INSTANCE=$vs") }

# A static library links with the toolset that built it or a newer one: the toolset of Visual Studio 2022 (v143)
# when it is installed next to a newer one, like the dist libraries (premake5.lua)
$toolset = @()
$toolsetFile = Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
$v143File = Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.v143.default.txt'
if ($generator -and $vsMajor -gt 17 -and (Test-Path $v143File))
{
    $toolset = @('-T', 'v143')
    $toolsetFile = $v143File
}

$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'

Write-Host "Visual Studio $vsMajor ($vs)"
Write-Host "MSVC $((Get-Content $toolsetFile -ErrorAction SilentlyContinue | Select-Object -First 1))$(if ($toolset) { ' (v143)' })"
Write-Host "CMake: $cmake"

$logs = Join-Path $work 'logs'
New-Item -ItemType Directory -Force (Join-Path $work 'downloads'), $logs | Out-Null

# Runs a tool with its output in a log file, shows the end of the log when it fails
function Invoke-Tool([string]$log, [string]$exe, [string[]]$arguments)
{
    $ErrorActionPreference = 'Continue' # native tools write warnings to stderr
    Add-Content -Path $log -Encoding UTF8 -Value "> $exe $($arguments -join ' ')"
    & $exe @arguments 2>&1 | ForEach-Object { "$_" } | Add-Content -Path $log -Encoding UTF8
    if ($LASTEXITCODE -ne 0)
    {
        Get-Content $log -Tail 40 | ForEach-Object { Write-Host "    $_" }
        throw "$([IO.Path]::GetFileName($exe)) failed with exit code $LASTEXITCODE, the whole output is in $log"
    }
}

# ---- versions

$deps = Get-Content $depsFile -Raw | ConvertFrom-Json
if ($Latest)
{
    Step 'Looking up the newest releases'
    foreach ($name in $names)
    {
        $release = Invoke-RestMethod "https://api.github.com/repos/$($repositories[$name])/releases/latest" -Headers @{ 'User-Agent' = 'modupdater-deps' }
        $version = ($release.tag_name -replace '^(curl-|v)', '') -replace '_', '.'
        if ($version -ne $deps.$name.version)
        {
            Write-Host "$name $($deps.$name.version) -> $version"
            $deps.$name.version = $version
            $deps.$name.sha256 = ''
        }
        else
        {
            Write-Host "$name $version is the newest release"
        }
    }
}

# ---- sources

$sources = @{}
foreach ($name in $names)
{
    $version = $deps.$name.version
    $url = Get-SourceUrl $name $version
    $archive = Join-Path $work "downloads\$name-$version.tar.gz"
    if (-not (Test-Path $archive))
    {
        Step "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile "$archive.part" -UseBasicParsing -Headers @{ 'User-Agent' = 'modupdater-deps' }
        Move-Item "$archive.part" $archive -Force
    }

    # a pinned version must be exactly the archive it was pinned with
    $hash = Get-Sha256 $archive
    if ($deps.$name.sha256 -and $deps.$name.sha256 -ne $hash)
    {
        Remove-Item $archive
        throw "$name $version does not match the SHA-256 in deps.json (expected $($deps.$name.sha256), got $hash)"
    }
    $deps.$name.sha256 = $hash

    $folder = Join-Path $work "src\$name"
    if (Test-Path $folder) { Remove-Item $folder -Recurse -Force }
    New-Item -ItemType Directory $folder | Out-Null
    & $tar -xf $archive -C $folder
    if ($LASTEXITCODE -ne 0) { throw "Cannot unpack $archive" }
    $sources[$name] = (Get-ChildItem $folder -Directory | Select-Object -First 1).FullName
}

# zlib is built from its sources directly: the same result for every zlib version, no shared library,
# only what deflates and inflates memory (minizip does the file handling)
$zlibProject = Join-Path $work 'zlib-project'
New-Item -ItemType Directory -Force $zlibProject | Out-Null
Set-Content -Path (Join-Path $zlibProject 'CMakeLists.txt') -Encoding ASCII -Value @'
cmake_minimum_required(VERSION 3.15)
project(zlib C)
set(ZLIB_SOURCE "" CACHE PATH "zlib source folder")
set(sources adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
list(TRANSFORM sources PREPEND "${ZLIB_SOURCE}/")
list(TRANSFORM sources APPEND ".c")
add_library(zlib STATIC ${sources})
target_compile_definitions(zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
set_target_properties(zlib PROPERTIES DEBUG_POSTFIX d)
install(TARGETS zlib ARCHIVE DESTINATION lib)
install(FILES "${ZLIB_SOURCE}/zlib.h" "${ZLIB_SOURCE}/zconf.h" DESTINATION include)
'@

# ---- build options

$common = @(
    '-DBUILD_SHARED_LIBS=OFF',
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',   # CMAKE_MSVC_RUNTIME_LIBRARY
    '-DCMAKE_POLICY_DEFAULT_CMP0141=NEW',   # CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>',
    '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=', # no debug information in any configuration
    '-DCMAKE_INSTALL_MESSAGE=NEVER'
)
# Debug keeps its asserts (no NDEBUG), /MTd defines _DEBUG. The vectorized algorithms of the STL call helpers
# that new toolset versions add all the time, a project with an older toolset could not link them.
$smallCode = @(
    '-DCMAKE_C_FLAGS_RELEASE=/O1 /Ob1 /Gw /DNDEBUG', '-DCMAKE_CXX_FLAGS_RELEASE=/O1 /Ob1 /Gw /DNDEBUG /D_USE_STD_VECTOR_ALGORITHMS=0',
    '-DCMAKE_C_FLAGS_DEBUG=/O1 /Ob1 /Gw', '-DCMAKE_CXX_FLAGS_DEBUG=/O1 /Ob1 /Gw /D_USE_STD_VECTOR_ALGORITHMS=0'
)
# inflate is the hot loop of an installation
$fastCode = @('-DCMAKE_C_FLAGS_RELEASE=/O2 /Gw /DNDEBUG', '-DCMAKE_C_FLAGS_DEBUG=/O2 /Gw')

$curlOptions = @(
    '-DBUILD_STATIC_LIBS=ON', '-DBUILD_CURL_EXE=OFF', '-DBUILD_TESTING=OFF', '-DBUILD_EXAMPLES=OFF',
    '-DBUILD_LIBCURL_DOCS=OFF', '-DBUILD_MISC_DOCS=OFF', '-DENABLE_CURL_MANUAL=OFF',
    '-DCURL_STATIC_CRT=ON', '-DENABLE_UNICODE=ON', '-DCURL_USE_PKGCONFIG=OFF', '-DCURL_LTO=OFF',
    # HTTP and HTTPS with the TLS of Windows
    '-DHTTP_ONLY=ON', '-DCURL_USE_SCHANNEL=ON', '-DCURL_WINDOWS_SSPI=ON',
    '-DCURL_USE_OPENSSL=OFF', '-DCURL_USE_MBEDTLS=OFF', '-DCURL_USE_WOLFSSL=OFF', '-DCURL_USE_GNUTLS=OFF', '-DCURL_USE_RUSTLS=OFF',
    '-DCURL_CA_BUNDLE=none', '-DCURL_CA_PATH=none', '-DCURL_DISABLE_CA_SEARCH=ON',
    # no third party libraries
    '-DCURL_ZLIB=OFF', '-DCURL_BROTLI=OFF', '-DCURL_ZSTD=OFF', '-DCURL_USE_LIBPSL=OFF', '-DCURL_USE_LIBSSH2=OFF', '-DCURL_USE_LIBSSH=OFF',
    '-DCURL_USE_GSSAPI=OFF', '-DUSE_NGHTTP2=OFF', '-DUSE_NGTCP2=OFF', '-DUSE_QUICHE=OFF', '-DUSE_MSH3=OFF', '-DUSE_OPENSSL_QUIC=OFF',
    '-DUSE_LIBIDN2=OFF', '-DUSE_WIN32_IDN=OFF', '-DUSE_LIBRTMP=OFF', '-DENABLE_ARES=OFF',
    # nothing a downloader needs
    '-DCURL_DISABLE_LDAP=ON', '-DCURL_DISABLE_LDAPS=ON', '-DCURL_DISABLE_ALTSVC=ON', '-DCURL_DISABLE_HSTS=ON', '-DCURL_DISABLE_DOH=ON',
    '-DCURL_DISABLE_NETRC=ON', '-DCURL_DISABLE_NTLM=ON', '-DCURL_DISABLE_KERBEROS_AUTH=ON', '-DCURL_DISABLE_NEGOTIATE_AUTH=ON',
    '-DCURL_DISABLE_DIGEST_AUTH=ON', '-DCURL_DISABLE_AWS=ON', '-DCURL_DISABLE_SRP=ON', '-DCURL_DISABLE_WEBSOCKETS=ON',
    '-DCURL_DISABLE_GETOPTIONS=ON', '-DCURL_DISABLE_PROGRESS_METER=ON', '-DCURL_DISABLE_SHUFFLE_DNS=ON', '-DENABLE_UNIX_SOCKETS=OFF'
)

$cprOptions = @('-DCPR_USE_SYSTEM_CURL=ON', '-DCPR_BUILD_TESTS=OFF', '-DCPR_ENABLE_SSL=ON', '-DCPR_ENABLE_CURL_HTTP_ONLY=ON')

function Build-Project([string]$name, [string]$source, [string]$arch, [string[]]$options, [hashtable]$prefixes)
{
    $build = Join-Path $work "build\$name-$arch"
    $log = Join-Path $logs "$name-$arch.log"
    if (Test-Path $build) { Remove-Item $build -Recurse -Force }
    if (Test-Path $log) { Remove-Item $log }
    Invoke-Tool $log $cmake (@('-S', $source, '-B', $build) + $generator + @('-A', $arch) + $toolset + $common + $options)
    foreach ($config in 'Release', 'Debug')
    {
        Invoke-Tool $log $cmake @('--build', $build, '--config', $config, '--parallel')
        Invoke-Tool $log $cmake @('--install', $build, '--config', $config, '--prefix', $prefixes[$config])
    }
    return $log
}

# ---- build

$stage = Join-Path $work 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }

foreach ($platform in $platforms)
{
    $arch = $platform.Arch
    $out = Join-Path $stage $arch

    Step "zlib $($deps.zlib.version) $arch"
    $zlib = Join-Path $out 'zlib'
    Build-Project 'zlib' $zlibProject $arch ($fastCode + "-DZLIB_SOURCE=$($sources.zlib)") @{ Release = $zlib; Debug = $zlib } | Out-Null

    Step "curl $($deps.curl.version) $arch"
    $curl = Join-Path $out 'curl'
    $curlLog = Build-Project 'curl' $sources.curl $arch ($smallCode + $curlOptions) @{ Release = $curl; Debug = $curl }
    Get-Content $curlLog | Where-Object { $_ -match '^-- (Protocols|Features|Enabled SSL backends):' } | Select-Object -First 3 | ForEach-Object { Write-Host "   $($_.Substring(3))" }

    Step "cpr $($deps.cpr.version) $arch"
    Build-Project 'cpr' $sources.cpr $arch ($smallCode + $cprOptions + "-DCMAKE_PREFIX_PATH=$curl") @{ Release = (Join-Path $out 'cpr'); Debug = (Join-Path $out 'cpr-debug') } | Out-Null
}

# ---- check: static libraries that only use the static CRT and Windows

function Get-DefaultLibs([string]$lib)
{
    $ErrorActionPreference = 'Continue'
    $libs = & $dumpbin /nologo /directives $lib 2>&1 | ForEach-Object { "$_" } |
        Select-String -Pattern '/DEFAULTLIB:"?([^"\s]+)' -AllMatches | ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() -replace '\.lib$', '' }
    return @($libs | Sort-Object -Unique)
}

$outputs = @{
    zlib = @{ Include = 'zlib\include'; Release = 'zlib\lib\zlib.lib'; Debug = 'zlib\lib\zlibd.lib'; Lib = 'zlib.lib'; DebugLib = 'zlibd.lib' }
    curl = @{ Include = 'curl\include'; Release = 'curl\lib\libcurl.lib'; Debug = 'curl\lib\libcurl-d.lib'; Lib = 'libcurl.lib'; DebugLib = 'libcurl-d.lib' }
    cpr = @{ Include = 'cpr\include'; Release = 'cpr\lib\cpr*.lib'; Debug = 'cpr-debug\lib\cpr*.lib'; Lib = 'cpr.lib'; DebugLib = 'cpr.lib' }
}

if ($dumpbin)
{
    Step 'Checking the libraries'
    foreach ($platform in $platforms)
    {
        foreach ($name in $names)
        {
            foreach ($config in 'Release', 'Debug')
            {
                $lib = Get-Item (Join-Path $stage "$($platform.Arch)\$($outputs[$name][$config])") | Select-Object -First 1
                if (-not $lib) { throw "The $config library of $name ($($platform.Arch)) was not built" }
                $defaults = Get-DefaultLibs $lib.FullName
                $crt = if ($config -eq 'Debug') { 'libcmtd' } else { 'libcmt' }
                $dynamic = $defaults | Where-Object { $_ -match '^(msvcrtd?|msvcprtd?|ucrtd?|vcruntimed?)$' }
                if ($dynamic) { throw "$($lib.FullName) uses the DLL runtime ($($dynamic -join ', '))" }
                if ($defaults -notcontains $crt) { throw "$($lib.FullName) is not built with the static CRT ($($defaults -join ', '))" }
            }
        }
    }
    Write-Host '   all libraries are static and use the static CRT'
}
else
{
    Write-Warning 'dumpbin.exe was not found, the libraries were not checked'
}

# the Windows libraries curl needs are linked by premake5.lua (and merged into the dist libraries)
$pc = Get-Content (Join-Path $stage 'x64\curl\lib\pkgconfig\libcurl.pc') | Where-Object { $_ -match '^Libs\.private:' } | Select-Object -First 1
$systemLibs = @([regex]::Matches("$pc", '-l(\w+)') | ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } | Sort-Object -Unique)
Write-Host "   curl needs: $($systemLibs -join ', ')"
$linked = (Get-Content (Join-Path $root 'premake5.lua') -Raw) + (Get-Content (Join-Path $root 'source\stdafx.h') -Raw)
$missing = @($systemLibs | Where-Object { $linked -notmatch "(?i)[`"']$_(\.lib)?[`"']" })
if ($missing) { Write-Warning "premake5.lua does not link $($missing -join ', '): add them next to crypt32.lib" }

# curl.h selects the static library by itself, like the vcpkg package did: nothing to define for users
foreach ($platform in $platforms)
{
    $header = Join-Path $stage "$($platform.Arch)\curl\include\curl\curl.h"
    $text = [IO.File]::ReadAllText($header)
    if ($text -notmatch '(?m)^#ifdef CURL_STATICLIB\r?$') { throw "curl.h changed, cannot make it select the static library: $header" }
    $text = $text -replace '(?m)^#ifdef CURL_STATICLIB(\r?)$', '#if 1 /* static library, see source/external/update.ps1 */$1'
    [IO.File]::WriteAllText($header, $text)
}

# ---- replace source\external\<name>_<triplet>

Step 'Updating source\external'
$before = @{}
Get-ChildItem $external -Recurse -Filter *.lib | ForEach-Object { $before[$_.FullName] = $_.Length }

foreach ($platform in $platforms)
{
    foreach ($name in $names)
    {
        $target = Join-Path $external "$($name)_$($platform.Triplet)"
        $out = $outputs[$name]
        $from = Join-Path $stage $platform.Arch
        if (Test-Path $target) { Remove-Item $target -Recurse -Force }
        New-Item -ItemType Directory -Force (Join-Path $target 'lib'), (Join-Path $target 'debug\lib') | Out-Null
        Copy-Item (Join-Path $from $out.Include) (Join-Path $target 'include') -Recurse
        Copy-Item (Get-Item (Join-Path $from $out.Release) | Select-Object -First 1).FullName (Join-Path $target "lib\$($out.Lib)")
        Copy-Item (Get-Item (Join-Path $from $out.Debug) | Select-Object -First 1).FullName (Join-Path $target "debug\lib\$($out.DebugLib)")
        Copy-Item (Join-Path $sources[$name] $licenses[$name]) (Join-Path $target 'LICENSE.txt')
    }
}

$json = "{`n" + (($names | ForEach-Object { '  "{0}": {{ "version": "{1}", "sha256": "{2}" }}' -f $_, $deps.$_.version, $deps.$_.sha256 }) -join ",`n") + "`n}`n"
[IO.File]::WriteAllText($depsFile, $json)

Get-ChildItem $external -Recurse -Filter *.lib | Where-Object { $_.FullName -match '(zlib|curl|cpr)_x(86|64)-windows-static' } | Sort-Object FullName | ForEach-Object {
    $old = $before[$_.FullName]
    $relative = $_.FullName.Substring($external.Length + 1)
    Write-Host ("   {0,-52} {1,8:N0} KB{2}" -f $relative, ($_.Length / 1KB), $(if ($old) { "  (was {0:N0} KB)" -f ($old / 1KB) } else { '' }))
}

Step "Done: zlib $($deps.zlib.version), curl $($deps.curl.version), cpr $($deps.cpr.version). Next: runtests.bat, then builddist.bat"
