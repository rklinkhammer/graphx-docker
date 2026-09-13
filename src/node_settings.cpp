#include "graphx/node_settings.hpp"
#include "bounded_output.hpp"
#include <cstdlib>
#include "config_document.hpp"
#include "graphx/config_schemas.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <regex>
#include <set>
#include <thread>

namespace graphx {
using namespace config_internal;
const PortBinding& NodeSettings::port(std::string_view name) const {
  const auto found = bindings.find(name);
  if (found == bindings.end() || found->second.size() != 1)
    reject("E_BINDING", std::string(name), "exactly one peer is required");
  return found->second.front();
}

NodeSettings load_node_settings(const std::filesystem::path& file, std::string_view node,
                                std::string_view expected_type) {
  if (node.empty()) reject("E_NODE_ID", "node_id", "--node is required");
  NodeSettings result;
  const auto source = read_document(file);
  if (source.find_first_not_of(" \r\n\t") == std::string::npos ||
      source[source.find_first_not_of(" \r\n\t")] != '{')
    reject("E_NODE_SCHEMA", "$", "a normalized JSON node object is required");
  result.resolved = parse_document(source);
  const auto& value = result.resolved;
  static const auto schema = parse_document(normalized_schema);
  validate_shape(value, schema.at("properties").at("nodes").at("items"));
  if (value.at("contract_version") != Value(2) || value.at("graph_version") != Value(3))
    reject("E_VERSION", "contract_version", "node contract 2 and graph version 3 are required");
  static const std::regex identity("^[a-z][a-z0-9_-]{0,63}$");
  if (value.at("node_id") != Value(std::string(node)) ||
      !std::regex_match(std::string(node), identity) ||
      !std::regex_match(value.at("graph_id").text(), identity))
    reject("E_NODE_ID", "node_id", "node identity does not match the requested instance");
  static const auto types = parse_document(runtime_types);
  const auto type = std::ranges::find_if(
      types.array(), [&](const auto& item) { return item.at("id") == value.at("type"); });
  if (type == types.array().end() || type->at("revision") != value.at("type_revision") ||
      (!expected_type.empty() && value.at("type") != Value(std::string(expected_type))))
    reject("E_NODE_TYPE", "type", "unsupported application type or revision");
  const auto& parameters = value.at("parameters").object();
  if (parameters.size() != type->at("parameters").object().size())
    reject("E_PARAMETER", "parameters", "resolved parameters must match the type");
  for (const auto& [name, definition] : type->at("parameters").object()) {
    if (!parameters.contains(name)) reject("E_PARAMETER", name, "missing resolved parameter");
    validate_shape(parameters.at(name), definition, "parameters." + name);
  }
  const auto& ports = type->at("ports").object();
  if (ports.size() != value.at("bindings").object().size())
    reject("E_BINDING", "bindings", "ports must match the application type");
  std::set<std::string> connections;
  for (const auto& [name, definition] : ports) {
    if (!value.at("bindings").contains(name)) reject("E_BINDING", name, "missing port");
    const auto& peers = value.at("bindings").at(name).array();
    if (peers.size() < static_cast<std::size_t>(definition.at("min_connections").integer()) ||
        peers.size() > static_cast<std::size_t>(definition.at("max_connections").integer()))
      reject("E_PORT_CARDINALITY", name, "peer count violates the type contract");
    auto& bindings = result.bindings[name];
    for (const auto& peer : peers) {
      const auto role = definition.at("listener").boolean() ? "listen" : "connect";
      if (peer.at("role") != Value(role) || peer.at("schema") != definition.at("schema") ||
          peer.at("encoding") != definition.at("encoding") ||
          std::ranges::find(definition.at("transports").array(), peer.at("transport")) ==
              definition.at("transports").array().end())
        reject("E_BINDING", name, "role, schema, encoding or transport violates the type contract");
      if (!std::regex_match(peer.at("connection").text(), identity) ||
          !connections.insert(peer.at("connection").text()).second)
        reject("E_BINDING", name, "connection identity is invalid or duplicated");
      const auto& settings = peer.at("settings");
      if (peer.at("transport") != Value("shared_memory")) {
        in_addr source_address{};
        if (::inet_pton(AF_INET, peer.at("source_address").text().c_str(), &source_address) != 1)
          reject("E_BINDING", name, "resolved source must be a numeric IPv4 address");
      }

      if (settings.contains("framing") &&
          settings.at("framing") !=
              Value(peer.at("encoding") == Value("graphx") ? "u32be" : "none"))
        reject("E_BINDING", name, "framing does not match encoding");
      if (peer.at("transport") != Value("shared_memory")) {
        in_addr address{};
        const auto destination = peer.at("destination_address").text();
        const auto target = peer.at("transport") == Value("tcp") ? "host" : "destination";
        static const std::regex hostname("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$");
        if (::inet_pton(AF_INET, destination.c_str(), &address) != 1 ||
            !std::regex_match(settings.at(target).text(), hostname) ||
            !std::regex_match(settings.at("bind").text(), hostname))
          reject("E_BINDING", name, "invalid resolved address or transport endpoint");
      }
      const auto security = peer.at("security").at("profile").text();
      if (security != "none" && security != "mtls")
        reject("E_BINDING", name, "unknown security profile");
      if (peer.at("schema") == Value("RawSdrControl") && security != "mtls")
        reject("E_BINDING", name, "SDR control requires mutual TLS");
      if (security == "mtls" &&
          (peer.at("transport") != Value("tcp") || !peer.at("security").contains("server_name") ||
           !value.at("credentials").contains(name)))
        reject("E_BINDING", name, "mTLS requires TCP, server name and port credential reference");
      if (peer.at("transport") == Value("shared_memory") &&
          settings.at("capacity").integer() * settings.at("max_message_bytes").integer() >
              268435456)
        reject("E_BOUND", name, "shared memory exceeds 256 MiB");
      EdgeConfig edge;
      edge.edge.id = peer.at("connection").text();
      edge.transport = resolved_transport(peer);
      if (auto* tcp = std::get_if<TcpTransportConfig>(&edge.transport))
        tcp->source_address = peer.at("source_address").text();
      if (auto* tcp = std::get_if<TcpTransportConfig>(&edge.transport); tcp && tcp->tls.enabled) {
        const auto* root = std::getenv("GRAPHX_CREDENTIALS");
        // Settings inspection stays pure; transport realization requires these files.
        const std::filesystem::path directory =
            std::filesystem::path(root ? root : "${GX_CREDENTIALS}") /
            value.at("credentials").at(name).text();
        tcp->tls.generation_file = (directory / "generation.json").string();
        tcp->tls.ca_file = (directory / "ca.pem").string();
        tcp->tls.certificate_file = (directory / "cert.pem").string();
        tcp->tls.private_key_file = (directory / "key.pem").string();
        tcp->tls.server_name = peer.at("security").at("server_name").text();
      }
      edge.data_plane = peer.at("encoding") == Value("graphx") ? "graphx" : "external";
      // A UDP sender binds its own resolved interface/address, not the listener's bind address.
      if (auto* udp = std::get_if<UdpTransportConfig>(&edge.transport);
          udp && std::string_view(role) == "connect")
        udp->bind = peer.at("source_address").text();
      bindings.push_back(
          {std::move(edge),
           std::string_view(role) == "listen" ? ConnectionMode::listen : ConnectionMode::connect,
           peer.at("schema").text(), peer.at("encoding").text(), peer.at("source_address").text(),
           peer.at("destination_address").text()});
    }
  }
  const auto& telemetry = value.at("telemetry");
  if (telemetry.at("host").text().empty() || telemetry.at("port").integer() < 1 ||
      telemetry.at("port").integer() > 65535)
    reject("E_BINDING", "telemetry", "bounded telemetry endpoint required");
  if (value.at("readiness").at("timeout_ms").integer() < 1 ||
      value.at("readiness").at("timeout_ms").integer() > 600000)
    reject("E_READINESS", "readiness", "bounded readiness deadline required");
  const auto& startup = value.at("startup");
  if (startup.at("bind_before_connect") != Value(true) ||
      startup.at("release_barrier") != Value(true) || startup.at("max_wait_ms").integer() < 1 ||
      startup.at("max_wait_ms").integer() > 600000 ||
      value.at("readiness").at("kind") != Value("local-bound"))
    reject("E_READINESS", "startup", "bounded local-bound release is required");
  return result;
}

NodeArguments node_arguments(int argc, char** argv, bool execution) {
  if (execution) detail::bound_process_output();
  NodeArguments result;
  std::map<std::string, std::string> options;
  for (int i = 1; i < argc; ++i) {
    const std::string key(argv[i]);
    if ((key != "--node" && key != "--config" &&
         (!execution || (key != "--release-file" && key != "--release-token"))) ||
        i + 1 == argc || !options.emplace(key, argv[++i]).second)
      reject("E_ARGUMENT", key, "unknown, duplicate or missing option value");
  }
  if (!options.contains("--node") || !options.contains("--config"))
    reject("E_ARGUMENT", "$", "--node ID --config FILE are required");
  result.node = options.at("--node");
  result.config = options.at("--config");
  if (execution) {
    if (!options.contains("--release-file") || !options.contains("--release-token") ||
        options.at("--release-token").size() < 32 || options.at("--release-token").size() > 128)
      reject("E_ARGUMENT", "startup",
             "--release-file and a 32–128 byte --release-token are required");
    result.release_file = options.at("--release-file");
    result.release_token = options.at("--release-token");
  }
  return result;
}

bool await_node_release(const NodeSettings& settings, const NodeArguments& arguments,
                        const std::function<bool()>& stopping) {
  std::cout << "ready node=" << settings.id() << std::endl;
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(settings.resolved.at("startup").at("max_wait_ms").integer());
  while (!stopping()) {
    if (std::filesystem::symlink_status(arguments.release_file).type() !=
        std::filesystem::file_type::not_found) {
      if (read_document(arguments.release_file, 128) != arguments.release_token)
        reject("E_RELEASE_IDENTITY", "startup", "release token does not match this invocation");
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      reject("E_READINESS_TIMEOUT", "startup", "release deadline expired");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}
}  // namespace graphx
