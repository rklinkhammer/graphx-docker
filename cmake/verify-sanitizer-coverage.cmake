if(NOT DEFINED GRAPHX_COMPILE_COMMANDS OR NOT EXISTS "${GRAPHX_COMPILE_COMMANDS}")
  message(FATAL_ERROR "sanitizer coverage requires an existing compile_commands.json")
endif()
if(NOT DEFINED GRAPHX_SOURCE_ROOT)
  message(FATAL_ERROR "sanitizer coverage requires GRAPHX_SOURCE_ROOT")
endif()

file(READ "${GRAPHX_COMPILE_COMMANDS}" compile_commands)
string(JSON command_count LENGTH "${compile_commands}")
if(command_count EQUAL 0)
  message(FATAL_ERROR "sanitizer coverage found no compile commands")
endif()

set(graphx_source_count 0)
set(graphx_application_count 0)
set(graphx_test_count 0)
set(graphx_fuzzer_count 0)
set(unsanitized_sources)
math(EXPR last_command "${command_count} - 1")
foreach(index RANGE 0 ${last_command})
  string(JSON source_file GET "${compile_commands}" ${index} file)
  file(RELATIVE_PATH relative_source "${GRAPHX_SOURCE_ROOT}" "${source_file}")
  set(source_kind "")
  if(relative_source MATCHES "^src/.*\\.cpp$")
    set(source_kind source)
    math(EXPR graphx_source_count "${graphx_source_count} + 1")
  elseif(relative_source MATCHES "^apps/.*\\.cpp$")
    set(source_kind application)
    math(EXPR graphx_application_count "${graphx_application_count} + 1")
  elseif(relative_source MATCHES "^tests/.*\\.cpp$")
    set(source_kind test)
    math(EXPR graphx_test_count "${graphx_test_count} + 1")
  elseif(relative_source MATCHES "^fuzz/.*\\.cpp$")
    set(source_kind fuzzer)
    math(EXPR graphx_fuzzer_count "${graphx_fuzzer_count} + 1")
  endif()

  if(source_kind)
    string(JSON compile_command GET "${compile_commands}" ${index} command)
    if(NOT compile_command MATCHES "(^| )-fsanitize=address,undefined( |$)" OR
       NOT compile_command MATCHES "(^| )-fno-omit-frame-pointer( |$)")
      list(APPEND unsanitized_sources "${source_file}")
    endif()
  endif()
endforeach()

if(NOT DEFINED GRAPHX_REQUIRE_TEST_SOURCES)
  set(GRAPHX_REQUIRE_TEST_SOURCES ON)
endif()
if(NOT DEFINED GRAPHX_REQUIRE_FUZZ_SOURCES)
  set(GRAPHX_REQUIRE_FUZZ_SOURCES OFF)
endif()
if(graphx_source_count EQUAL 0 OR graphx_application_count EQUAL 0 OR
   (GRAPHX_REQUIRE_TEST_SOURCES AND graphx_test_count EQUAL 0) OR
   (GRAPHX_REQUIRE_FUZZ_SOURCES AND graphx_fuzzer_count EQUAL 0))
  message(FATAL_ERROR
    "sanitizer coverage did not find all required GraphX source classes: "
    "src=${graphx_source_count}, apps=${graphx_application_count}, "
    "tests=${graphx_test_count}, fuzz=${graphx_fuzzer_count}")
endif()
if(unsanitized_sources)
  list(JOIN unsanitized_sources "\n  " formatted_sources)
  message(FATAL_ERROR "GraphX-owned sources missing ASan/UBSan instrumentation:\n  ${formatted_sources}")
endif()

message(STATUS
  "sanitizer coverage passed for ${graphx_source_count} library, "
  "${graphx_application_count} application, ${graphx_test_count} test, and "
  "${graphx_fuzzer_count} fuzzer translation units")
