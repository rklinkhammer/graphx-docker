if(NOT DEFINED CPACK_TEMPORARY_DIRECTORY OR CPACK_TEMPORARY_DIRECTORY STREQUAL "")
  message(FATAL_ERROR "CPack did not provide a temporary package directory")
endif()
if(NOT CPACK_PACKAGE_FILES)
  message(FATAL_ERROR "CPack did not provide a generated package path")
endif()

get_filename_component(GRAPHX_PACKAGE_ROOT "${CPACK_TEMPORARY_DIRECTORY}" NAME)
get_filename_component(GRAPHX_PACKAGE_PARENT "${CPACK_TEMPORARY_DIRECTORY}" DIRECTORY)
foreach(GRAPHX_PACKAGE_FILE IN LISTS CPACK_PACKAGE_FILES)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cfz "${GRAPHX_PACKAGE_FILE}"
      --format=gnutar "${GRAPHX_PACKAGE_ROOT}"
    WORKING_DIRECTORY "${GRAPHX_PACKAGE_PARENT}"
    RESULT_VARIABLE GRAPHX_ARCHIVE_RESULT
    ERROR_VARIABLE GRAPHX_ARCHIVE_ERROR)
  if(NOT GRAPHX_ARCHIVE_RESULT EQUAL 0)
    message(FATAL_ERROR
      "failed to canonicalize macOS package archive: ${GRAPHX_ARCHIVE_ERROR}")
  endif()
endforeach()
