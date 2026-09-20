#pragma once

#include "config_document.hpp"
#include <algorithm>
#include <map>
#include <string>

namespace graphx::infra::detail {
inline bool application_ready(std::string_view log, const std::string& name) {
  const auto marker = "ready node=" + name;
  std::size_t offset{};
  while ((offset = log.find(marker, offset)) != std::string_view::npos) {
    const auto end = offset + marker.size();
    if ((offset == 0 || log[offset - 1] == '\n') && (end == log.size() || log[end] == '\n'))
      return true;
    offset = end;
  }
  return false;
}
inline bool available_startup(const ConfigValue& graph) {
  return graph.contains("lifecycle") && graph.at("lifecycle").at("startup").text() == "available";
}
inline std::int64_t application_readiness_ms(const ConfigValue& graph) {
  return graph.contains("lifecycle") ? graph.at("lifecycle").at("readiness_ms").integer() : 30000;
}
// A normal unknown error must never be silently classified as peer unavailability.
inline bool unavailable_exit(int code, bool container = false) {
  // Docker records signal termination as 128 + signal; native waitpid preserves it.
  return code == 0 || code == 75 || (container && code > 128 && code <= 159);
}
inline std::string application_status(const ConfigValue& graph,
                                      const std::map<std::string, bool>& live) {
  std::size_t count{}, radios{};
  bool processor = false, needs_processor = false;
  for (const auto& node : graph.at("nodes").array()) {
    const auto found = live.find(node.at("node_id").text());
    const bool running = found != live.end() && found->second;
    count += running;
    if (node.at("type").text() == "vita.radio") radios += running;
    if (node.at("type").text() == "vita.processor") {
      needs_processor = true;
      processor = processor || running;
    }
  }
  if (!count || (needs_processor && (!processor || !radios))) return "unavailable";
  return count == graph.at("nodes").array().size() ? "ready" : "degraded";
}
}  // namespace graphx::infra::detail
