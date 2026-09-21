# One entry point for every authored example; lifecycle stays in example_cli.py.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GRAPHX_EXAMPLE_TARGET "" CACHE STRING "Execution target; empty selects the host default")
set_property(CACHE GRAPHX_EXAMPLE_TARGET PROPERTY STRINGS "" lima orbstack native-linux native-macos)
option(GRAPHX_EXAMPLE_ALLOW_PRIVILEGED "Explicitly permit Linux/Lima example operations" OFF)
foreach(field WORKSPACE INSTANCE IMAGES RELEASE CATALOG EXTERNAL LABORATORY NODE SCENARIO)
  set(GRAPHX_EXAMPLE_${field} "" CACHE STRING "Example CLI ${field} selection")
endforeach()
set(GRAPHX_EXAMPLE_CONTROL "" CACHE STRING "Semicolon-separated NODE:ACTION,ACTION grants")

# Component expressions do not introduce a target dependency (CMP0112 NEW).
# In particular down/status must work even when current sources cannot compile.
set(graphx_example_cli "$<TARGET_FILE_DIR:graphx-cli>/$<TARGET_FILE_NAME:graphx-cli>")
set(graphx_example_command "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/example_cli.py"
  --graphx "${graphx_example_cli}" --source "${PROJECT_SOURCE_DIR}" example)
set(graphx_example_options)
foreach(field TARGET WORKSPACE INSTANCE IMAGES RELEASE CATALOG EXTERNAL LABORATORY NODE)
  if(NOT GRAPHX_EXAMPLE_${field} STREQUAL "")
    string(TOLOWER "${field}" flag)
    list(APPEND graphx_example_options "--${flag}" "${GRAPHX_EXAMPLE_${field}}")
  endif()
endforeach()
foreach(grant IN LISTS GRAPHX_EXAMPLE_CONTROL)
  list(APPEND graphx_example_options --control "${grant}")
endforeach()
if(GRAPHX_EXAMPLE_ALLOW_PRIVILEGED)
  list(APPEND graphx_example_options --allow-privileged)
endif()

add_custom_target(examples-list
  COMMAND ${graphx_example_command} list
  DEPENDS graphx-cli USES_TERMINAL VERBATIM)
file(GLOB_RECURSE graphx_example_inputs CONFIGURE_DEPENDS
  RELATIVE "${PROJECT_SOURCE_DIR}/examples" "${PROJECT_SOURCE_DIR}/examples/graphx.yml")
foreach(input IN LISTS graphx_example_inputs)
  get_filename_component(example "${input}" DIRECTORY)
  string(REPLACE "/" "-" stem "${example}")
  foreach(action plan prepare up status open logs down scenario-plan scenario-run scenario-status scenario-clear)
    set(options ${graphx_example_options})
    set(operation "${action}")
    if(action MATCHES "^scenario-(.*)$")
      set(operation scenario)
      list(APPEND options --operation "${CMAKE_MATCH_1}")
      if(NOT GRAPHX_EXAMPLE_SCENARIO STREQUAL "")
        list(APPEND options --action "${GRAPHX_EXAMPLE_SCENARIO}")
      endif()
    elseif(action STREQUAL "prepare")
      # Explicit preparation starts a new verification release. Up reuses it.
      list(APPEND options --restart)
      if(GRAPHX_EXAMPLE_IMAGES STREQUAL "" AND GRAPHX_EXAMPLE_RELEASE STREQUAL "" AND GRAPHX_EXAMPLE_CATALOG STREQUAL "")
        list(APPEND options --fresh-images)
      endif()
    elseif(action STREQUAL "up")
      list(APPEND options --restart)
    endif()
    add_custom_target(${stem}-${action}
      COMMAND ${graphx_example_command} "${operation}" "${example}" ${options}
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" USES_TERMINAL VERBATIM)
    if(action MATCHES "^(plan|prepare|up)$")
      add_dependencies(${stem}-${action} graphx-cli)
    endif()
  endforeach()
endforeach()

if(GRAPHX_BUILD_EXAMPLES)
  string(SHA256 graphx_build_location "${PROJECT_SOURCE_DIR}|${PROJECT_BINARY_DIR}")
  set(GRAPHX_EXAMPLE_ARTIFACT_ROOT "$ENV{HOME}/.cache/graphx/build/${graphx_build_location}"
    CACHE PATH "External output root for shared images and QEMU build artifacts")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(graphx_image_platform linux/arm64)
  else()
    set(graphx_image_platform linux/amd64)
  endif()
  set(GRAPHX_EXAMPLE_IMAGE_PLATFORM "${graphx_image_platform}" CACHE STRING "Shared image architecture")
  set(graphx_build_examples "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build_examples.py"
    --source "${PROJECT_SOURCE_DIR}" --graphx "${graphx_example_cli}"
    --output "${GRAPHX_EXAMPLE_ARTIFACT_ROOT}" --platform "${GRAPHX_EXAMPLE_IMAGE_PLATFORM}")
  add_custom_target(examples-build ALL
    COMMAND ${graphx_build_examples}
    DEPENDS graphx-cli USES_TERMINAL VERBATIM)
  add_custom_target(examples-rebuild
    COMMAND ${graphx_build_examples} --fresh
    DEPENDS graphx-cli USES_TERMINAL VERBATIM)
endif()
