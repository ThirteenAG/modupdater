-- modupdater
workspace "modupdater"
   configurations { "Release", "Debug" }
   platforms { "Win32", "x64" }
   location "build"
   cppdialect "C++latest"
   characterset ("UNICODE")
   -- /Zc:inline drops unreferenced inline functions from the objects, the static libraries in dist stay small
   buildoptions { "/utf-8", "/Zc:inline" }
   editandcontinue "Off"
   justmycode "Off"
   multiprocessorcompile ("On")

   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""}
   defines { "rsc_FileVersion=\"2.0.0.0\"", "rsc_ProductVersion=\"2.0.0.0\"" }
   defines { "rsc_InternalName=\"%{wks.name}\"", "rsc_ProductName=\"%{wks.name}\"", "rsc_OriginalFilename=\"%{wks.name}.asi\"" }
   defines { "rsc_FileDescription=\"%{wks.name}\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/modupdater\"" }

   files { "source/*.h", "source/*.hpp" }
   files { "source/*.cpp" }
   files { "dist/libmodupdater.h" }

   files { "source/resources/*.rc" }

   files { "external/jsoncpp/src/lib_json/*.h", "external/jsoncpp/src/lib_json/*.cpp" }
   files { "source/external/zipper/external/minizip/*.h" }
   files { "source/external/zipper/external/minizip/*.c" }
   -- command line tools of minizip
   removefiles { "source/external/zipper/external/minizip/miniunz.c", "source/external/zipper/external/minizip/minizip.c" }

   includedirs { "dist" }
   includedirs { "source" }
   includedirs { "external/inireader" }
   includedirs { "external/date/include/date" }
   includedirs { "external/jsoncpp/include" }
   includedirs { "source/external/zipper/external/minizip" }

   links { "Wldap32.lib" }
   links { "crypt32.lib" }
   links { "Ws2_32.lib" }
   links { "version.lib" }
   links { "cpr.lib" }
   defines { "_CRT_SECURE_NO_WARNINGS", "USE_WINDOWS", "_WINDOWS", "_CRT_NONSTDC_NO_DEPRECATE", "NOMAIN" }

  filter { "platforms:Win32", "configurations:Debug" }
    includedirs { "source/external/cpr_x86-windows-static/include" }
    includedirs { "source/external/curl_x86-windows-static/include" }
    includedirs { "source/external/zlib_x86-windows-static/include" }
    libdirs { "source/external/cpr_x86-windows-static/debug/lib" }
    libdirs { "source/external/curl_x86-windows-static/debug/lib" }
    libdirs { "source/external/zlib_x86-windows-static/debug/lib" }
    links { "libcurl-d.lib" }
    links { "zlibd.lib" }

  filter { "platforms:Win32", "configurations:Release" }
    includedirs { "source/external/cpr_x86-windows-static/include" }
    includedirs { "source/external/curl_x86-windows-static/include" }
    includedirs { "source/external/zlib_x86-windows-static/include" }
    libdirs { "source/external/cpr_x86-windows-static/lib" }
    libdirs { "source/external/curl_x86-windows-static/lib" }
    libdirs { "source/external/zlib_x86-windows-static/lib" }
    links { "libcurl.lib" }
    links { "zlib.lib" }

  filter { "platforms:x64", "configurations:Debug" }
    includedirs { "source/external/cpr_x64-windows-static/include" }
    includedirs { "source/external/curl_x64-windows-static/include" }
    includedirs { "source/external/zlib_x64-windows-static/include" }
    libdirs { "source/external/cpr_x64-windows-static/debug/lib" }
    libdirs { "source/external/curl_x64-windows-static/debug/lib" }
    libdirs { "source/external/zlib_x64-windows-static/debug/lib" }
    links { "libcurl-d.lib" }
    links { "zlibd.lib" }

  filter { "platforms:x64", "configurations:Release" }
    includedirs { "source/external/cpr_x64-windows-static/include" }
    includedirs { "source/external/curl_x64-windows-static/include" }
    includedirs { "source/external/zlib_x64-windows-static/include" }
    libdirs { "source/external/cpr_x64-windows-static/lib" }
    libdirs { "source/external/curl_x64-windows-static/lib" }
    libdirs { "source/external/zlib_x64-windows-static/lib" }
    links { "libcurl.lib" }
    links { "zlib.lib" }

project "UpdaterApp"
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "modupdater%{cfg.architecture}"
   targetextension ".exe"
   staticruntime "On"

   defines { "EXECUTABLE" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


project "UpdaterPlugin"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "modupdater%{cfg.architecture}"
   targetextension ".asi"

   staticruntime "On"

   defines { "DYNAMICLIB" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


project "UpdaterLib"
   kind "StaticLib"
   language "C++"
   targetdir "dist"
   targetname "libmodupdater_%{cfg.shortname}"
   targetextension ".lib"
   staticruntime "On"

   -- main() and DllMain of the standalone updater must not end up in the modules that link the library
   removefiles { "source/main.cpp" }
   removefiles { "source/resources/*.rc" }

   defines { "STATICLIB" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


-- Tests link the static library from dist, run builddist.bat (or build UpdaterLib) first
workspace "test"
   configurations { "Release", "Debug" }
   platforms { "Win32", "x64" }
   location "build"
   cppdialect "C++latest"
   characterset ("UNICODE")
   buildoptions { "/utf-8" }
   multiprocessorcompile ("On")

   includedirs { "dist" }
   includedirs { "source" }
   includedirs { "source/test" }
   includedirs { "external/inireader" }
   includedirs { "external/jsoncpp/include" }
   includedirs { "external/date/include/date" }
   includedirs { "source/external/zipper/external/minizip" }
   libdirs { "dist" }
   links { "libmodupdater_%{cfg.shortname}.lib" }
   defines { "_CRT_SECURE_NO_WARNINGS", "USE_WINDOWS", "_WINDOWS", "STATICLIB" }

   files { "dist/libmodupdater.h" }

   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""}
   defines { "rsc_FileVersion=\"2.0.0.0\"", "rsc_ProductVersion=\"2.0.0.0\"" }
   defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"", "rsc_OriginalFilename=\"%{prj.name}.asi\"" }
   defines { "rsc_FileDescription=\"%{prj.name}\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/modupdater\"" }

  filter { "platforms:Win32" }
    includedirs { "source/external/zlib_x86-windows-static/include" }

  filter { "platforms:x64" }
    includedirs { "source/external/zlib_x64-windows-static/include" }

  filter {}

-- Automated tests: run build\bin\...\UnitTests.exe (see runtests.bat)
project "UnitTests"
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
   targetname "UnitTests"
   staticruntime "On"
   dependson { "TestInstaller", "TestDLL1" }

   files { "source/test/unit/*.cpp" }
   files { "source/test/common/*.h", "source/test/common/*.cpp" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      symbols "On"

-- Installer with a scenario picker: run it without arguments to try the installer UIs
project "TestInstaller"
   kind "WindowedApp"
   language "C++"
   targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
   targetname "TestInstallerApp"
   targetextension ".exe"
   staticruntime "On"

   files { "source/test/installer/*.cpp", "source/test/installer/*.rc" }
   files { "source/test/common/*.h", "source/test/common/*.cpp" }
   files { "source/resources/*.rc" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

-- Simulates a game with two plugins that use the updater
project "TestApp"
   dependson { "TestDLL1", "TestDLL2" }
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
   targetname "TestApp"
   targetextension ".exe"
   staticruntime "On"

   files { "source/test/app/*.cpp" }
   files { "source/test/common/*.h", "source/test/common/*.cpp" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

project "TestDLL1"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
   targetname "TestDLL1"
   targetextension ".asi"
   staticruntime "On"
   defines { "TESTDLL_NAME=\"TestDLL1\"" }

   files { "source/test/dll/*.cpp" }
   files { "source/resources/*.rc" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

project "TestDLL2"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.platform}/%{cfg.buildcfg}"
   targetname "TestDLL2"
   targetextension ".asi"
   staticruntime "On"
   defines { "TESTDLL_NAME=\"TestDLL2\"" }

   files { "source/test/dll/*.cpp" }
   files { "source/resources/*.rc" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
