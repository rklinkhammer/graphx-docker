cmake_minimum_required(VERSION 3.25)
file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GRAPHX_VERSION_FILE_SIZE)
if(GRAPHX_VERSION_FILE_SIZE GREATER 64)
  message(FATAL_ERROR "VERSION must be no larger than 64 bytes")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GRAPHX_VERSION)
string(STRIP "${GRAPHX_VERSION}" GRAPHX_VERSION)
if(NOT GRAPHX_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR "VERSION must contain one canonical MAJOR.MINOR.PATCH release version")
endif()
project(graphx VERSION "${GRAPHX_VERSION}" LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(GRAPHX_BUILD_TESTS "Build GraphX tests" ON)
option(GRAPHX_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(GRAPHX_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GRAPHX_ENABLE_CLANG_TIDY "Run clang-tidy while compiling GraphX targets" OFF)
option(GRAPHX_ENABLE_CPPCHECK "Run cppcheck while compiling GraphX targets" OFF)
option(GRAPHX_FORCE_BUNDLED_YAML_CPP "Use the pinned yaml-cpp source archive" OFF)
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ language standard (20 or 23)")
set_property(CACHE CMAKE_CXX_STANDARD PROPERTY STRINGS 20 23)
if(NOT CMAKE_CXX_STANDARD STREQUAL "20" AND NOT CMAKE_CXX_STANDARD STREQUAL "23")
  message(FATAL_ERROR "GraphX supports CMAKE_CXX_STANDARD=20 or 23")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(GRAPHX_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  message(FATAL_ERROR "GRAPHX_ENABLE_SANITIZERS requires Clang, AppleClang, or GCC")
endif()
if(GRAPHX_BUILD_FUZZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
  message(FATAL_ERROR "GRAPHX_BUILD_FUZZERS requires Clang or AppleClang libFuzzer support")
endif()
if(GRAPHX_BUILD_FUZZERS AND NOT GRAPHX_ENABLE_SANITIZERS)
  message(FATAL_ERROR "GRAPHX_BUILD_FUZZERS requires GRAPHX_ENABLE_SANITIZERS=ON")
endif()

if(GRAPHX_ENABLE_CLANG_TIDY)
  find_program(GRAPHX_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()
if(GRAPHX_ENABLE_CPPCHECK)
  find_program(GRAPHX_CPPCHECK_EXECUTABLE NAMES cppcheck REQUIRED)
endif()

if(GRAPHX_ENABLE_SANITIZERS)
  # The sanitizer coverage CTest audits the generated commands for every
  # GraphX-owned translation unit. Keep the database available even when a
  # caller did not explicitly request it.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

function(graphx_enable_analysis target)
  if(GRAPHX_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
      "${GRAPHX_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy;--header-filter=^${CMAKE_SOURCE_DIR}/(include|src|apps|tests|fuzz)/")
  endif()
  if(GRAPHX_ENABLE_CPPCHECK)
    set_property(TARGET ${target} PROPERTY CXX_CPPCHECK
      "${GRAPHX_CPPCHECK_EXECUTABLE};--enable=warning,portability;--inline-suppr;--suppress=missingIncludeSystem;--suppress=*:*_deps/*;--error-exitcode=1")
  endif()
endfunction()

function(graphx_enable_sanitizers target)
  if(GRAPHX_ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE
      -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(${target} PRIVATE
      -fno-omit-frame-pointer -fsanitize=address,undefined)
  endif()
endfunction()

find_package(Threads REQUIRED)
find_package(OpenSSL 3 REQUIRED)
if(NOT GRAPHX_FORCE_BUNDLED_YAML_CPP)
  find_package(yaml-cpp 0.9 QUIET CONFIG)
endif()
set(GRAPHX_BUNDLED_YAML_CPP OFF)
if(NOT yaml-cpp_FOUND)
  set(GRAPHX_BUNDLED_YAML_CPP ON)
  include(FetchContent)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-0.9.0.tar.gz
    URL_HASH SHA256=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(yaml-cpp)
endif()

add_library(graphx
  src/capture.cpp
  src/config.cpp
  src/envelope.cpp
  src/framing.cpp
  src/in_process_transport.cpp
  src/infra.cpp
  src/network.cpp
  src/observability.cpp
  src/shared_memory_transport.cpp
  src/tcp_transport.cpp
  src/udp_transport.cpp
  src/unix_domain_socket_transport.cpp
  src/transport_factory.cpp
)
configure_file(include/graphx/version.hpp.in generated/graphx/version.hpp @ONLY)
configure_file(cmake/GraphXBuildDependencies.json.in
  generated/graphx-build-dependencies.json @ONLY)
target_include_directories(graphx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(graphx PUBLIC cxx_std_20)
target_link_libraries(graphx PUBLIC Threads::Threads yaml-cpp::yaml-cpp OpenSSL::SSL OpenSSL::Crypto)
graphx_enable_analysis(graphx)
graphx_enable_sanitizers(graphx)
target_compile_options(graphx PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(GRAPHX_BUILD_FUZZERS)
  target_compile_options(graphx PRIVATE -fsanitize=fuzzer-no-link)
endif()

foreach(app generator transform sink)
  add_executable(graphx-${app} apps/${app}/main.cpp)
  target_link_libraries(graphx-${app} PRIVATE graphx)
  graphx_enable_analysis(graphx-${app})
  graphx_enable_sanitizers(graphx-${app})
endforeach()

foreach(app udp_publisher udp_subscriber)
  string(REPLACE "_" "-" app_name "${app}")
  add_executable(graphx-${app_name} apps/${app}/main.cpp)
  target_link_libraries(graphx-${app_name} PRIVATE graphx)
  graphx_enable_analysis(graphx-${app_name})
  graphx_enable_sanitizers(graphx-${app_name})
endforeach()

add_executable(graphx-cli apps/cli/main.cpp)
set_target_properties(graphx-cli PROPERTIES OUTPUT_NAME graphx)
target_link_libraries(graphx-cli PRIVATE graphx)
graphx_enable_analysis(graphx-cli)
graphx_enable_sanitizers(graphx-cli)

install(TARGETS graphx EXPORT GraphXTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(GRAPHX_BUNDLED_YAML_CPP)
  install(TARGETS yaml-cpp EXPORT GraphXTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES "${yaml-cpp_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/graphx/licenses
    RENAME yaml-cpp-LICENSE COMPONENT documentation)
endif()
install(TARGETS graphx-cli graphx-generator graphx-transform graphx-sink
  graphx-udp-publisher graphx-udp-subscriber
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
install(DIRECTORY include/graphx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT development FILES_MATCHING PATTERN "*.hpp")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/graphx/version.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/graphx COMPONENT development)
install(EXPORT GraphXTargets FILE GraphXTargets.cmake NAMESPACE GraphX::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphX COMPONENT development)
configure_package_config_file(cmake/GraphXConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/GraphXConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphX)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/GraphXConfigVersion.cmake"
  VERSION "${GRAPHX_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/GraphXConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/GraphXConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphX COMPONENT development)
install(FILES config/schema/graphx.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/schema COMPONENT runtime)
install(FILES graphx.yaml DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx COMPONENT runtime)
install(PROGRAMS tools/graphx-extcap
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/graphx COMPONENT tools)
install(FILES wireshark/graphx.lua wireshark/README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/wireshark COMPONENT tools)
install(FILES LICENSE THIRD_PARTY.md README.md VERSION CHANGELOG.md SECURITY.md SUPPORT.md
  CONTRIBUTING.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/graphx COMPONENT documentation)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/graphx/docs
  COMPONENT documentation FILES_MATCHING PATTERN "*.md")
if(APPLE)
  set(CPACK_POST_BUILD_SCRIPTS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GraphXCpackCanonicalize.cmake")
endif()

set(CPACK_PACKAGE_NAME graphx)
set(CPACK_PACKAGE_VENDOR "GraphX contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Transport-neutral processing graph runtime and tools")
set(CPACK_PACKAGE_VERSION "${GRAPHX_VERSION}")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" GRAPHX_PACKAGE_SYSTEM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" GRAPHX_PACKAGE_PROCESSOR)
if(GRAPHX_PACKAGE_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(GRAPHX_PACKAGE_PROCESSOR aarch64)
elseif(GRAPHX_PACKAGE_PROCESSOR MATCHES "^(amd64|x86_64)$")
  set(GRAPHX_PACKAGE_PROCESSOR x86_64)
endif()
set(CPACK_PACKAGE_FILE_NAME
  "graphx-${GRAPHX_VERSION}-${GRAPHX_PACKAGE_SYSTEM}-${GRAPHX_PACKAGE_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR TGZ)
set(CPACK_ARCHIVE_THREADS 1)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "graphx-${GRAPHX_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
  "/\\.git/" "/build[^/]*/" "/web/node_modules/" "/apps/telemetry/node_modules/"
  "/outputs/" "/captures/" "/\\.DS_Store$")
include(CPack)

if(GRAPHX_BUILD_TESTS)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(graphx-tests tests/test_main.cpp)
  target_link_libraries(graphx-tests PRIVATE graphx)
  graphx_enable_analysis(graphx-tests)
  graphx_enable_sanitizers(graphx-tests)
  target_compile_definitions(graphx-tests PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME graphx-tests COMMAND graphx-tests)
  add_executable(graphx-config-tests tests/test_config.cpp)
  target_link_libraries(graphx-config-tests PRIVATE graphx)
  graphx_enable_analysis(graphx-config-tests)
  graphx_enable_sanitizers(graphx-config-tests)
  target_compile_definitions(graphx-config-tests PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME graphx-config-tests COMMAND graphx-config-tests)
  set_tests_properties(graphx-config-tests PROPERTIES TIMEOUT 30)
  add_executable(graphx-tls-smoke tests/tls_smoke.cpp)
  target_link_libraries(graphx-tls-smoke PRIVATE graphx)
  graphx_enable_analysis(graphx-tls-smoke)
  graphx_enable_sanitizers(graphx-tls-smoke)
  add_test(NAME graphx-tls-security
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test-tls.sh $<TARGET_FILE:graphx-tls-smoke>)
  set_tests_properties(graphx-tls-security PROPERTIES TIMEOUT 15)
  add_executable(graphx-capture-fixture tests/capture_fixture.cpp)
  target_link_libraries(graphx-capture-fixture PRIVATE graphx)
  graphx_enable_analysis(graphx-capture-fixture)
  graphx_enable_sanitizers(graphx-capture-fixture)
  add_executable(graphx-capture-runtime-config tests/capture_runtime_config.cpp)
  target_link_libraries(graphx-capture-runtime-config PRIVATE graphx)
  graphx_enable_analysis(graphx-capture-runtime-config)
  graphx_enable_sanitizers(graphx-capture-runtime-config)
  add_test(NAME graphx-capture-runtime-config COMMAND graphx-capture-runtime-config)
  add_test(NAME graphx-extcap COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_extcap.py ${CMAKE_SOURCE_DIR}/tools/graphx-extcap
    $<TARGET_FILE:graphx-capture-fixture>)
  find_program(GRAPHX_TSHARK_EXECUTABLE tshark)
  if(GRAPHX_TSHARK_EXECUTABLE)
    add_test(NAME graphx-wireshark-dissector COMMAND bash
      ${CMAKE_SOURCE_DIR}/scripts/test-wireshark.sh ${GRAPHX_TSHARK_EXECUTABLE}
      $<TARGET_FILE:graphx-capture-fixture> ${CMAKE_SOURCE_DIR}/wireshark/graphx.lua)
  else()
    message(STATUS "tshark not found; graphx-wireshark-dissector runtime test is unavailable")
  endif()
  add_test(NAME graphx-config-cli
    COMMAND graphx-cli validate ${CMAKE_SOURCE_DIR}/graphx.yaml)
  add_test(NAME graphx-version COMMAND graphx-cli --version)
  set_tests_properties(graphx-version PROPERTIES
    PASS_REGULAR_EXPRESSION "^graphx ${GRAPHX_VERSION}")
  add_test(NAME graphx-release-contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_release.py ${CMAKE_SOURCE_DIR} $<TARGET_FILE:graphx-cli>)
  add_test(NAME graphx-package COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_package.py ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
  set_tests_properties(graphx-release-contract PROPERTIES TIMEOUT 30)
  set_tests_properties(graphx-package PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  if(GRAPHX_ENABLE_SANITIZERS)
    # A sanitizer-instrumented static archive requires the external consumer to
    # link the same sanitizer runtimes. Release archives are never built this
    # way, so keep this packaging-only test in the ordinary Release/Debug gates.
    set_tests_properties(graphx-package PROPERTIES DISABLED TRUE)
  endif()
  set(graphx_config_cli_tests graphx-config-cli)
  foreach(example mixed-network macvlan ipvlan-l2 ipvlan-l3 shared-memory udp-unicast
                  udp-broadcast udp-multicast)
    add_test(NAME graphx-config-${example}
      COMMAND graphx-cli validate ${CMAKE_SOURCE_DIR}/examples/${example}/graphx.yaml)
    list(APPEND graphx_config_cli_tests graphx-config-${example})
  endforeach()
  # Configuration tests must validate the checked-in files exactly as written.
  # Do not let deployment overrides exported by an earlier manual demo leak into
  # CTest and invalidate topologies that intentionally use another transport.
  set_tests_properties(${graphx_config_cli_tests}
    PROPERTIES ENVIRONMENT "GRAPHX_OVERRIDES=")

  add_test(NAME graphx-protocol-boundaries
    COMMAND graphx-tests --filter "protocol exhaustive boundaries")
  add_test(NAME graphx-mixed-version-transport
    COMMAND graphx-tests --filter "TCP mixed-version interoperability")
  add_test(NAME graphx-transport-lifecycle-stress
    COMMAND graphx-tests --filter "transport lifecycle stress")
  set_tests_properties(graphx-transport-lifecycle-stress PROPERTIES TIMEOUT 30)
  add_test(NAME graphx-udp-transport
    COMMAND graphx-tests --filter "UDP" --repeat 3)
  set_tests_properties(graphx-udp-transport PROPERTIES TIMEOUT 30)
  add_test(NAME graphx-udp-unicast-example
    COMMAND ${CMAKE_SOURCE_DIR}/examples/udp-unicast/run.sh)
  add_test(NAME graphx-udp-multicast-example
    COMMAND ${CMAKE_SOURCE_DIR}/examples/udp-multicast/run.sh)
  set_tests_properties(graphx-udp-unicast-example graphx-udp-multicast-example
    PROPERTIES TIMEOUT 15 ENVIRONMENT "GRAPHX_BUILD_DIR=${CMAKE_BINARY_DIR}")
  if(GRAPHX_ENABLE_SANITIZERS)
    add_test(NAME graphx-sanitizer-coverage
      COMMAND ${CMAKE_COMMAND}
        -DGRAPHX_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
        -DGRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/verify-sanitizer-coverage.cmake)
  endif()
endif()

if(GRAPHX_BUILD_FUZZERS)
  foreach(fuzzer envelope frame)
    add_executable(graphx-${fuzzer}-fuzz fuzz/${fuzzer}_fuzz.cpp)
    target_link_libraries(graphx-${fuzzer}-fuzz PRIVATE graphx)
    graphx_enable_analysis(graphx-${fuzzer}-fuzz)
    graphx_enable_sanitizers(graphx-${fuzzer}-fuzz)
    target_compile_options(graphx-${fuzzer}-fuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(graphx-${fuzzer}-fuzz PRIVATE -fsanitize=fuzzer)
  endforeach()
endif()
