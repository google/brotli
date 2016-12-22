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

include(CMakeDependentOption)
CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON "NOT BROTLI_BUNDLED_MODE" OFF)

include(GNUInstallDirs)

# When building shared libraries it is important to set the correct rpath.
# See https://cmake.org/Wiki/CMake_RPATH_handling#Always_full_RPATH
if (BUILD_SHARED_LIBS)
  add_definitions(-DBROTLI_SHARED_COMPILATION)
  set(CMAKE_SKIP_BUILD_RPATH FALSE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_LIBDIR}" isSystemDir)
  if ("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_LIBDIR}")
  endif()
endif()

# Parse version information from common/version.h. Normally we would
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

# Version information
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
set(BROTLI_LIBRARIES_CORE brotlienc brotlidec brotlicommon)
set(BROTLI_LIBRARIES ${BROTLI_LIBRARIES_CORE} ${LIBM_LIBRARY})
mark_as_advanced(BROTLI_INCLUDE_DIRS BROTLI_LIBRARIES)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_definitions(-DOS_LINUX)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  add_definitions(-DOS_FREEBSD)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-DOS_MACOSX)
endif()

add_library(brotlicommon
  common/dictionary.c)
add_library(brotlidec
  dec/bit_reader.c
  dec/decode.c
  dec/huffman.c
  dec/state.c)
add_library(brotlienc
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

foreach(lib brotlicommon brotlidec brotlienc)
  target_link_libraries(${lib} ${LIBM_LIBRARY})
  set_property(TARGET ${lib} APPEND PROPERTY INCLUDE_DIRECTORIES ${BROTLI_INCLUDE_DIRS})
  set_target_properties(${lib} PROPERTIES
    SOVERSION "${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}"
    VERSION "${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}"
    POSITION_INDEPENDENT_CODE TRUE)
  string(TOUPPER "${lib}" LIB)
  set_target_properties (${lib} PROPERTIES DEFINE_SYMBOL "${LIB}_SHARED_COMPILATION" )

  set_property(TARGET ${lib} APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${BROTLI_INCLUDE_DIRS}")
endforeach()

target_link_libraries(brotlidec brotlicommon)
target_link_libraries(brotlienc brotlicommon)

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
  install (TARGETS bro RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

  if(WIN32)
    install(
      TARGETS ${BROTLI_LIBRARIES_CORE}
      LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
  else()
    install(TARGETS ${BROTLI_LIBRARIES_CORE}
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
    install(DIRECTORY ${BROTLI_INCLUDE_DIRS}/brotli DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
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

# Generate a pkg-config file

include(CMakeParseArguments)

function(generate_pkg_config_path outvar path)
  string(LENGTH "${path}" path_length)

  set(path_args ${ARGV})
  list(REMOVE_AT path_args 0 1)
  list(LENGTH path_args path_args_remaining)

  set("${outvar}" "${path}")

  while(path_args_remaining GREATER 1)
    list(GET path_args 0 name)
    list(GET path_args 1 value)

    get_filename_component(value_full "${value}" ABSOLUTE)
    string(LENGTH "${value}" value_length)

    if(path_length EQUAL value_length AND path STREQUAL value)
      set("${outvar}" "\${${name}}")
      break()
    elseif(path_length GREATER value_length)
      # We might be in a subdirectory of the value, but we have to be
      # careful about a prefix matching but not being a subdirectory
      # (for example, /usr/lib64 is not a subdirectory of /usr/lib).
      # We'll do this by making sure the next character is a directory
      # separator.
      string(SUBSTRING "${path}" ${value_length} 1 sep)
      if(sep STREQUAL "/")
        string(SUBSTRING "${path}" 0 ${value_length} s)
        if(s STREQUAL value)
          string(SUBSTRING "${path}" "${value_length}" -1 suffix)
          set("${outvar}" "\${${name}}${suffix}")
          break()
        endif()
      endif()
    endif()

    list(REMOVE_AT path_args 0 1)
    list(LENGTH path_args path_args_remaining)
  endwhile()

  set("${outvar}" "${${outvar}}" PARENT_SCOPE)
endfunction(generate_pkg_config_path)

function(generate_pkg_config output_file)
  set (options)
  set (oneValueArgs NAME DESCRIPTION URL VERSION PREFIX LIBDIR INCLUDEDIR)
  set (multiValueArgs DEPENDS_PRIVATE CFLAGS LIBRARIES)
  cmake_parse_arguments(GEN_PKG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  unset (options)
  unset (oneValueArgs)
  unset (multiValueArgs)

  if(NOT GEN_PKG_PREFIX)
    set(GEN_PKG_PREFIX "${CMAKE_INSTALL_PREFIX}")
  endif()

  if(NOT GEN_PKG_LIBDIR)
    set(GEN_PKG_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
  endif()
  generate_pkg_config_path(GEN_PKG_LIBDIR "${GEN_PKG_LIBDIR}"
    prefix "${GEN_PKG_PREFIX}")

  if(NOT GEN_PKG_INCLUDEDIR)
    set(GEN_PKG_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
  endif()
  generate_pkg_config_path(GEN_PKG_INCLUDEDIR "${GEN_PKG_INCLUDEDIR}"
    prefix "${GEN_PKG_PREFIX}")

  file(WRITE  "${output_file}" "prefix=${GEN_PKG_PREFIX}\n")
  file(APPEND "${output_file}" "libdir=${GEN_PKG_LIBDIR}\n")
  file(APPEND "${output_file}" "includedir=${GEN_PKG_INCLUDEDIR}\n")
  file(APPEND "${output_file}" "\n")

  if(GEN_PKG_NAME)
    file(APPEND "${output_file}" "Name: ${GEN_PKG_NAME}\n")
  else()
    file(APPEND "${output_file}" "Name: ${CMAKE_PROJECT_NAME}\n")
  endif()

  if(GEN_PKG_DESCRIPTION)
    file(APPEND "${output_file}" "Description: ${GEN_PKG_DESCRIPTION}\n")
  endif()

  if(GEN_PKG_URL)
    file(APPEND "${output_file}" "URL: ${GEN_PKG_URL}\n")
  endif()

  if(GEN_PKG_VERSION)
    file(APPEND "${output_file}" "Version: ${GEN_PKG_VERSION}\n")
  endif()

  if(GEN_PKG_DEPENDS_PRIVATE)
    file(APPEND "${output_file}" "Requires.private:")
    foreach(lib ${GEN_PKG_DEPENDS_PRIVATE})
      file(APPEND "${output_file}" " ${lib}")
    endforeach()
    file(APPEND "${output_file}" "\n")
  endif()

  if(GEN_PKG_LIBRARIES)
    set(libs)

    file(APPEND "${output_file}" "Libs: -L\${libdir}")
    foreach(lib ${GEN_PKG_LIBRARIES})
      file(APPEND "${output_file}" " -l${lib}")
    endforeach()
    file(APPEND "${output_file}" "\n")
  endif()

  file(APPEND "${output_file}" "Cflags: -I\${includedir}")
  if(GEN_PKG_CFLAGS)
    foreach(cflag ${GEN_PKG_CFLAGS})
      file(APPEND "${output_file}" " ${cflag}")
    endforeach()
  endif()
  file(APPEND "${output_file}" "\n")
endfunction(generate_pkg_config)

generate_pkg_config ("${CMAKE_CURRENT_BINARY_DIR}/libbrotlicommon.pc"
  NAME libbrotlicommon
  DESCRIPTION "Shared data used by libbrotlienc and libbrotlidec libraries"
  URL "https://github.com/google/brotli"
  VERSION "${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}"
  LIBRARIES brotlicommon)

generate_pkg_config ("${CMAKE_CURRENT_BINARY_DIR}/libbrotlidec.pc"
  NAME libbrotlidec
  DESCRIPTION "Brotli decoder library"
  VERSION "${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}"
  URL "https://github.com/google/brotli"
  DEPENDS_PRIVATE libbrotlicommon
  LIBRARIES brotlidec)

generate_pkg_config ("${CMAKE_CURRENT_BINARY_DIR}/libbrotlienc.pc"
  NAME libbrotlienc
  DESCRIPTION "Brotli encoder library"
  VERSION "${BROTLI_VERSION_MAJOR}.${BROTLI_VERSION_MINOR}.${BROTLI_VERSION_REVISION}"
  URL "https://github.com/google/brotli"
  DEPENDS_PRIVATE brotlicommon
  LIBRARIES brotlienc)

if(NOT BROTLI_BUNDLED_MODE)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbrotlicommon.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbrotlidec.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbrotlienc.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

if (ENABLE_COVERAGE STREQUAL "yes")
  SETUP_TARGET_FOR_COVERAGE(coverage test coverage)
endif ()
