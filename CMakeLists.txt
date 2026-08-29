cmake_minimum_required(VERSION 3.25)
project(graphx VERSION 0.1.0 LANGUAGES CXX)

option(GRAPHX_BUILD_TESTS "Build GraphX tests" ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(graphx
  src/envelope.cpp
  src/framing.cpp
  src/in_process_transport.cpp
  src/observability.cpp
  src/tcp_transport.cpp
  src/unix_domain_socket_transport.cpp
)
target_include_directories(graphx PUBLIC include)
target_link_libraries(graphx PUBLIC Threads::Threads)
target_compile_options(graphx PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)

foreach(app generator transform sink)
  add_executable(graphx-${app} apps/${app}/main.cpp)
  target_link_libraries(graphx-${app} PRIVATE graphx)
endforeach()

install(TARGETS graphx graphx-generator graphx-transform graphx-sink)

if(GRAPHX_BUILD_TESTS)
  enable_testing()
  add_executable(graphx-tests tests/test_main.cpp)
  target_link_libraries(graphx-tests PRIVATE graphx)
  add_test(NAME graphx-tests COMMAND graphx-tests)
endif()
