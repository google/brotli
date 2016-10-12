-- A solution contains projects, and defines the available configurations
solution "brotli"
configurations { "Release", "Debug" }
targetdir "bin"
location "buildfiles"
flags "RelativeLinks"
includedirs { "include" }

filter "configurations:Release"
  optimize "Speed"
  flags { "StaticRuntime" }

filter "configurations:Debug"
  flags { "Symbols" }

configuration { "gmake" }
  buildoptions { "-Wall -fno-omit-frame-pointer" }
  location "buildfiles/gmake"

configuration { "xcode4" }
  location "buildfiles/xcode4"

configuration "linux"
  links "m"

configuration { "macosx" }
  defines { "OS_MACOSX" }

project "brotlicommon"
  kind "SharedLib"
  language "C"
  files { "common/**.h", "common/**.c" }

project "brotlicommon_static"
  kind "StaticLib"
  targetname "brotlicommon"
  language "C"
  files { "common/**.h", "common/**.c" }

project "brotlidec"
  kind "SharedLib"
  language "C"
  files { "dec/**.h", "dec/**.c" }
  links "brotlicommon"

project "brotlidec_static"
  kind "StaticLib"
  targetname "brotlidec"
  language "C"
  files { "dec/**.h", "dec/**.c" }
  links "brotlicommon_static"

project "brotlienc"
  kind "SharedLib"
  language "C"
  files { "enc/**.h", "enc/**.c" }
  links "brotlicommon"

project "brotlienc_static"
  kind "StaticLib"
  targetname "brotlienc"
  language "C"
  files { "enc/**.h", "enc/**.c" }
  links "brotlicommon_static"

project "bro"
  kind "ConsoleApp"
  language "C"
  linkoptions "-static"
  files { "tools/bro.c" }
  links { "brotlicommon_static", "brotlidec_static", "brotlienc_static" }
