-- premake5.lua
workspace "WFP.Updater"
   configurations { "Release", "Debug" }
   location "build"
   
   files { "source/Header Files/*.h" }
   files { "source/Source Files/*.cpp", "source/Source Files/*.c" }
   
   files { "external/resources/*.rc" }
   
   files { "external/cpr/cpr/*.cpp" }
   files { "external/jsoncpp/src/lib_json/*.cpp" }
   files { "external/zipper/zipper/*.cpp" }
   files { "external/zipper/minizip/*.cpp" }
   
   includedirs { "source/Header Files/" }
   includedirs { "external/curl/builds/libcurl-vc14-x86-release-static-ipv6-sspi-winssl/include" }
   includedirs { "external/cpr/include" }
   includedirs { "external/date" }
   includedirs { "external/inireader" }
   includedirs { "external/jsoncpp/include" }
   includedirs { "external/zipper" }
   includedirs { "external/zipper/minizip" }
   includedirs { "external/zipper/zlib" }
   
   libdirs { "external/zipper/zlib" }
   libdirs { "external/zipper/lib" }
   libdirs { "external/curl/builds/libcurl-vc14-x86-release-static-ipv6-sspi-winssl/lib" }
   
   defines { "INSECURE_CURL", "CURL_STATICLIB" }

project "WFP.UpdaterApp"
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "WFP.Updater"
   targetextension ".exe"

   filter "configurations:Debug"
      defines { "DEBUG" }
      flags { "Symbols" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
	  flags { "StaticRuntime" }
	  characterset ("MBCS")
	  
	  
project "WFP.UpdaterPlugin"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetname "WFP.Updater"
   targetextension ".asi"

   filter "configurations:Debug"
      defines { "DEBUG" }
      flags { "Symbols" }
	  characterset ("MBCS")

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
	  flags { "StaticRuntime" }
	  characterset ("MBCS")