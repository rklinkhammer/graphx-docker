#pragma once

#include "graphx/config.hpp"
#include "graphx/infra.hpp"
#include "graphx/transport_factory.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace config_test {

inline void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class TemporaryConfig {
 public:
  explicit TemporaryConfig(std::string_view contents)
      : path_(std::filesystem::temp_directory_path() /
              ("graphx-config-" + std::to_string(::getpid()) + "-" + std::to_string(counter_++) +
               ".yaml")) {
    std::ofstream output(path_);
    output << contents;
    if (!output) throw std::runtime_error("could not write temporary configuration");
  }
  ~TemporaryConfig() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  inline static unsigned counter_{};
};

inline graphx::ConfigValue authored(std::string_view example = "sample-pipeline") {
  const auto path = std::filesystem::path(GRAPHX_SOURCE_DIR) / "examples" / example / "graphx.yml";
  auto value = graphx::load_config(path).authored;
  value["catalog"] = std::filesystem::relative(
                         std::filesystem::path(GRAPHX_SOURCE_DIR) / "config/catalog/lock.json",
                         std::filesystem::temp_directory_path())
                         .string();
  return value;
}
inline graphx::GraphConfig load_value(const graphx::ConfigValue& value) {
  TemporaryConfig file(graphx::config_value_json(value));
  return graphx::load_config(file.path());
}
inline void rejected(const graphx::ConfigValue& value, std::string_view code,
                     std::string_view path) {
  try {
    (void)load_value(value);
  } catch (const graphx::ConfigError& error) {
    for (const auto& d : error.diagnostics())
      if (d.code == code && d.path.find(path) != std::string::npos) return;
    throw std::runtime_error(std::string("unexpected diagnostic: ") + error.what());
  }
  throw std::runtime_error("invalid configuration accepted");
}

inline bool diagnostic_contains(const graphx::ConfigError& error, std::string_view text) {
  for (const auto& diagnostic : error.diagnostics())
    if (diagnostic.path.find(text) != std::string::npos ||
        diagnostic.message.find(text) != std::string::npos)
      return true;
  return false;
}

inline int run_tests(std::initializer_list<std::pair<const char*, std::function<void()>>> tests) {
  std::cout << std::unitbuf;
  ::unsetenv("GRAPHX_OVERRIDES");
  int failures{};
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "[pass] " << name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[fail] " << name << ": " << error.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}

}  // namespace config_test
