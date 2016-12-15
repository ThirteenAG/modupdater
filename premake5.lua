-- premake5.lua
workspace "WFP.Updater"
   configurations { "Release", "Debug" }
   location "build"
   
   files { "source/Header Files/*.h" }
   files { "source/Source Files/*.cpp", "source/Source Files/*.c" }
   
   files { "source/includes/resources/*.rc" }
   
   files { "source/includes/cpr/cpr/*.cpp" }
   files { "source/includes/jsoncpp/src/lib_json/*.cpp" }
   files { "source/includes/zipper/*.cpp" }
   
   includedirs { "source/Header Files/" }
   includedirs { "source/includes/curl/builds/libcurl-vc14-x86-release-static-ipv6-sspi-winssl/include" }
   includedirs { "source/includes/cpr/include" }
   includedirs { "source/includes/date" }
   includedirs { "source/includes/jsoncpp/include" }
   includedirs { "source/includes/zipper" }
   includedirs { "source/includes/zlib-1.2.8" }
   
   libdirs { "source/includes/zlib-1.2.8" }
   libdirs { "source/includes/curl/builds/libcurl-vc14-x86-release-static-ipv6-sspi-winssl/lib" }
   
   defines { "INSECURE_CURL", "CURL_STATICLIB" }

project "WFP.CheckForUpdates"
   kind "ConsoleApp"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"

   filter "configurations:Debug"
      defines { "DEBUG" }
      flags { "Symbols" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
	  flags { "StaticRuntime" }
	  characterset ("MBCS")
	  
	  
project "WFP.Autoupdater"
   kind "SharedLib"
   language "C++"
   targetdir "bin/%{cfg.buildcfg}"
   targetextension ".asi"

   filter "configurations:Debug"
      defines { "DEBUG" }
      flags { "Symbols" }

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"
	  flags { "StaticRuntime" }
	  characterset ("MBCS")