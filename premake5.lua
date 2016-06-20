-- A solution contains projects, and defines the available configurations
solution "brotli"
configurations { "Release", "Debug" }
targetdir "bin"
location "buildfiles"
flags "RelativeLinks"

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
  defines { "DOS_MACOSX" }

project "brotli_common"
  kind "SharedLib"
  language "C"
  files { "common/**.h", "common/**.c" }

project "brotli_dec"
  kind "SharedLib"
  language "C"
  files { "dec/**.h", "dec/**.c" }
  links "brotli_common"

project "brotli_enc"
  kind "SharedLib"
  language "C"
  files { "enc/**.h", "enc/**.c" }
  links "brotli_common"

project "brotli"
  kind "StaticLib"
  language "C"
  files {
    "common/**.h", "common/**.c",
    "dec/**.h", "dec/**.c",
    "enc/**.h", "enc/**.c"
  }

project "bro"
  kind "ConsoleApp"
  language "C"
  files { "tools/bro.c" }
  links { "brotli" }
