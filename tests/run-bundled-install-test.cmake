cmake_minimum_required(VERSION 3.15)

# Use a fresh prefix so that previous test runs cannot leave installed files.
string(RANDOM LENGTH 8 run_id)
set(test_dir "${TEST_BINARY_DIR}/${run_id}")
file(MAKE_DIRECTORY "${test_dir}/source")
file(WRITE "${test_dir}/source/parent.txt" "parent installation\n")
file(WRITE "${test_dir}/source/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.15)
project(brotli_install_test C)
set(BROTLI_DISABLE_TESTS ON)
add_subdirectory(\"${BROTLI_SOURCE_DIR}\" brotli)
install(FILES parent.txt DESTINATION .)
")

set(options -DBROTLI_BUILD_TOOLS=${BUILD_TOOLS})
if(NOT BUNDLED_MODE STREQUAL "AUTO")
  list(APPEND options -DBROTLI_BUNDLED_MODE=${BUNDLED_MODE})
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${test_dir}/source" -B "${test_dir}/build"
    ${options}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Configuration failed: ${output}${error}")
endif()

# Bundled mode has no Brotli install targets, so no build is needed.
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${test_dir}/build"
    --prefix "${test_dir}/prefix"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Installation failed: ${output}${error}")
endif()

file(GLOB_RECURSE installed_files RELATIVE "${test_dir}/prefix"
  "${test_dir}/prefix/*")
if(NOT installed_files STREQUAL "parent.txt")
  message(FATAL_ERROR
    "Bundled Brotli installed unexpected files: ${installed_files}")
endif()
