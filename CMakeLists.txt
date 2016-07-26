# Ubuntu 12.04 LTS has CMake 2.8.7, and is an important target since
# several CI services, such as Travis and Drone, use it.  Solaris 11
# has 2.8.6, and it's not difficult to support if you already have to
# support 2.8.7.
cmake_minimum_required(VERSION 2.8.6)

project(brotli)

# If Brotli is being bundled in another project, we don't want to
# install anything.  However, we want to let people override this, so
# we'll use the BROTLI_BUNDLED_MODE variable to let them do that; just
# set it to OFF in your project before you add_subdirectory(brotli).
get_directory_property(BROTLI_PARENT_DIRECTORY PARENT_DIRECTORY)
if(BROTLI_BUNDLED_MODE STREQUAL "")
  # Bundled mode hasn't been set one way or the other, set the default
  # depending on whether or not we are the top-level project.
  if(BROTLI_PARENT_DIRECTORY)
    set(BROTLI_BUNDLED_MODE OFF)
  else()
    set(BROTLI_BUNDLED_MODE ON)
  endif()
endif()
mark_as_advanced(BROTLI_BUNDLED_MODE)

# Parse version information from tools/version.h.  Normally we would
# define these values here and write them out to configuration file(s)
# (i.e., config.h), but in this case we parse them from
# tools/version.h to be less intrusive.
file(STRINGS "tools/version.h" BROTLI_VERSION REGEX "^#define BROTLI_VERSION \"+([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
string(REGEX REPLACE "^#define BROTLI_VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"$" "\\1" BROTLI_VERSION_MAJOR "${BROTLI_VERSION}")
string(REGEX REPLACE "^#define BROTLI_VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"$" "\\2" BROTLI_VERSION_MINOR "${BROTLI_VERSION}")
string(REGEX REPLACE "^#define BROTLI_VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"$" "\\3" BROTLI_VERSION_REVISION "${BROTLI_VERSION}")
mark_as_advanced(BROTLI_VERSION_MAJOR BROTLI_VERSION_MINOR BROTLI_VERSION_REVISION)

if (ENABLE_SANITIZER)
  set(CMAKE_C_FLAGS " ${CMAKE_C_FLAGS} -fsanitize=${ENABLE_SANITIZER}")
  set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} -fsanitize=${ENABLE_SANITIZER}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${ENABLE_SANITIZER}")

  # By default, brotli depends on undefined behavior, but setting
  # BROTLI_BUILD_PORTABLE should result in a build which does not.
  if(ENABLE_SANITIZER STREQUAL "undefined")
    add_definitions(-DBROTLI_BUILD_PORTABLE)
  endif()
endif ()

set(BROTLI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
set(BROTLI_LIBRARIES brotli_enc brotli_dec brotli_common m)
mark_as_advanced(BROTLI_INCLUDE_DIRS BROTLI_LIBRARIES)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_definitions(-DOS_LINUX)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  add_definitions(-DOS_FREEBSD)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-DOS_MACOSX)
endif()

add_library(brotli_common STATIC
  common/dictionary.c)
add_library(brotli_dec STATIC
  dec/bit_reader.c
  dec/decode.c
  dec/huffman.c
  dec/state.c)
add_library(brotli_enc STATIC
  enc/backward_references.c
  enc/bit_cost.c
  enc/block_splitter.c
  enc/brotli_bit_stream.c
  enc/cluster.c
  enc/compress_fragment.c
  enc/compress_fragment_two_pass.c
  enc/encode.c
  enc/entropy_encode.c
  enc/histogram.c
  enc/literal_cost.c
  enc/memory.c
  enc/metablock.c
  enc/static_dict.c
  enc/utf8_util.c)

foreach(lib brotli_common brotli_dec brotli_enc)
  target_link_libraries(${lib} m)
  set_property(TARGET ${lib} APPEND PROPERTY INCLUDE_DIRECTORIES ${BROTLI_INCLUDE_DIRS})
  set_target_properties(${lib} PROPERTIES
    VERSION ${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}
    POSITION_INDEPENDENT_CODE TRUE)

  set_property(TARGET ${lib} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${BROTLI_INCLUDE_DIRS}")
endforeach()

# For projects stuck on older versions of CMake, this will set the
# BROTLI_INCLUDE_DIRS and BROTLI_LIBRARIES variables so they still
# have a relatively easy way to use Brotli:
#
#   include_directories(${BROTLI_INCLUDE_DIRS})
#   target_link_libraries(foo ${BROTLI_LIBRARIES})
if(BROTLI_PARENT_DIRECTORY)
  set(BROTLI_INCLUDE_DIRS "${BROTLI_INCLUDE_DIRS}" PARENT_SCOPE)
  set(BROTLI_LIBRARIES "${BROTLI_LIBRARIES}" PARENT_SCOPE)
endif()

# Build the bro executable
add_executable(bro tools/bro.c)
target_link_libraries(bro ${BROTLI_LIBRARIES})

# Installation
if(NOT BROTLI_BUNDLE_MODE)
  include(GNUInstallDirs)

  install (TARGETS bro RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

# Tests
if(NOT BROTLI_DISABLE_TESTS)
  include(CTest)
  enable_testing()

  set(ROUNDTRIP_INPUTS
    tests/testdata/alice29.txt
    tests/testdata/asyoulik.txt
    tests/testdata/lcet10.txt
    tests/testdata/plrabn12.txt
    enc/encode.c
    common/dictionary.h
    dec/decode.c)

  foreach(INPUT ${ROUNDTRIP_INPUTS})
    foreach(quality 1 6 9 11)
      add_test(NAME "${BROTLI_TEST_PREFIX}roundtrip/${INPUT}/${quality}"
        COMMAND "${CMAKE_COMMAND}"
          -DBROTLI_CLI=$<TARGET_FILE:bro>
          -DQUALITY=${quality}
          -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-roundtrip-test.cmake)
    endforeach()
  endforeach()

  file(GLOB_RECURSE
    COMPATIBILITY_INPUTS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    tests/testdata/*.compressed*)

  foreach(INPUT ${COMPATIBILITY_INPUTS})
    add_test(NAME "${BROTLI_TEST_PREFIX}compatibility/${INPUT}"
      COMMAND "${CMAKE_COMMAND}"
        -DBROTLI_CLI=$<TARGET_FILE:bro>
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-compatibility-test.cmake)
  endforeach()
endif()
