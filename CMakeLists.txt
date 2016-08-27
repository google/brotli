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

# When building shared libraries it is important to set the correct rpath.
# See https://cmake.org/Wiki/CMake_RPATH_handling#Always_full_RPATH
if (BUILD_SHARED_LIBS)
  set(CMAKE_SKIP_BUILD_RPATH FALSE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
  if ("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
  endif()
endif()

# Parse version information from common/version.h.  Normally we would
# define these values here and write them out to configuration file(s)
# (i.e., config.h), but in this case we parse them from
# common/version.h to be less intrusive.
function(hex_to_dec HEXADECIMAL DECIMAL)
  string(TOUPPER "${HEXADECIMAL}" _tail)
  set(_decimal 0)
  string(LENGTH "${_tail}" _tail_length)
  while (_tail_length GREATER 0)
    math(EXPR _decimal "${_decimal} * 16")
    string(SUBSTRING "${_tail}" 0 1 _digit)
    string(SUBSTRING "${_tail}" 1 -1 _tail)
    if (_digit STREQUAL "A")
      math(EXPR _decimal "${_decimal} + 10")
    elseif (_digit STREQUAL "B")
      math(EXPR _decimal "${_decimal} + 11")
    elseif (_digit STREQUAL "C")
      math(EXPR _decimal "${_decimal} + 12")
    elseif (_digit STREQUAL "D")
      math(EXPR _decimal "${_decimal} + 13")
    elseif (_digit STREQUAL "E")
      math(EXPR _decimal "${_decimal} + 14")
    elseif (_digit STREQUAL "F")
      math(EXPR _decimal "${_decimal} + 15")
    else()
      math(EXPR _decimal "${_decimal} + ${_digit}")
    endif()
    string(LENGTH "${_tail}" _tail_length)
  endwhile()
  set(${DECIMAL} ${_decimal} PARENT_SCOPE)
endfunction(hex_to_dec)

file(STRINGS "common/version.h" _brotli_version_line REGEX "^#define BROTLI_VERSION (0x[0-9a-fA-F]+)$")
string(REGEX REPLACE "^#define BROTLI_VERSION 0x([0-9a-fA-F]+)$" "\\1" _brotli_version_hex "${_brotli_version_line}")
hex_to_dec("${_brotli_version_hex}" _brotli_version)
math(EXPR BROTLI_VERSION_MAJOR "${_brotli_version} >> 24")
math(EXPR BROTLI_VERSION_MINOR "(${_brotli_version} >> 12) & 4095")
math(EXPR BROTLI_VERSION_REVISION "${_brotli_version} & 4095")
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

include(CheckFunctionExists)
set(LIBM_LIBRARY)
CHECK_FUNCTION_EXISTS(log2 LOG2_RES)
if(NOT LOG2_RES)
  set(orig_req_libs "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES};m")
  CHECK_FUNCTION_EXISTS(log2 LOG2_LIBM_RES)
  if(LOG2_LIBM_RES)
    set(LIBM_LIBRARY "m")
  else()
    message(FATAL_ERROR "log2() not found")
  endif()

  set(CMAKE_REQUIRED_LIBRARIES "${orig_req_libs}")
  unset(LOG2_LIBM_RES)
  unset(orig_req_libs)
endif()
unset(LOG2_RES)

set(BROTLI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(BROTLI_LIBRARIES_CORE brotli_enc brotli_dec brotli_common)
set(BROTLI_LIBRARIES ${BROTLI_LIBRARIES_CORE} ${LIBM_LIBRARY})
mark_as_advanced(BROTLI_INCLUDE_DIRS BROTLI_LIBRARIES)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_definitions(-DOS_LINUX)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  add_definitions(-DOS_FREEBSD)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-DOS_MACOSX)
endif()

add_library(brotli_common
  common/dictionary.c)
add_library(brotli_dec
  dec/bit_reader.c
  dec/decode.c
  dec/huffman.c
  dec/state.c)
add_library(brotli_enc
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

# Older CMake versions does not understand INCLUDE_DIRECTORIES property.
include_directories(${BROTLI_INCLUDE_DIRS})

foreach(lib brotli_common brotli_dec brotli_enc)
  target_link_libraries(${lib} ${LIBM_LIBRARY})
  set_property(TARGET ${lib} APPEND PROPERTY INCLUDE_DIRECTORIES ${BROTLI_INCLUDE_DIRS})
  set_target_properties(${lib} PROPERTIES
    VERSION ${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}
    POSITION_INDEPENDENT_CODE TRUE)

  set_property(TARGET ${lib} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${BROTLI_INCLUDE_DIRS}")
endforeach()
if (BUILD_SHARED_LIBS)
  target_link_libraries(brotli_dec brotli_common)
  target_link_libraries(brotli_enc brotli_common)
endif()

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
if(NOT BROTLI_BUNDLED_MODE)
  include(GNUInstallDirs)

  install (TARGETS bro RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

  if(BUILD_SHARED_LIBS)
    if(WIN32)
      install(TARGETS ${BROTLI_LIBRARIES_CORE} RUNTIME DESTINATION bin)
    else()
      install(TARGETS ${BROTLI_LIBRARIES_CORE} LIBRARY DESTINATION lib)
    endif()
  endif()

endif()

# Tests

# If we're targeting Windows but not running on Windows, we need Wine
# to run the tests...
if(NOT BROTLI_DISABLE_TESTS)
  if(WIN32 AND NOT CMAKE_HOST_WIN32)
    find_program(BROTLI_WINE NAMES wine)

    if(NOT BROTLI_WINE)
      message(STATUS "wine not found, disabling tests")
      set(BROTLI_DISABLE_TESTS TRUE)
    endif()
  endif()
endif()

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
    get_filename_component(OUTPUT_NAME "${INPUT}" NAME)

    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}")
    set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}")

    foreach(quality 1 6 9 11)
      add_test(NAME "${BROTLI_TEST_PREFIX}roundtrip/${INPUT}/${quality}"
        COMMAND "${CMAKE_COMMAND}"
          -DBROTLI_WRAPPER=${BROTLI_WINE}
          -DBROTLI_CLI=$<TARGET_FILE:bro>
          -DQUALITY=${quality}
          -DINPUT=${INPUT_FILE}
  	  -DOUTPUT=${OUTPUT_FILE}.${quality}
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
        -DBROTLI_WRAPPER=${BROTLI_WINE}
        -DBROTLI_CLI=$<TARGET_FILE:bro>
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-compatibility-test.cmake)
  endforeach()
endif()
