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
option(GRAPHX_BUILD_VITA_RADIO "Build standalone P1 VITA radio" OFF)
option(GRAPHX_ENABLE_LINUX_OVS_TESTS "Enable privileged Linux system-OVS lifecycle tests" OFF)
option(GRAPHX_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(GRAPHX_ENABLE_SANITIZERS "Enable the configured compiler sanitizers" OFF)
set(GRAPHX_SANITIZERS "address,undefined" CACHE STRING
  "Comma-separated compiler sanitizers used when GRAPHX_ENABLE_SANITIZERS is ON")
option(GRAPHX_ENABLE_CLANG_TIDY "Run clang-tidy while compiling GraphX targets" OFF)
option(GRAPHX_ENABLE_CPPCHECK "Run cppcheck while compiling GraphX targets" OFF)
option(GRAPHX_FORCE_BUNDLED_YAML_CPP "Use the pinned yaml-cpp source archive" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(GRAPHX_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
  message(FATAL_ERROR "GRAPHX_ENABLE_SANITIZERS requires Clang, AppleClang, or GCC")
endif()
if(GRAPHX_ENABLE_SANITIZERS AND GRAPHX_SANITIZERS STREQUAL "")
  message(FATAL_ERROR "GRAPHX_SANITIZERS must not be empty when sanitizers are enabled")
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
    # Keep instrumented configuration and lifecycle matrices within their
    # deadlines on libstdc++ while retaining debug symbols and frame pointers.
    target_compile_options(${target} PRIVATE
      -O1 -fno-omit-frame-pointer "-fsanitize=${GRAPHX_SANITIZERS}")
    target_link_options(${target} PRIVATE
      -fno-omit-frame-pointer "-fsanitize=${GRAPHX_SANITIZERS}")
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
  src/config_document.cpp
  src/config_v3.cpp
  src/scenario_config.cpp
  src/infra/scenario_execution.cpp
  src/resolved_transport.cpp
  src/resolved_network.cpp
  src/node_settings.cpp
  src/sample_application.cpp
  src/diagnostic_application.cpp
  src/packet_application.cpp
  src/config.cpp
  src/compile.cpp
  src/example_cli.cpp
  src/execution.cpp
  src/infra/process_resources.cpp
  src/infra/qemu_resources.cpp
  src/infra/compose_execution.cpp
  src/compile_output.cpp
  src/envelope.cpp
  src/framing.cpp
  src/in_process_transport.cpp
  src/infra/capture_resources.cpp
  src/infra/capture_handoff.cpp
  src/infra/node_console.cpp
  src/infra/command_runner.cpp
  src/infra/endpoint_resources.cpp
  src/infra/management_policy.cpp
  src/infra/fault_resources.cpp
  src/infra/lifecycle_coordinator.cpp
  src/infra/namespace_resources.cpp
  src/infra/ovs_resources.cpp
  src/infra/ownership_lock.cpp
  src/infra/ownership_state.cpp
  src/infra.cpp
  src/network.cpp
  src/normalized_config.cpp
  src/observability.cpp
  src/ownership.cpp
  src/shared_memory_transport.cpp
  src/tcp_transport.cpp
  src/udp_transport.cpp
  src/unix_domain_socket_transport.cpp
  src/transport_factory.cpp
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/schema/graphx.schema.json" GRAPHX_AUTHORED_SCHEMA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/schema/node-type.schema.json" GRAPHX_TYPE_SCHEMA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/schema/normalized-graph.schema.json" GRAPHX_NORMALIZED_SCHEMA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/schema/guest-artifact.schema.json" GRAPHX_GUEST_SCHEMA)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/schema/catalog-lock.schema.json" GRAPHX_LOCK_SCHEMA)
# Remove indentation before embedding to stay within portable C++ literal limits.
foreach(schema_variable GRAPHX_AUTHORED_SCHEMA GRAPHX_TYPE_SCHEMA GRAPHX_NORMALIZED_SCHEMA GRAPHX_GUEST_SCHEMA GRAPHX_LOCK_SCHEMA)
  string(REGEX REPLACE "\n[ \t]+" "\n" ${schema_variable} "${${schema_variable}}")
endforeach()
file(GLOB GRAPHX_RUNTIME_TYPE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/catalog/types/*.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GRAPHX_RUNTIME_TYPE_FILES})
set(GRAPHX_RUNTIME_TYPES "[")
option(GRAPHX_RELEASE_IMAGE_TYPES "Embed the container revisions derived by image_release.py" OFF)
foreach(type_file IN LISTS GRAPHX_RUNTIME_TYPE_FILES)
  file(READ "${type_file}" type_json)
  if(GRAPHX_RELEASE_IMAGE_TYPES)
    string(JSON type_execution GET "${type_json}" execution)
    if(type_execution MATCHES "\"container\"")
      string(JSON type_revision GET "${type_json}" revision)
      math(EXPR type_revision "${type_revision} + 1")
      string(JSON type_json SET "${type_json}" revision ${type_revision})
    endif()
  endif()
  string(APPEND GRAPHX_RUNTIME_TYPES "${type_json},")
endforeach()
string(REGEX REPLACE ",$" "]" GRAPHX_RUNTIME_TYPES "${GRAPHX_RUNTIME_TYPES}")
string(REGEX REPLACE "\n[ ]*" "" GRAPHX_RUNTIME_TYPES "${GRAPHX_RUNTIME_TYPES}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deploy/observability/grafana/dashboards/graphx-operations.json" GRAPHX_GRAFANA_DASHBOARD)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deploy/observability/alerts.yml" GRAPHX_PROMETHEUS_ALERTS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/observability/grafana/dashboards/graphx-operations.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/deploy/observability/alerts.yml")
configure_file(cmake/platform_assets.hpp.in generated/graphx/platform_assets.hpp @ONLY)
configure_file(cmake/config_schemas.hpp.in generated/graphx/config_schemas.hpp @ONLY)
option(GRAPHX_QUALIFICATION_HOOKS "Enable private application admission fault fixtures" OFF)
if(GRAPHX_QUALIFICATION_HOOKS)
  target_compile_definitions(graphx PRIVATE GRAPHX_QUALIFICATION_HOOKS=1)
endif()
target_compile_definitions(graphx PRIVATE GRAPHX_EXAMPLE_HELPER="${CMAKE_CURRENT_SOURCE_DIR}/scripts/example_cli.py" GRAPHX_CATALOG_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/config/catalog")
install(DIRECTORY config/catalog DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx)
configure_file(include/graphx/version.hpp.in generated/graphx/version.hpp @ONLY)
configure_file(cmake/GraphXBuildDependencies.json.in
  generated/graphx-build-dependencies.json @ONLY)
target_include_directories(graphx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(graphx PUBLIC cxx_std_20)
target_link_libraries(graphx PUBLIC Threads::Threads yaml-cpp::yaml-cpp OpenSSL::SSL OpenSSL::Crypto)
graphx_enable_analysis(graphx)
graphx_enable_sanitizers(graphx)
target_compile_options(graphx PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(GRAPHX_BUILD_FUZZERS)
  target_compile_options(graphx PRIVATE -fsanitize=fuzzer-no-link)
endif()

foreach(app generator transform sink diagnostic packet-guest)
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
include(cmake/Examples.cmake)
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
install(TARGETS graphx-cli graphx-generator graphx-transform graphx-sink graphx-diagnostic graphx-packet-guest
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
install(FILES config/schema/graphx.schema.json config/schema/normalized-graph.schema.json
  config/schema/node-type.schema.json config/schema/guest-artifact.schema.json config/schema/catalog-lock.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/schema COMPONENT runtime)
install(PROGRAMS tools/graphx-extcap
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/graphx COMPONENT tools)
install(FILES wireshark/graphx.lua wireshark/README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/graphx/wireshark COMPONENT tools)
install(FILES LICENSE THIRD_PARTY.md README.md VERSION SECURITY.md SUPPORT.md
  CONTRIBUTING.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/graphx COMPONENT documentation)
install(FILES docs/GraphX_Architecture.md docs/README.md
  docs/documentation-verification.md docs/project-decisions.md
  docs/release-license-inventory.md docs/test-procedure.md docs/user-guide.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/graphx/docs COMPONENT documentation)
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

  function(graphx_add_test_target target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE graphx)
    graphx_enable_analysis(${target})
    graphx_enable_sanitizers(${target})
  endfunction()

  function(graphx_add_python_test name script timeout)
    add_test(NAME ${name} COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/${script} ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout}
      ENVIRONMENT "GRAPHX_OVERRIDES=")
  endfunction()

  graphx_add_test_target(graphx-lifecycle-application tests/lifecycle_application.cpp)
  graphx_add_test_target(graphx-tests tests/test_main.cpp)
  target_compile_definitions(graphx-tests PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME graphx-tests COMMAND graphx-tests)
  graphx_add_test_target(graphx-command-runner-tests tests/test_command_runner.cpp)
  target_include_directories(graphx-command-runner-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME graphx-command-runner COMMAND graphx-command-runner-tests)
  set_tests_properties(graphx-command-runner PROPERTIES TIMEOUT 15)
  graphx_add_test_target(graphx-node-console-tests tests/test_node_console.cpp)
  target_include_directories(graphx-node-console-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME graphx-node-console COMMAND graphx-node-console-tests)
  graphx_add_test_target(graphx-ownership-state-tests tests/test_ownership_state.cpp)
  target_include_directories(graphx-ownership-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(graphx-ownership-state-tests
    PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME graphx-ownership-state COMMAND graphx-ownership-state-tests)
  set_tests_properties(graphx-ownership-state PROPERTIES TIMEOUT 15)
  graphx_add_test_target(graphx-guest-artifact-tests tests/test_guest_artifacts.cpp)
  target_include_directories(graphx-guest-artifact-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME graphx-guest-artifacts COMMAND graphx-guest-artifact-tests ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(graphx-guest-artifacts PROPERTIES TIMEOUT 15)
  graphx_add_test_target(graphx-resource-tests tests/test_resource_modules.cpp)
  target_include_directories(graphx-resource-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME graphx-resource-modules COMMAND graphx-resource-tests)
  set_tests_properties(graphx-resource-modules PROPERTIES TIMEOUT 15)
  foreach(domain graph network observability transport)
    graphx_add_test_target(graphx-config-${domain}-tests tests/test_config_${domain}.cpp)
    target_compile_definitions(graphx-config-${domain}-tests
      PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME graphx-config-${domain} COMMAND graphx-config-${domain}-tests)
    set_tests_properties(graphx-config-${domain} PROPERTIES TIMEOUT 30)
  endforeach()
  graphx_add_test_target(graphx-tls-smoke tests/tls_smoke.cpp)
  add_test(NAME graphx-tls-security
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test-tls.sh $<TARGET_FILE:graphx-tls-smoke>)
  set_tests_properties(graphx-tls-security PROPERTIES TIMEOUT 15)
  graphx_add_test_target(graphx-capture-fixture tests/capture_fixture.cpp)
  graphx_add_test_target(graphx-capture-runtime-config tests/capture_runtime_config.cpp)
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
  graphx_add_python_test(graphx-cmake-examples tests/test_cmake_examples.py 90 "${CMAKE_CURRENT_SOURCE_DIR}")
  graphx_add_python_test(graphx-example-cli tests/test_example_cli.py 180 "$<TARGET_FILE:graphx-cli>" "${CMAKE_CURRENT_SOURCE_DIR}")
  graphx_add_python_test(graphx-vita-example tests/test_vita_example.py 45
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-vita-acceptance-contract tests/test_vita_acceptance_contract.py 15)
  graphx_add_python_test(graphx-config-cli tests/test_example_configs.py 45
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-config-v3-matrix tests/test_config_v3_matrix.py 120
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-normalized-config tests/test_normalized_config.py 45
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-ovs-compiled tests/test_ovs_compiled.py 60
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-ovs-ownership-plan tests/test_ovs_ownership_plan.py 30
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-container-veth-plan tests/test_container_veth_plan.py 30
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-network-lab-plans tests/test_network_lab_plans.py 60
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  if(GRAPHX_ENABLE_LINUX_OVS_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME graphx-compiled-live
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test-linux-network-features.sh)
    set_tests_properties(graphx-compiled-live PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
  endif()
  add_test(NAME graphx-documentation-consistency COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_documentation_consistency.py ${CMAKE_SOURCE_DIR}
    $<TARGET_FILE:graphx-cli>)
  add_test(NAME graphx-repository-hygiene COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_repository_hygiene.py ${CMAKE_SOURCE_DIR})
  add_test(NAME graphx-lima-static COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_lima_environment.py ${CMAKE_SOURCE_DIR})
  set_tests_properties(graphx-lima-static PROPERTIES TIMEOUT 15)
  add_test(NAME graphx-node-version COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_node_version.py ${CMAKE_SOURCE_DIR})
  set_tests_properties(graphx-node-version PROPERTIES TIMEOUT 15)
  add_test(NAME graphx-version COMMAND graphx-cli --version)
  set_tests_properties(graphx-version PROPERTIES
    PASS_REGULAR_EXPRESSION "^graphx ${GRAPHX_VERSION}")
  add_test(NAME graphx-release-contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_release.py ${CMAKE_SOURCE_DIR} $<TARGET_FILE:graphx-cli>)
  add_test(NAME graphx-image-release COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/test_image_release.py ${CMAKE_SOURCE_DIR})
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
  add_test(NAME graphx-udp-transport
    COMMAND graphx-tests --filter "UDP" --repeat 3)
  set_tests_properties(graphx-udp-transport PROPERTIES TIMEOUT 30)
  graphx_add_python_test(graphx-packet-guest tests/test_packet_guest.py 30
    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-guest-release tests/test_guest_release.py 30
    ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-guest-agent tests/test_guest_agent.py 30
    ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-scenario tests/test_scenario.py 60
    $<TARGET_FILE:graphx-cli> ${CMAKE_CURRENT_SOURCE_DIR})
  graphx_add_python_test(graphx-compile tests/test_compile.py 120
    $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-diagnostic-bindings tests/test_diagnostic_bindings.py 30
    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
  graphx_add_python_test(graphx-node-bindings tests/test_node_bindings.py 60
    ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR})
  add_test(NAME graphx-qemu-node-static
    COMMAND ${CMAKE_SOURCE_DIR}/examples/qemu-node/scripts/test.sh $<TARGET_FILE:graphx-cli>)
  set_tests_properties(graphx-qemu-node-static PROPERTIES TIMEOUT 30)
  add_test(NAME graphx-qemu-tap-plan
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_qemu_tap_plan.py
      $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  set_tests_properties(graphx-qemu-tap-plan PROPERTIES
    TIMEOUT 30 ENVIRONMENT "GRAPHX_OVERRIDES=")
  add_test(NAME graphx-network-observability-plan
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_network_observability_plan.py
      $<TARGET_FILE:graphx-cli> ${CMAKE_SOURCE_DIR})
  set_tests_properties(graphx-network-observability-plan PROPERTIES
    TIMEOUT 30 ENVIRONMENT "GRAPHX_OVERRIDES=")
  add_test(NAME graphx-sdr-node-portable
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_sdr_example.py
      ${CMAKE_SOURCE_DIR})
  set_tests_properties(graphx-sdr-node-portable PROPERTIES TIMEOUT 15)
  add_test(NAME graphx-static-route-policy-portable
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_static_route_policy.py
      ${CMAKE_SOURCE_DIR} $<TARGET_FILE:graphx-cli>)
  set_tests_properties(graphx-static-route-policy-portable PROPERTIES
    TIMEOUT 30 ENVIRONMENT "GRAPHX_OVERRIDES=")
  if(GRAPHX_ENABLE_SANITIZERS)
    add_test(NAME graphx-sanitizer-coverage
      COMMAND ${CMAKE_COMMAND}
        -DGRAPHX_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
        -DGRAPHX_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DGRAPHX_EXPECTED_SANITIZERS=${GRAPHX_SANITIZERS}
        -P ${CMAKE_SOURCE_DIR}/cmake/verify-sanitizer-coverage.cmake)
  endif()

  # Keep the everyday feedback loop focused. Package construction and repeated
  # UDP stress remain registered, but run only in their dedicated profiles.
  get_property(graphx_registered_tests DIRECTORY PROPERTY TESTS)
  foreach(graphx_test IN LISTS graphx_registered_tests)
    if(graphx_test STREQUAL "graphx-package")
      set_tests_properties(${graphx_test} PROPERTIES LABELS package)
    elseif(graphx_test MATCHES "-live($|-)")
      set_tests_properties(${graphx_test} PROPERTIES LABELS privileged)
    elseif(graphx_test MATCHES "stress|graphx-udp-transport")
      set_tests_properties(${graphx_test} PROPERTIES LABELS stress)
    else()
      set_tests_properties(${graphx_test} PROPERTIES LABELS quick)
    endif()
  endforeach()
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

install(FILES scripts/example_cli.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/graphx)

if(GRAPHX_BUILD_VITA_RADIO)
  include(cmake/VitaRadio.cmake)
endif()
