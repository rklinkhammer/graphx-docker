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

inline constexpr std::string_view valid_config = R"yaml(
version: 2
graph:
  id: test-graph
  nodes:
    - id: source
      kind: source
      ports: [{ name: out, direction: output, schema: Sample }]
    - id: target
      kind: sink
      ports: [{ name: in, direction: input, schema: Sample }]
  edges:
    - { id: sample-edge, from: source.out, to: target.in, transport: tcp }
transport:
  tcp:
    sample-edge:
      host: target
      bind: 0.0.0.0
      port: 7001
      framing: u32be
      connect_timeout_ms: 1200
      send_timeout_ms: 900
      reconnect: true
      retry: { max_attempts: 7, initial_backoff_ms: 10, max_backoff_ms: 80 }
)yaml";

inline bool diagnostic_contains(const graphx::ConfigError& error, std::string_view text) {
  for (const auto& diagnostic : error.diagnostics())
    if (diagnostic.path.find(text) != std::string::npos ||
        diagnostic.message.find(text) != std::string::npos)
      return true;
  return false;
}

inline std::string current_network_config() {
  auto source = std::string(valid_config);
  source += R"yaml(
network:
  networks:
    - id: semantic-lan
      profile: ethernet
      subnets: [10.80.0.0/24]
      gateway: 10.80.0.1
      external: false
  attachments:
    - { id: source-data, kind: external, owner: source, network: semantic-lan, address: 10.80.0.10/24, mac: "02:80:00:00:00:10" }
    - { id: target-data, kind: external, owner: target, network: semantic-lan, address: 10.80.0.20/24 }
)yaml";
  return source;
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
