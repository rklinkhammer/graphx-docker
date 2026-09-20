include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(ENABLE_PYTHON3 OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_APPS OFF CACHE BOOL "" FORCE)
set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(soapysdr
  URL https://codeload.github.com/pothosware/SoapySDR/tar.gz/1cf5a539a21414ff509ff7d0eedfc5fa8edb90c6
  URL_HASH SHA256=e680e9a6f741764b9c31b0520e577d91ad4197968d50c78db601c8a59e9ba72a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(soapysdr)
# Scope the dependency's CTest switch; do not disable GraphX tests.
function(graphx_vrt_dependency)
  set(BUILD_TESTING OFF)
  set(VITA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(VITA_BUILD_FUZZING OFF CACHE BOOL "" FORCE)
  set(GRAPHX_VRT_REPOSITORY "https://github.com/rklinkhammer/vrt_framework.git"
    CACHE STRING "VRT repository (local Git mirror permitted; immutable revision enforced)")
  FetchContent_Declare(vrt_framework
    GIT_REPOSITORY "${GRAPHX_VRT_REPOSITORY}"
    GIT_TAG dbe85d37155145842da60367af1c4beef8801b0c
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(vrt_framework)
  find_package(Git REQUIRED)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${vrt_framework_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE vrt_revision OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${vrt_framework_SOURCE_DIR}" status --porcelain
    OUTPUT_VARIABLE vrt_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT vrt_revision STREQUAL "dbe85d37155145842da60367af1c4beef8801b0c" OR NOT vrt_changes STREQUAL "")
    message(FATAL_ERROR "VRT sources must be clean at the pinned revision")
  endif()
  set(vrt_framework_SOURCE_DIR "${vrt_framework_SOURCE_DIR}" PARENT_SCOPE)
endfunction()
graphx_vrt_dependency()
add_library(graphx-vita-radio STATIC
  src/vita/virtual_device.cpp src/vita/host.cpp src/vita/radio.cpp
  src/vita/processing.cpp src/vita/processing_app.cpp)
target_include_directories(graphx-vita-radio PUBLIC include)
target_link_libraries(graphx-vita-radio PUBLIC SoapySDR vita::core graphx)
graphx_enable_analysis(graphx-vita-radio)
graphx_enable_sanitizers(graphx-vita-radio)

add_executable(graphx-vita-radio-app apps/vita-radio/main.cpp)
set_target_properties(graphx-vita-radio-app PROPERTIES OUTPUT_NAME graphx-vita-radio)
target_link_libraries(graphx-vita-radio-app PRIVATE graphx-vita-radio)
graphx_enable_analysis(graphx-vita-radio-app)
graphx_enable_sanitizers(graphx-vita-radio-app)
if(GRAPHX_BUILD_TESTS)
  add_executable(graphx-vita-transport-test tests/vita_transport_client.cpp)
  target_link_libraries(graphx-vita-transport-test PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-transport-test)
  graphx_enable_sanitizers(graphx-vita-transport-test)
  add_test(NAME graphx-vita-transport COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_vita_transport.py ${CMAKE_BINARY_DIR})
  set_tests_properties(graphx-vita-transport PROPERTIES TIMEOUT 30)
  add_executable(graphx-vita-runtime-test tests/test_vita_runtime.cpp)
  target_link_libraries(graphx-vita-runtime-test PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-runtime-test)
  graphx_enable_sanitizers(graphx-vita-runtime-test)
  add_test(NAME graphx-vita-runtime COMMAND graphx-vita-runtime-test)
  add_test(NAME graphx-vita-controller COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_vita_controller.py ${CMAKE_BINARY_DIR})
  set_tests_properties(graphx-vita-controller PROPERTIES TIMEOUT 30)
  add_executable(graphx-vita-controller-test tests/vita_controller_client.cpp)
  target_link_libraries(graphx-vita-controller-test PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-controller-test)
  graphx_enable_sanitizers(graphx-vita-controller-test)
  add_executable(graphx-vita-start-epoch tests/repro_vita_start_epoch.cpp)
  target_link_libraries(graphx-vita-start-epoch PRIVATE vita::core)
  graphx_enable_sanitizers(graphx-vita-start-epoch)
  add_test(NAME graphx-vita-start-epoch COMMAND graphx-vita-start-epoch)
  add_test(NAME graphx-vita-start-on-time COMMAND graphx-vita-start-epoch --on-time)
  add_executable(graphx-vita-device-test tests/test_vita_device.cpp)
  target_link_libraries(graphx-vita-device-test PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-device-test)
  graphx_enable_sanitizers(graphx-vita-device-test)
  add_test(NAME graphx-vita-device COMMAND graphx-vita-device-test)
  add_test(NAME graphx-vita-standalone COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_vita_radio.py ${CMAKE_BINARY_DIR})
  set_tests_properties(graphx-vita-standalone PROPERTIES TIMEOUT 60)
endif()
configure_file(config/vita/dependencies.json generated/vita-dependencies.json COPYONLY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/vita-licenses")
configure_file("${soapysdr_SOURCE_DIR}/LICENSE_1_0.txt" generated/vita-licenses/SoapySDR.txt COPYONLY)
configure_file("${vrt_framework_SOURCE_DIR}/LICENSE" generated/vita-licenses/vrt_framework.txt COPYONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/config/vita/dependencies.json"
  "${CMAKE_SOURCE_DIR}/scripts/vita/dependency_sbom.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/vita/dependency_sbom.py"
  "${CMAKE_SOURCE_DIR}/config/vita/dependencies.json"
  "${CMAKE_BINARY_DIR}/generated/vita-dependencies.spdx.json" COMMAND_ERROR_IS_FATAL ANY)

foreach(app processor detector)
  add_executable(graphx-vita-${app} apps/vita-${app}/main.cpp)
  target_link_libraries(graphx-vita-${app} PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-${app})
  graphx_enable_sanitizers(graphx-vita-${app})
endforeach()
if(GRAPHX_BUILD_TESTS)
  add_test(NAME graphx-vita-processing COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_vita_processing.py ${CMAKE_BINARY_DIR})
  set_tests_properties(graphx-vita-processing PROPERTIES TIMEOUT 30)

  add_executable(graphx-vita-processing-test tests/test_vita_processing.cpp)
  target_link_libraries(graphx-vita-processing-test PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-processing-test)
  graphx_enable_sanitizers(graphx-vita-processing-test)
  add_test(NAME graphx-vita-processing-unit COMMAND graphx-vita-processing-test)

  add_test(NAME graphx-vita-power-wire COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_vita_power_wire.py ${CMAKE_BINARY_DIR})

  foreach(mode boundary missing wrong-source)
    add_test(NAME graphx-vita-processing-${mode} COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tests/test_vita_processing.py ${CMAKE_BINARY_DIR} --${mode})
    set_tests_properties(graphx-vita-processing-${mode} PROPERTIES TIMEOUT 30)
  endforeach()
endif()

if(GRAPHX_BUILD_FUZZERS)
  # Compile the power codec itself with coverage, not only the driver.
  add_executable(graphx-vita-power-fuzz fuzz/vita_power_fuzz.cpp src/vita/processing.cpp)
  target_link_libraries(graphx-vita-power-fuzz PRIVATE graphx-vita-radio)
  graphx_enable_analysis(graphx-vita-power-fuzz)
  graphx_enable_sanitizers(graphx-vita-power-fuzz)
  target_compile_options(graphx-vita-power-fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(graphx-vita-power-fuzz PRIVATE -fsanitize=fuzzer)
endif()
