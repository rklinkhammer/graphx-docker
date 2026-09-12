#include "config_internal.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <regex>
#include <unordered_set>

namespace graphx::config_internal {

const std::regex kIdentifier{"^[A-Za-z][A-Za-z0-9_-]{0,63}$"};
const std::regex kMacAddress{"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"};
const std::regex kInterfaceName{"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,14}$"};
const std::regex kHttpOrigin{R"(^https?://(\[[0-9A-Fa-f:]+\]|[A-Za-z0-9.-]+)(:([0-9]{1,5}))?/?$)"};

std::optional<std::uint32_t> ipv4_address(std::string_view value) {
  in_addr address{};
  const std::string source(value);
  if (::inet_pton(AF_INET, source.c_str(), &address) != 1) return std::nullopt;
  return ntohl(address.s_addr);
}

std::optional<Ipv4Cidr> ipv4_cidr(std::string_view value) {
  const auto slash = value.find('/');
  if (slash == std::string_view::npos) return std::nullopt;
  const auto address = ipv4_address(value.substr(0, slash));
  if (!address) return std::nullopt;
  unsigned prefix{};
  const auto prefix_text = value.substr(slash + 1);
  const auto result =
      std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(), prefix);
  if (result.ec != std::errc{} || result.ptr != prefix_text.data() + prefix_text.size() ||
      prefix > 32)
    return std::nullopt;
  const auto mask = prefix == 0 ? 0U : 0xffffffffU << (32U - prefix);
  return Ipv4Cidr{*address & mask, mask, prefix};
}

ConfigParser::ConfigParser(YAML::Node root) : root_(std::move(root)) {}

GraphConfig ConfigParser::parse() {
  GraphConfig config;
  if (!require_map(root_, "$")) throw ConfigError(std::move(errors_));
  strict_keys(root_, "$",
              {"version", "graph", "transport", "network", "deployment", "observability"});
  config.version = strict_unsigned_value(root_["version"], "version");
  if (config.version != kConfigVersion)
    error("version", "must be " + std::to_string(kConfigVersion));

  const auto graph = root_["graph"];
  if (require_map(graph, "graph")) {
    strict_keys(graph, "graph", {"id", "nodes", "edges"});
    config.id = text(graph["id"], "graph.id", 64);
    identifier(config.id, "graph.id");
    parse_nodes(graph["nodes"], config);
    parse_edges(graph["edges"], config);
  }
  parse_transports(root_["transport"], config);
  parse_network_infrastructure(root_["network"], config);
  parse_deployment(root_["deployment"], config);
  parse_observability(root_["observability"], config);
  validate_graph(config);
  validate_sdr(config);
  if (!errors_.empty()) throw ConfigError(std::move(errors_));
  return config;
}

void ConfigParser::error(std::string path, std::string message) {
  errors_.push_back({std::move(path), std::move(message)});
}

bool ConfigParser::require_map(const YAML::Node& node, const std::string& path) {
  if (!node) {
    error(path, "is required");
    return false;
  }
  if (!node.IsMap()) {
    error(path, "must be a mapping");
    return false;
  }
  return true;
}

bool ConfigParser::require_sequence(const YAML::Node& node, const std::string& path) {
  if (!node) {
    error(path, "is required");
    return false;
  }
  if (!node.IsSequence()) {
    error(path, "must be a sequence");
    return false;
  }
  return true;
}

void ConfigParser::strict_keys(const YAML::Node& node, const std::string& path,
                               std::initializer_list<std::string_view> allowed) {
  if (!node.IsMap()) return;
  std::unordered_set<std::string> seen;
  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      error(path, "contains a non-scalar key");
      continue;
    }
    const auto key = entry.first.Scalar();
    if (!seen.insert(key).second) error(path + "." + key, "duplicate key");
    if (std::ranges::find(allowed, key) == allowed.end())
      error(path + "." + key, "unknown property");
  }
}

std::string ConfigParser::text(const YAML::Node& node, const std::string& path,
                               std::size_t maximum) {
  if (!node) {
    error(path, "is required");
    return {};
  }
  if (!node.IsScalar()) {
    error(path, "must be a scalar string");
    return {};
  }
  const auto& value = node.Scalar();
  if (value.empty()) error(path, "must not be empty");
  if (value.size() > maximum) error(path, "exceeds maximum length " + std::to_string(maximum));
  return value;
}

std::string ConfigParser::strict_text(const YAML::Node& node, const std::string& path,
                                      std::size_t maximum) {
  const auto value = text(node, path, maximum);
  if (node && node.IsScalar() && node.Tag() == "?") {
    try {
      static_cast<void>(node.as<double>());
      error(path, "must be a string, not a number");
    } catch (const YAML::Exception&) {
      // Plain nonnumeric scalars remain strings, except booleans checked below.
    }
  }
  if (node && node.IsScalar() && node.Tag() != "!" && node.Tag() != "tag:yaml.org,2002:str" &&
      (node.Tag() != "?" ||
       std::regex_match(value, std::regex("^(true|false|True|False|TRUE|FALSE)$"))))
    error(path, "must be a string, not a typed scalar");
  return value;
}

std::uint32_t ConfigParser::unsigned_value(const YAML::Node& node, const std::string& path) {
  if (!node) {
    error(path, "is required");
    return 0;
  }
  try {
    return node.as<std::uint32_t>();
  } catch (const YAML::Exception&) {
    error(path, "must be an unsigned integer");
    return 0;
  }
}

std::uint32_t ConfigParser::strict_unsigned_value(const YAML::Node& node, const std::string& path) {
  if (node && (node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str")) {
    error(path, "must be an unsigned integer, not a string");
    return 0;
  }
  return unsigned_value(node, path);
}

std::uint64_t ConfigParser::unsigned_64_value(const YAML::Node& node, const std::string& path) {
  if (!node) {
    error(path, "is required");
    return 0;
  }
  try {
    return node.as<std::uint64_t>();
  } catch (const YAML::Exception&) {
    error(path, "must be an unsigned integer");
    return 0;
  }
}

std::uint64_t ConfigParser::strict_unsigned_64_value(const YAML::Node& node,
                                                     const std::string& path) {
  if (node && (node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str")) {
    error(path, "must be an unsigned integer, not a string");
    return 0;
  }
  return unsigned_64_value(node, path);
}

bool ConfigParser::bool_value(const YAML::Node& node, const std::string& path, bool fallback) {
  if (!node) return fallback;
  try {
    return node.as<bool>();
  } catch (const YAML::Exception&) {
    error(path, "must be a boolean");
    return fallback;
  }
}

bool ConfigParser::strict_bool_value(const YAML::Node& node, const std::string& path,
                                     bool fallback) {
  if (!node) return fallback;
  if (!node.IsScalar() || node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str") {
    error(path, "must be a boolean, not a string");
    return fallback;
  }
  const auto& value = node.Scalar();
  if (value == "true" || value == "True" || value == "TRUE") return true;
  if (value == "false" || value == "False" || value == "FALSE") return false;
  error(path, "must be a boolean");
  return fallback;
}

double ConfigParser::double_value(const YAML::Node& node, const std::string& path,
                                  double fallback) {
  if (!node) return fallback;
  try {
    const auto value = node.as<double>();
    if (!std::isfinite(value)) {
      error(path, "must be a finite number");
      return fallback;
    }
    return value;
  } catch (const YAML::Exception&) {
    error(path, "must be a finite number");
    return fallback;
  }
}

void ConfigParser::identifier(const std::string& value, const std::string& path) {
  if (!value.empty() && !std::regex_match(value, kIdentifier))
    error(path, "must match [A-Za-z][A-Za-z0-9_-]{0,63}");
}

}  // namespace graphx::config_internal
