-- modupdater
workspace "modupdater"
   configurations { "Release", "Debug" }
   platforms { "Win32", "x64" }
   location "build"
   cppdialect "C++latest"
   characterset ("UNICODE")

   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""} 
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{wks.name}\"", "rsc_ProductName=\"%{wks.name}\"", "rsc_OriginalFilename=\"%{wks.name}.asi\"" }
   defines { "rsc_FileDescription=\"%{wks.name}\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/modupdater\"" }
   
   files { "source/*.h" }
   files { "source/*.cpp" }
   files { "libmodupdater.h" }
   
   files { "source/resources/*.rc" }
   
   files { "external/jsoncpp/src/lib_json/*.h", "external/jsoncpp/src/lib_json/*.cpp" }
   files { "source/external/zipper/external/minizip/*.h" }
   files { "source/external/zipper/external/minizip/*.c" }
   files { "source/external/zipper/external/minizip/zipper/*.cpp" }
   files { "source/external/zipper/external/minizip/*.cpp" }
   files { "source/external/zipper/include/Zipper/*.hpp" }
   files { "source/external/zipper/src/**.*" }

   includedirs { "dist" }
   includedirs { "source" }
   includedirs { "external/inireader" }
   includedirs { "external/date/include/date" }
   includedirs { "external/jsoncpp/include" }
   includedirs { "source/external/zipper/external/minizip" }
   includedirs { "source/external/zipper/external/minizip/zipper" }
   includedirs { "source/external/zipper/external/minizip/minizip" }
   includedirs { "source/external/zipper" }
   includedirs { "source/external/zipper/include" }
   includedirs { "source/external/zipper/src" }
   includedirs { "source/external/zipper/src/utils" }
   includedirs { "source/external/zipper/include/Zipper" }
   
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
   
   excludes { "source/Includes/Resources/*.rc" }

   defines { "STATICLIB" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"


workspace "test"
   configurations { "Release", "Debug" }
   platforms { "Win32", "x64" }
   location "build"
   cppdialect "C++latest"
   characterset ("UNICODE")
   
   includedirs { "dist" }
   libdirs { "dist" }
   links { "libmodupdater_%{cfg.shortname}.lib" }
   
   files { "libmodupdater.h" }
   files { "source/resources/*.rc" }
   files { "source/test/main.cpp" }

   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""} 
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{wks.name}\"", "rsc_ProductName=\"%{wks.name}\"", "rsc_OriginalFilename=\"%{wks.name}.asi\"" }
   defines { "rsc_FileDescription=\"%{wks.name}\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/modupdater\"" }

project "TestApp"
   dependson { "TestDLL1", "TestDLL2" }
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "TestApp"
   targetextension ".exe"
   staticruntime "On"

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

project "TestDLL1"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "TestDLL1"
   targetextension ".asi"

   staticruntime "On"

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

project "TestDLL2"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "TestDLL2"
   targetextension ".asi"

   staticruntime "On"

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"