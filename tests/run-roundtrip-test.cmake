get_filename_component(OUTPUT_NAME "${INPUT}" NAME)

execute_process(
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND ${BROTLI_CLI} -f -q ${QUALITY} -i "${INPUT}" -o "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.bro"
  RESULT_VARIABLE result)
if(result)
  message(FATAL_ERROR "Compression failed")
endif()

execute_process(
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND ${BROTLI_CLI} -f -d -i "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.bro" -o "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.unbro"
  RESULT_VARIABLE result)
if(result)
  message(FATAL_ERROR "Decompression failed")
endif()

function(test_file_equality f1 f2)
  if(NOT CMAKE_VERSION VERSION_LESS 2.8.7)
    file(SHA512 "${f1}" f1_cs)
    file(SHA512 "${f2}" f2_cs)
    if(NOT "${f1_cs}" STREQUAL "${f2_cs}")
      message(FATAL_ERROR "Files do not match")
    endif()
  else()
    file(READ "${f1}" f1_contents)
    file(READ "${f2}" f2_contents)
    if(NOT "${f1_contents}" STREQUAL "${f2_contents}")
      message(FATAL_ERROR "Files do not match")
    endif()
  endif()
endfunction()

test_file_equality("${INPUT}" "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.unbro")
