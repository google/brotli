-- A solution contains projects, and defines the available configurations
solution "brotli"
configurations { "Release", "Debug" }
platforms { "Static", "Shared" }
targetdir "bin"
location "buildfiles"
flags "RelativeLinks"

filter "configurations:Release"
  optimize "Speed"
  flags { "StaticRuntime" }

filter "configurations:Debug"
  flags { "Symbols" }

filter { "platforms:Static" }
  kind "StaticLib"

filter { "platforms:Shared" }
  kind "SharedLib"

configuration { "gmake" }
  buildoptions { "-Wall -fno-omit-frame-pointer" }
  location "buildfiles/gmake"

configuration "linux"
  links "m"

configuration { "macosx" }
  defines { "DOS_MACOSX" }

project "brotli_common"
  language "C"
  files { "common/**.h", "common/**.c" }

project "brotli_dec"
  language "C"
  files { "dec/**.h", "dec/**.c" }
  links "brotli_common"

project "brotli_enc"
  language "C"
  files { "enc/**.h", "enc/**.c" }
  links "brotli_common"

project "bro"
  kind "ConsoleApp"
  language "C"
  files { "tools/bro.c" }
  links { "brotli_common", "brotli_dec", "brotli_enc" }
