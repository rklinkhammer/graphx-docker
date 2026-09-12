#include "config_internal.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace graphx::config_internal {

void ConfigParser::parse_deployment(const YAML::Node& deployment, GraphConfig& config) {
  if (!deployment) return;
  if (!require_map(deployment, "deployment")) return;
  strict_keys(deployment, "deployment", {"project", "services", "telemetry"});
  if (deployment["project"])
    config.deployment.project = text(deployment["project"], "deployment.project", 63);
  if (!config.deployment.project.empty() &&
      !std::regex_match(config.deployment.project, std::regex("^[a-z0-9][a-z0-9_-]*$")))
    error("deployment.project", "must be a lowercase Docker Compose project name");

  const auto services = deployment["services"];
  if (services && require_map(services, "deployment.services")) {
    std::unordered_set<std::string> seen;
    for (const auto& entry : services) {
      if (!entry.first.IsScalar()) {
        error("deployment.services", "contains a non-scalar node id");
        continue;
      }
      const auto node_id = entry.first.Scalar();
      const auto path = "deployment.services." + node_id;
      identifier(node_id, path);
      if (!seen.insert(node_id).second) error(path, "duplicate service placement");
      if (!require_map(entry.second, path)) continue;
      strict_keys(entry.second, path, {"image", "command"});
      DeploymentService service;
      service.node_id = node_id;
      service.image = text(entry.second["image"], path + ".image");
      service.command = text(entry.second["command"], path + ".command");
      config.deployment.services.push_back(std::move(service));
    }
  }

  const auto telemetry = deployment["telemetry"];
  if (telemetry && require_map(telemetry, "deployment.telemetry")) {
    strict_keys(telemetry, "deployment.telemetry", {"service", "port"});
    config.deployment.telemetry_service =
        text(telemetry["service"], "deployment.telemetry.service", 128);
    const auto port = unsigned_value(telemetry["port"], "deployment.telemetry.port");
    if (port == 0 || port > 65535)
      error("deployment.telemetry.port", "must be between 1 and 65535");
    else
      config.deployment.telemetry_port = static_cast<std::uint16_t>(port);
  }
}

}  // namespace graphx::config_internal
