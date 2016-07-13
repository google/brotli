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
  defines { "OS_MACOSX" }

project "brotli_common"
  kind "SharedLib"
  language "C"
  files { "common/**.h", "common/**.c", "include/brotli/types.h" }
  includedirs { "include" }

project "brotli_common_static"
  kind "StaticLib"
  targetname "brotli_common"
  language "C"
  files { "common/**.h", "common/**.c", "include/brotli/types.h" }
  includedirs { "include" }

project "brotli_dec"
  kind "SharedLib"
  language "C"
  files { "dec/**.h", "dec/**.c", "include/brotli/decode.h" }
  includedirs { "include" }
  links "brotli_common"

project "brotli_dec_static"
  kind "StaticLib"
  targetname "brotli_dec"
  language "C"
  files { "dec/**.h", "dec/**.c", "include/brotli/decode.h" }
  includedirs { "include" }
  links "brotli_common_static"

project "brotli_enc"
  kind "SharedLib"
  language "C"
  files { "enc/**.h", "enc/**.c", "include/brotli/encode.h" }
  includedirs { "include" }
  links "brotli_common"

project "brotli_enc_static"
  kind "StaticLib"
  targetname "brotli_enc"
  language "C"
  files { "enc/**.h", "enc/**.c", "include/brotli/encode.h" }
  includedirs { "include" }
  links "brotli_common_static"

project "bro"
  kind "ConsoleApp"
  language "C"
  linkoptions "-static"
  files { "tools/bro.c" }
  includedirs { "include" }
  links { "brotli_common_static", "brotli_dec_static", "brotli_enc_static" }
