-- modupdater
workspace "modupdater"
   configurations { "Release", "Debug" }
   location "build"
   
   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""} 
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{wks.name}\"", "rsc_ProductName=\"%{wks.name}\"", "rsc_OriginalFilename=\"%{wks.name}.asi\"" }
   defines { "rsc_FileDescription=\"%{wks.name}\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/modupdater\"" }
   
   files { "source/Header Files/*.h" }
   files { "source/Source Files/*.cpp", "source/Source Files/*.c" }
   
   files { "source/Includes/Resources/*.rc" }
   
   files { "external/cpr/cpr/*.cpp" }
   files { "external/jsoncpp/src/lib_json/*.cpp" }
   files { "external/zipper/zipper/*.cpp" }
   files { "external/zipper/minizip/*.cpp" }
   
   includedirs { "source/Header Files/" }
   includedirs { "external/inireader" }
   includedirs { "external/curl/builds/libcurl-vc-x86-release-static-ipv6-sspi-winssl/include" }
   includedirs { "external/cpr/include" }
   includedirs { "external/date/include/date" }
   includedirs { "external/jsoncpp/include" }
   includedirs { "external/zipper/zipper" }
   includedirs { "external/zipper/minizip" }
   includedirs { "external/zlib" }
   
   libdirs { "source/Includes/Libs" }
   libdirs { "external/curl/builds/libcurl-vc-x86-release-static-ipv6-sspi-winssl/lib" }
   
   links { "Wldap32.lib" }
   links { "crypt32.lib" }
   links { "Ws2_32.lib" }
   links { "version.lib" }
   links { "zlibstat.lib" }
   links { "libZipper-static.lib" }
   links { "libcurl_a.lib" }
   defines { "CURL_STATICLIB", "WIN32" }
   
    prebuildcommands {
        "msbuild ../build/libZipper.sln /t:zlibstat /p:Configuration=ReleaseWithoutAsm /p:Platform=Win32",
        "msbuild ../build/libZipper.sln /t:libZipper-static /p:Configuration=ReleaseWithoutAsm /p:Platform=Win32",
        "cd ../external/curl/",
		"call buildconf.bat",
		"cd winbuild",
        "nmake /f Makefile.vc mode=static RTLIBCFG=static ENABLE_IDN=no"
    }

project "UpdaterApp"
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "modupdater"
   targetextension ".exe"
   characterset ("MBCS")
   flags { "StaticRuntime" }
   buildoptions {"-std:c++latest"}

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"
      flags { "StaticRuntime" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      
      
project "UpdaterPlugin"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "modupdater"
   targetextension ".asi"
   characterset ("MBCS")
   flags { "StaticRuntime" }
   buildoptions {"-std:c++latest"}

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      

project "UpdaterLib"
   kind "StaticLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "modupdater"
   targetextension ".lib"
   characterset ("MBCS")
   flags { "StaticRuntime" }
   buildoptions {"-std:c++latest"}
   
   excludes { "source/Includes/Resources/*.rc" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
      
-- libZipper
workspace "libZipper"
   configurations { "ReleaseWithoutAsm", "Debug" }
   location "build"
   ignoredefaultlibraries { "MSVCRT" }
   
project "zlibstat"
   kind "StaticLib"
   language "C++"
   targetdir "source/Includes/Libs"
   targetextension ".lib"
   
   files { "external/zlib/*.h", "external/zlib/*.c" }
   files { "external/zlib/contrib/vstudio/vc14/zlib.rc" }
   files { "external/zlib/contrib/vstudio/vc14/zlibvc.def" }
   removefiles { "inffas8664.c" }
      
   defines { "WIN32;_CRT_NONSTDC_NO_DEPRECATE;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_WARNINGS;" }
   
   characterset ("MBCS")

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:ReleaseWithoutAsm"
      defines { "NDEBUG" }
      optimize "On"
      flags { "StaticRuntime" }
      
project "libZipper-static"
   kind "StaticLib"
   language "C++"
   targetdir "source/Includes/Libs"
   targetextension ".lib"
   
   files { "external/zipper/zipper/*.h", "external/zipper/zipper/*.cpp" }
   files { "external/zipper/minizip/crypt.h" }
   files { "external/zipper/minizip/ioapi.h" }
   files { "external/zipper/minizip/ioapi_buf.h" }
   files { "external/zipper/minizip/ioapi_mem.h" }
   files { "external/zipper/minizip/iowin32.h" }
   files { "external/zipper/minizip/unzip.h" }
   files { "external/zipper/minizip/zip.h" }
   files { "external/zipper/minizip/ioapi.c" }
   files { "external/zipper/minizip/ioapi_buf.c" }
   files { "external/zipper/minizip/ioapi_mem.c" }
   files { "external/zipper/minizip/iowin32.c" }
   files { "external/zipper/minizip/unzip.c" }
   files { "external/zipper/minizip/zip.c" }
   includedirs { "external/zipper/minizip" }
   includedirs { "external/zlib/" }
   includedirs { "external/zipper/zipper" }
      
   defines { "WIN32;_WINDOWS;NDEBUG;USE_ZLIB;_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_DEPRECATE;" }

   characterset ("MBCS")
   
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:ReleaseWithoutAsm"
      defines { "NDEBUG" }
      optimize "On"
      flags { "StaticRuntime" }