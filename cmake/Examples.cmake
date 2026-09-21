# One entry point for every authored example; lifecycle stays in example_cli.py.
set(graphx_example_cli "$<TARGET_FILE_DIR:graphx-cli>/$<TARGET_FILE_NAME:graphx-cli>")

add_custom_target(examples-list
  COMMAND "${graphx_example_cli}" example list --source "${PROJECT_SOURCE_DIR}"
  DEPENDS graphx-cli USES_TERMINAL VERBATIM)

string(SHA256 graphx_build_location "${PROJECT_SOURCE_DIR}|${PROJECT_BINARY_DIR}")
set(graphx_artifact_root "$ENV{HOME}/.cache/graphx/build/${graphx_build_location}")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(graphx_image_platform linux/arm64)
else()
  set(graphx_image_platform linux/amd64)
endif()
set(graphx_build_examples "${graphx_example_cli}" artifacts build
  --source "${PROJECT_SOURCE_DIR}" --output "${graphx_artifact_root}"
  --platform "${graphx_image_platform}")
add_custom_target(examples-build
  COMMAND ${graphx_build_examples}
  DEPENDS graphx-cli USES_TERMINAL VERBATIM)
add_custom_target(examples-rebuild
  COMMAND ${graphx_build_examples} --fresh
  DEPENDS graphx-cli USES_TERMINAL VERBATIM)
