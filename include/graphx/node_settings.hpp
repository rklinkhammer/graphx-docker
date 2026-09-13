#pragma once

#include "graphx/config.hpp"
#include "graphx/transport_factory.hpp"

#include <functional>
#include <map>

namespace graphx {
struct PortBinding {
  EdgeConfig edge;
  ConnectionMode role{ConnectionMode::listen};
  std::string schema;
  std::string encoding;
  std::string source_address;
  std::string destination_address;
};

struct NodeSettings {
  ConfigValue resolved;
  std::map<std::string, std::vector<PortBinding>, std::less<>> bindings;
  [[nodiscard]] const std::string& id() const { return resolved.at("node_id").text(); }
  [[nodiscard]] const PortBinding& port(std::string_view name) const;
};

// Reads only the normalized node contract, using the authoritative schema and
// release type contracts. No authored configuration or environment defaults.
NodeSettings load_node_settings(const std::filesystem::path& file, std::string_view node,
                                std::string_view expected_type = {});
struct NodeArguments {
  std::string node;
  std::filesystem::path config;
  std::filesystem::path release_file;
  std::string release_token;
};
NodeArguments node_arguments(int argc, char** argv, bool execution = true);
// Call after listeners/local resources are bound. The execution adapter owns
// release creation; this function only reads a bounded, regular token file.
bool await_node_release(const NodeSettings& settings, const NodeArguments& arguments,
                        const std::function<bool()>& stopping);
}  // namespace graphx
