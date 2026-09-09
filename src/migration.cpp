#include "graphx/migration.hpp"

#include "graphx/config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace graphx {
namespace {

std::string migrated_profile(const NetworkDefinition& network) {
  switch (network.driver) {
    case NetworkDriver::bridge:
      if (!network.mode.empty() || !network.parent.empty())
        throw std::invalid_argument("bridge network '" + network.id +
                                    "' has legacy parent/mode fields with no version-2 meaning");
      return "ethernet";
    case NetworkDriver::macvlan:
      if (!network.mode.empty() && network.mode != "bridge")
        throw std::invalid_argument("macvlan network '" + network.id +
                                    "' uses unsupported legacy mode '" + network.mode + "'");
      return "macvlan";
    case NetworkDriver::ipvlan:
      if (network.mode == "l2") return "ipvlan-l2";
      if (network.mode == "l3") return "ipvlan-l3";
      if (network.mode == "l3s") return "ipvlan-l3s";
      break;
  }
  throw std::invalid_argument("network '" + network.id + "' cannot be migrated deterministically");
}

AttachmentKind inferred_kind(const GraphConfig& config, std::string_view owner) {
  const auto node = std::ranges::find_if(
      config.nodes, [&](const auto& candidate) { return candidate.id == owner; });
  if (node == config.nodes.end())
    throw std::invalid_argument("network interface owner '" + std::string(owner) +
                                "' is not a graph node");
  if (node->runtime == "qemu") return AttachmentKind::qemu_tap;
  if (std::ranges::any_of(config.deployment.services,
                          [&](const auto& service) { return service.node_id == owner; }))
    return AttachmentKind::container_veth;
  return AttachmentKind::external;
}

std::string attachment_id(std::string_view owner, std::string_view interface) {
  std::string result = std::string(owner) + "-" + std::string(interface);
  if (result.size() > 64)
    throw std::invalid_argument("migrated attachment id exceeds 64 characters: " + result);
  return result;
}

void append_attachment(YAML::Node& attachments, std::set<std::string>& ids,
                       const AttachmentDefinition& attachment) {
  if (!ids.insert(attachment.id).second)
    throw std::invalid_argument("migrated attachment id collision: " + attachment.id);
  YAML::Node item;
  item["id"] = attachment.id;
  item["kind"] = std::string(to_string(attachment.kind));
  item["owner"] = attachment.owner;
  if (!attachment.network.empty()) item["network"] = attachment.network;
  if (!attachment.address.empty()) item["address"] = attachment.address;
  if (!attachment.mac.empty()) item["mac"] = attachment.mac;
  if (!attachment.interface.empty()) item["interface"] = attachment.interface;
  if (!attachment.peer.empty()) item["peer"] = attachment.peer;
  if (!attachment.network_switch.empty()) item["switch"] = attachment.network_switch;
  attachments.push_back(item);
}

}  // namespace

std::string migrate_config_v1_to_v2(const std::filesystem::path& source) {
  const auto size = std::filesystem::file_size(source);
  if (size > kMaxConfigBytes) throw std::invalid_argument("configuration exceeds 1 MiB");
  const auto config = load_config_literal(source);
  if (config.version != 1)
    throw std::invalid_argument("migration input must be configuration version 1");
  for (const auto& network_switch : config.network_infrastructure.switches)
    if (network_switch.datapath != "system")
      throw std::invalid_argument("switch '" + network_switch.id +
                                  "' must use the system datapath before version-2 migration");

  YAML::Node root;
  try {
    root = YAML::LoadFile(source.string());
  } catch (const YAML::Exception& error) {
    throw std::invalid_argument("cannot parse migration input: " + std::string(error.what()));
  }
  root["version"] = 2;
  auto network = root["network"];
  if (network && network["networks"]) {
    for (std::size_t index = 0; index < config.network_infrastructure.networks.size(); ++index) {
      auto item = network["networks"][index];
      const auto& definition = config.network_infrastructure.networks[index];
      item.remove("driver");
      item.remove("mode");
      item.remove("parent");
      item.remove("subnet");
      item["profile"] = migrated_profile(definition);
      YAML::Node subnets(YAML::NodeType::Sequence);
      for (const auto& subnet : definition.subnets) subnets.push_back(subnet);
      item["subnets"] = subnets;
      if (!definition.parent.empty()) item["uplink"] = definition.parent;
    }

    YAML::Node attachments(YAML::NodeType::Sequence);
    std::set<std::string> attachment_ids;
    for (const auto& interface : config.network_infrastructure.interfaces) {
      AttachmentDefinition attachment;
      attachment.id = attachment_id(interface.owner, interface.id);
      attachment.kind = inferred_kind(config, interface.owner);
      attachment.owner = interface.owner;
      attachment.network = interface.network;
      attachment.address = interface.address;
      attachment.mac = interface.mac;
      append_attachment(attachments, attachment_ids, attachment);
    }
    for (const auto& router : config.network_infrastructure.routers) {
      if (router.kind != RouterKind::linux_namespace) continue;
      for (const auto& interface : router.interfaces) {
        AttachmentDefinition attachment;
        attachment.id = attachment_id(router.id, interface.id);
        attachment.kind = AttachmentKind::namespace_veth;
        attachment.owner = router.id;
        attachment.network = interface.network;
        attachment.address = interface.address;
        attachment.interface = interface.device;
        attachment.peer = interface.peer;
        attachment.network_switch = interface.network_switch;
        append_attachment(attachments, attachment_ids, attachment);
      }
    }
    for (const auto& network_switch : config.network_infrastructure.switches) {
      if (!network_switch.mirror) continue;
      const auto port = std::ranges::find_if(network_switch.ports, [&](const auto& candidate) {
        return candidate.id == network_switch.mirror->output_port;
      });
      AttachmentDefinition attachment;
      attachment.id = network_switch.mirror->id;
      attachment.kind = AttachmentKind::mirror;
      attachment.owner = network_switch.id;
      attachment.interface = port->interface;
      attachment.network_switch = network_switch.id;
      append_attachment(attachments, attachment_ids, attachment);
    }
    network.remove("interfaces");
    if (attachments.size() > 0) network["attachments"] = attachments;
  }
  if (root["deployment"]) root["deployment"].remove("network");

  YAML::Emitter output;
  output.SetIndent(2);
  output << root;
  if (!output.good()) throw std::runtime_error("could not emit migrated configuration");
  return "# GraphX deterministic migration from configuration version 1.\n"
         "# Review semantic profiles and attachment kinds before realization.\n" +
         std::string(output.c_str()) + "\n";
}

}  // namespace graphx
