#include "../src/application_observer.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>

int main() {
  std::filesystem::path directory;
  try {
    auto pattern =
        (std::filesystem::temp_directory_path() / "graphx-capture-binding-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    if (!created) return 1;
    directory = created;
    graphx::GraphConfig config;
    config.observability.metrics.exporters = {"console"};
    config.observability.tracing.exporters = {"console"};
    config.observability.capture.enabled = true;
    config.observability.capture.provider = "pcapng";
    config.observability.capture.directory = directory.string();
    ::setenv("GRAPHX_CAPTURE_ENABLED", "false", 1);
    ::setenv("GRAPHX_CAPTURE_DIR", "/nonexistent", 1);
    {
      demo::RuntimeTraceSink trace("renamed-instance", config);
      trace.on_send("renamed-connection", graphx::Envelope::make(1, "Sample", "7"), 1);
    }
    if (std::filesystem::file_size(directory / "renamed-instance.pcapng") <= 100)
      throw std::runtime_error("resolved capture settings or identity were ignored");
    std::filesystem::remove_all(directory);
    std::cout << "Resolved application capture settings and identity passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::error_code ignored;
    if (!directory.empty()) std::filesystem::remove_all(directory, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
