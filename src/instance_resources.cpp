#include "graphx/instance_resources.hpp"

#include <openssl/evp.h>

#include <array>
#include <map>
#include <stdexcept>

namespace graphx {
namespace {
bool identifier(std::string_view value) {
  const auto letter = [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); };
  if (value.empty() || value.size() > 64 || !letter(value.front())) return false;
  for (const char ch : value)
    if (!letter(ch) && !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-') return false;
  return true;
}
}  // namespace

std::string instance_resource_name(std::string_view graph, std::string_view instance,
                                   std::string_view kind, std::string_view logical) {
  if (!identifier(graph) || !identifier(instance) || kind.empty() || logical.empty())
    throw std::invalid_argument("invalid instance resource identity");
  std::string input = "graphx-instance-resources-v1";
  for (const auto part : {graph, instance, kind, logical})
    input += std::to_string(part.size()) + ":" + std::string(part);
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size{};
  if (EVP_Digest(input.data(), input.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1 ||
      size != 32)
    throw std::runtime_error("cannot hash instance resource identity");
  constexpr std::string_view hex = "0123456789abcdef";
  std::string result = "gx";
  for (unsigned int index = 0; index < size; ++index) {
    result += hex[digest[index] >> 4];
    result += hex[digest[index] & 15];
  }
  // Interface and bridge names occupy the same Linux name space.
  result.resize(kind == "interface" || kind == "bridge" ? 15 : 64);
  return result;
}

InstanceResources resolve_instance_resources(const GraphConfig& source) {
  InstanceResources result{source, source.id, {}};
  const auto& instance = source.deployment.instance_id;
  if (instance.empty()) return result;
  auto& config = result.config;
  std::map<std::pair<std::string, std::string>, std::string> names;
  std::map<std::string, std::pair<std::string, std::string>> physical_names;
  const auto resolve = [&](std::string_view kind, const std::string& logical) {
    if (logical.empty()) return logical;
    const auto key = std::make_pair(std::string(kind), logical);
    if (const auto found = names.find(key); found != names.end()) return found->second;
    auto physical = instance_resource_name(source.id, instance, kind, logical);
    if (!physical_names.emplace(physical, key).second)
      throw std::runtime_error("instance resource name collision");
    names.emplace(key, physical);
    result.mappings.push_back({std::string(kind), logical, physical});
    return physical;
  };
  result.state_key = resolve("state", source.id);
  config.deployment.project = resolve("compose", source.deployment.project);
  auto& network = config.network_infrastructure;
  std::map<std::string, std::string> bridges;
  std::map<std::string, std::string> interfaces;
  for (auto& item : network.switches) {
    const auto logical = item.id;
    item.id = resolve("bridge", logical);
    bridges.emplace(logical, item.id);
  }
  for (const auto& item : network.attachments) {
    if (item.kind == AttachmentKind::external) continue;
    for (const auto& name : {item.interface, item.peer})
      if (!name.empty()) interfaces.emplace(name, resolve("interface", name));
  }
  for (const auto& item : network.switches)
    for (const auto& port : item.ports)
      if (interfaces.contains(port.interface) && !port.peer.empty())
        interfaces.emplace(port.peer, resolve("interface", port.peer));
  for (const auto& item : network.attachments)
    if (item.kind == AttachmentKind::mirror) static_cast<void>(resolve("mirror", item.id));
  const auto translate = [](std::string& value, const auto& mapping) {
    if (const auto found = mapping.find(value); found != mapping.end()) value = found->second;
  };
  for (auto& item : network.attachments) {
    translate(item.network_switch, bridges);
    if (item.kind == AttachmentKind::external) continue;
    if (item.kind == AttachmentKind::mirror) translate(item.owner, bridges);
    translate(item.interface, interfaces);
    translate(item.peer, interfaces);
    for (auto& route : item.routes) translate(route.device, interfaces);
  }
  for (auto& item : network.switches) {
    if (item.mirror)
      for (const auto& attachment : network.attachments)
        if (attachment.kind == AttachmentKind::mirror && attachment.network_switch == item.id)
          item.mirror->id = resolve("mirror", attachment.id);
    for (auto& port : item.ports) {
      translate(port.interface, interfaces);
      translate(port.peer, interfaces);
    }
  }
  for (auto& router : network.routers) {
    if (router.kind == RouterKind::linux_namespace)
      router.namespace_name = resolve("namespace", router.namespace_name);
    for (auto& item : router.interfaces) {
      translate(item.device, interfaces);
      translate(item.peer, interfaces);
      translate(item.network_switch, bridges);
    }
    for (auto& route : router.routes) translate(route.device, interfaces);
  }
  for (auto& path : network.edge_paths)
    for (auto& hop : path.hops) translate(hop, bridges);
  for (auto& capture : network.captures)
    capture.directory = (std::filesystem::path(capture.directory) / result.state_key).string();
  return result;
}
}  // namespace graphx
