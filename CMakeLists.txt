cmake_minimum_required(VERSION 3.25)
project(graphx VERSION 0.1.0 LANGUAGES CXX)

option(GRAPHX_BUILD_TESTS "Build GraphX tests" ON)
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ language standard (20 or 23)")
set_property(CACHE CMAKE_CXX_STANDARD PROPERTY STRINGS 20 23)
if(NOT CMAKE_CXX_STANDARD STREQUAL "20" AND NOT CMAKE_CXX_STANDARD STREQUAL "23")
  message(FATAL_ERROR "GraphX supports CMAKE_CXX_STANDARD=20 or 23")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(yaml-cpp 0.9 QUIET CONFIG)
if(NOT yaml-cpp_FOUND)
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
  src/config.cpp
  src/envelope.cpp
  src/framing.cpp
  src/in_process_transport.cpp
  src/infra.cpp
  src/network.cpp
  src/observability.cpp
  src/shared_memory_transport.cpp
  src/tcp_transport.cpp
  src/unix_domain_socket_transport.cpp
  src/transport_factory.cpp
)
target_include_directories(graphx PUBLIC include)
target_link_libraries(graphx PUBLIC Threads::Threads yaml-cpp::yaml-cpp)
target_compile_options(graphx PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)

foreach(app generator transform sink)
  add_executable(graphx-${app} apps/${app}/main.cpp)
  target_link_libraries(graphx-${app} PRIVATE graphx)
endforeach()

add_executable(graphx-cli apps/cli/main.cpp)
set_target_properties(graphx-cli PROPERTIES OUTPUT_NAME graphx)
target_link_libraries(graphx-cli PRIVATE graphx)

install(TARGETS graphx graphx-cli graphx-generator graphx-transform graphx-sink)
install(FILES config/schema/graphx.schema.json DESTINATION share/graphx/schema)

if(GRAPHX_BUILD_TESTS)
  enable_testing()
  add_executable(graphx-tests tests/test_main.cpp)
  target_link_libraries(graphx-tests PRIVATE graphx)
  add_test(NAME graphx-tests COMMAND graphx-tests)
  add_executable(graphx-config-tests tests/test_config.cpp)
  target_link_libraries(graphx-config-tests PRIVATE graphx)
  target_compile_definitions(graphx-config-tests PRIVATE GRAPHX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME graphx-config-tests COMMAND graphx-config-tests)
  add_test(NAME graphx-config-cli
    COMMAND graphx-cli validate ${CMAKE_SOURCE_DIR}/graphx.yaml)
  foreach(example mixed-network macvlan ipvlan-l2 ipvlan-l3 shared-memory)
    add_test(NAME graphx-config-${example}
      COMMAND graphx-cli validate ${CMAKE_SOURCE_DIR}/examples/${example}/graphx.yaml)
  endforeach()
endif()
