#include "config_document.hpp"
#include "graphx/config_schemas.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <regex>

namespace graphx::config_internal {
namespace {
const Value empty_object{Object{}};
const Value empty_array{Array{}};
const Value& member(const Value& value, std::string_view key,
                    const Value& fallback = empty_object) {
  return value.contains(key) ? value.at(key) : fallback;
}
const Array& entries(const Value& value, std::string_view key) {
  return member(value, key, empty_array).array();
}

std::string name_for(std::string_view graph, std::string_view kind, std::string_view id) {
  std::string key(graph);
  key += '\0';
  key += kind;
  key += '\0';
  key += id;
  const char prefix = kind == "bridge"      ? 'b'
                      : kind == "interface" ? 'i'
                      : kind == "peer"      ? 'p'
                      : kind == "namespace" ? 'n'
                                            : 't';
  return "gx" + std::string(1, prefix) + sha256(key).substr(0, 12);
}

std::uint32_t ip(std::string_view text, const std::string& path) {
  in_addr address{};
  if (::inet_pton(AF_INET, std::string(text).c_str(), &address) != 1)
    reject("E_ADDRESS", path, "a numeric IPv4 address is required");
  return ntohl(address.s_addr);
}
struct Cidr {
  std::uint32_t address;
  std::uint32_t mask;
};
Cidr cidr(const std::string& value, const std::string& path) {
  const auto slash = value.find('/');
  if (slash == std::string::npos) reject("E_ADDRESS", path, "an IPv4 CIDR is required");
  const auto prefix = value.substr(slash + 1);
  if (prefix.empty() || prefix.size() > 2 ||
      prefix.find_first_not_of("0123456789") != std::string::npos)
    reject("E_ADDRESS", path, "invalid IPv4 prefix");
  const auto bits = std::stoul(prefix);
  if (bits > 32) reject("E_ADDRESS", path, "IPv4 prefix exceeds 32");
  return {ip(value.substr(0, slash), path), bits == 0 ? 0U : 0xffffffffU << (32U - bits)};
}
std::string address_of(const Value& value) {
  const auto address = value.at("address").text();
  return address.substr(0, address.find('/'));
}

struct Catalog {
  std::string digest;
  Value platform;
  Value wire_schemas;
  std::map<std::string, Value> types;
  std::map<std::string, Value> guests;
};
Catalog load_catalog(const Value& graph, const ConfigLoadOptions& options,
                     const std::filesystem::path& graph_path) {
  const auto root = options.catalog_root.empty() ? std::filesystem::path(GRAPHX_CATALOG_ROOT)
                                                 : options.catalog_root;
  const std::filesystem::path authored_path = graph.at("catalog").text();
  if (authored_path.is_absolute())
    reject("E_PATH_ESCAPE", "catalog", "authored catalog paths must be relative");
  const auto path = confined_path(root, graph_path.parent_path() / authored_path);
  const auto source = read_document(path);
  const auto lock = parse_document(source);
  static const auto locked_shape = parse_document(lock_schema);
  validate_shape(lock, locked_shape, "catalog");
  if (!lock.is_object() || !lock.contains("version") || lock.at("version") != Value(1) ||
      !lock.contains("files") || !lock.at("files").is_array())
    reject("E_CATALOG", "catalog", "invalid catalog lock");
  if (lock.at("files").array().size() > 1024)
    reject("E_BOUND", "catalog", "catalog exceeds 1024 files");
  Catalog catalog;
  catalog.digest = sha256(source);
  std::set<std::string> paths;
  std::size_t bytes{};
  static const auto schema = parse_document(node_type_schema);
  for (const auto& file : lock.at("files").array()) {
    if (!file.is_object() || file.object().size() != 2 || !file.contains("path") ||
        !file.at("path").is_string() || !file.contains("sha256") || !file.at("sha256").is_string())
      reject("E_CATALOG", "catalog.files", "each lock entry requires path and sha256 only");
    const auto relative = file.at("path").text();
    if (std::filesystem::path(relative).is_absolute() || !paths.insert(relative).second)
      reject("E_CATALOG", "catalog.files", "duplicate or absolute catalog path");
    const auto contents = read_document(confined_path(root, path.parent_path() / relative));
    bytes += contents.size();
    if (bytes > 16 * kMaxConfigBytes) reject("E_BOUND", "catalog", "catalog exceeds 16 MiB");
    if (sha256(contents) != file.at("sha256").text())
      reject("E_CATALOG_DIGEST", "catalog." + relative,
             "catalog content does not match its digest");
    const auto data = parse_document(contents);
    if (relative.starts_with("types/")) {
      validate_shape(data, schema, "catalog." + relative);
      for (const auto& [name, port] : data.at("ports").object()) {
        if (port.at("min_connections").integer() > port.at("max_connections").integer())
          reject("E_CATALOG", "catalog." + relative + ".ports." + name,
                 "minimum connections exceeds maximum");
        if (port.at("transports").array().empty())
          reject("E_CATALOG", "catalog." + relative + ".ports." + name,
                 "port requires a transport");
        if (port.at("listener").boolean() != (port.at("direction") == Value("input")))
          reject("E_CATALOG", "catalog." + relative + ".ports." + name,
                 "listener must be an input port");
      }
      for (const auto& [name, definition] : data.at("parameters").object())
        validate_shape(definition.at("default"), definition,
                       "catalog." + relative + ".parameters." + name);
      if (!catalog.types.emplace(data.at("id").text(), data).second)
        reject("E_CATALOG", "catalog", "duplicate type ID");
    } else if (relative.starts_with("guests/")) {
      static const auto guest_shape = parse_document(guest_schema);
      validate_shape(data, guest_shape, "catalog." + relative);
      if (!data.contains("id") || !data.contains("architecture") ||
          !data.contains("accelerators") || !data.contains("outputs"))
        reject("E_GUEST_ARTIFACT", "catalog." + relative, "incomplete guest artifact declaration");
      if (!catalog.guests.emplace(data.at("id").text(), data).second)
        reject("E_CATALOG", "catalog", "duplicate guest ID");
    } else if (relative == "platform.json")
      catalog.platform = data;
    else if (relative == "wire-schemas.json")
      catalog.wire_schemas = data;
  }
  if (catalog.platform.is_null() || catalog.types.empty())
    reject("E_CATALOG", "catalog", "catalog requires platform defaults and types");
  for (const auto& [id, type] : catalog.types)
    for (const auto& [name, port] : type.at("ports").object())
      if (!catalog.wire_schemas.contains(port.at("schema").text()) ||
          catalog.wire_schemas.at(port.at("schema").text()).at("encoding") != port.at("encoding"))
        reject("E_CATALOG", "catalog.types." + id + ".ports." + name,
               "unknown wire schema or mismatched encoding");
  return catalog;
}

void validate_network(const Value& graph) {
  if (!graph.contains("network")) return;
  const auto& net = graph.at("network");
  std::map<std::string, std::vector<Cidr>> subnets;
  std::set<std::string> switches;
  std::set<std::string> ids;
  for (const auto& n : entries(net, "networks")) {
    const auto id = n.at("id").text();
    if (subnets.contains(id)) reject("E_NAME_CONFLICT", "network.networks", "duplicate network ID");
    for (const auto& prefix : n.at("subnets").array()) {
      const auto parsed = cidr(prefix.text(), "network.networks." + id);
      if ((parsed.address & parsed.mask) != parsed.address)
        reject("E_ADDRESS", "network.networks." + id, "subnet must use its network address");
      subnets[id].push_back(parsed);
    }
    if (subnets[id].empty())
      reject("E_ADDRESS", "network.networks." + id, "at least one subnet is required");
    if (n.contains("gateway")) {
      const auto address = ip(n.at("gateway").text(), "network.networks." + id + ".gateway");
      if (std::ranges::none_of(subnets[id],
                               [&](const auto& s) { return (address & s.mask) == s.address; }))
        reject("E_ADDRESS", "network.networks." + id, "gateway is outside subnet");
    }
  }
  for (const auto& sw : entries(net, "switches")) {
    if (!switches.insert(sw.at("id").text()).second)
      reject("E_NAME_CONFLICT", "network.switches", "duplicate switch ID");
    std::set<std::string> ports;
    for (const auto& p : sw.at("ports").array())
      if (!ports.insert(p.at("id").text()).second)
        reject("E_NAME_CONFLICT", "network.switches", "duplicate switch port");
    if (sw.contains("mirror") && !ports.contains(sw.at("mirror").at("output_port").text()))
      reject("E_REFERENCE", "network.switches", "mirror output port is unknown");
  }
  std::set<std::string> address_keys;
  std::size_t index{};
  for (const auto& a : entries(net, "attachments")) {
    const auto path = "network.attachments[" + std::to_string(index++) + "]";
    if (!ids.insert(a.at("id").text()).second)
      reject("E_NAME_CONFLICT", path, "duplicate attachment ID");
    if (a.contains("switch") && !switches.contains(a.at("switch").text()))
      reject("E_REFERENCE", path + ".switch", "unknown switch");
    if (a.at("kind").text() == "mirror") continue;
    if (!a.contains("network") || !subnets.contains(a.at("network").text()))
      reject("E_REFERENCE", path + ".network", "unknown network");
    if (!a.contains("address"))
      reject("E_ADDRESS", path + ".address", "attachment address is required");
    const auto address = cidr(a.at("address").text(), path + ".address");
    if (std::ranges::none_of(subnets.at(a.at("network").text()), [&](const auto& s) {
          return (address.address & s.mask) == s.address && address.mask == s.mask;
        }))
      reject("E_ADDRESS", path + ".address", "attachment prefix does not match network");
    const auto key = a.at("network").text() + ":" + config_value_json(member(a, "vlan"), false) +
                     ":" + address_of(a);
    if (!address_keys.insert(key).second)
      reject("E_ADDRESS_CONFLICT", path + ".address", "duplicate address in segment and VLAN");
    for (const auto& route : entries(a, "routes")) {
      (void)cidr(route.at("destination").text(), path + ".routes");
      if (route.contains("via")) (void)ip(route.at("via").text(), path + ".routes.via");
    }
  }
  std::set<std::string> router_ids;
  for (const auto& router : entries(net, "routers")) {
    const auto id = router.at("id").text();
    const auto path = "network.routers." + id;
    if (!router_ids.insert(id).second) reject("E_NAME_CONFLICT", path, "duplicate router ID");
    std::set<std::string> interfaces, policies, routes;
    for (const auto& interface : router.at("interfaces").array()) {
      if (!interfaces.insert(interface.at("id").text()).second)
        reject("E_NAME_CONFLICT", path + ".interfaces", "duplicate router interface");
      if (!subnets.contains(interface.at("network").text()) ||
          !switches.contains(interface.at("switch").text()))
        reject("E_REFERENCE", path + ".interfaces", "unknown interface network or switch");
      (void)cidr(interface.at("address").text(), path + ".interfaces.address");
    }
    for (const auto& policy : entries(router, "policies")) {
      if (!policies.insert(policy.at("id").text()).second)
        reject("E_NAME_CONFLICT", path + ".policies", "duplicate policy ID");
      (void)cidr(policy.at("source").text(), path + ".policies.source");
      (void)cidr(policy.at("destination").text(), path + ".policies.destination");
    }
    for (const auto& route : entries(router, "routes")) {
      (void)cidr(route.at("destination").text(), path + ".routes.destination");
      if (!routes.insert(route.at("destination").text()).second)
        reject("E_NAME_CONFLICT", path + ".routes", "duplicate route destination");
      if (route.contains("via")) (void)ip(route.at("via").text(), path + ".routes.via");
      if (!route.contains("interface") || !interfaces.contains(route.at("interface").text()))
        reject("E_REFERENCE", path + ".routes.interface", "route requires known interface");
    }
  }
  std::set<std::string> ports_used, capture_ids;
  for (const auto& attachment : entries(net, "attachments")) {
    const auto path = "network.attachments." + attachment.at("id").text();
    const auto owner = attachment.at("owner").text();
    if (!graph.at("nodes").contains(owner) && !router_ids.contains(owner) &&
        !switches.contains(owner))
      reject("E_REFERENCE", path + ".owner", "unknown attachment owner");
    if (attachment.contains("port")) {
      if (!attachment.contains("switch"))
        reject("E_REFERENCE", path + ".switch", "port requires a switch");
      if (!ports_used.insert(attachment.at("switch").text() + "." + attachment.at("port").text())
               .second)
        reject("E_NAME_CONFLICT", path + ".port", "switch port already has an attachment");
    }
    for (const auto& alias : entries(attachment, "aliases")) {
      const auto address = cidr(alias.text(), path + ".aliases");
      const auto key =
          attachment.at("network").text() + ":alias:" + std::to_string(address.address);
      if (!address_keys.insert(key).second)
        reject("E_ADDRESS_CONFLICT", path + ".aliases", "duplicate alias address");
    }
  }
  for (const auto& capture : entries(net, "captures")) {
    if (!capture_ids.insert(capture.at("id").text()).second)
      reject("E_NAME_CONFLICT", "network.captures", "duplicate capture ID");
    if (!ids.contains(capture.at("attachment").text()))
      reject("E_REFERENCE", "network.captures", "unknown capture attachment");
  }
  for (const auto& [edge, hops] : member(net, "edge_paths").object()) {
    if (!graph.at("connections").contains(edge))
      reject("E_REFERENCE", "network.edge_paths." + edge, "unknown connection");
    for (const auto& hop : hops.array()) {
      const auto& id = hop.text();
      if (!graph.at("nodes").contains(id) && !subnets.contains(id) && !switches.contains(id) &&
          !router_ids.contains(id) && !ids.contains(id))
        reject("E_REFERENCE", "network.edge_paths." + edge, "unknown path hop");
    }
  }
}

Value network_value(const Value& graph) {
  Value net = member(graph, "network");
  const auto gid = graph.at("graph").at("id").text();
  for (const auto& key : {"networks", "switches", "routers", "attachments", "captures"})
    if (!net.contains(key)) net[key] = Array{};
  if (!net.contains("edge_paths")) net["edge_paths"] = Object{};
  for (auto& sw : net["switches"].array()) {
    const auto id = sw.at("id").text();
    sw["name"] = name_for(gid, "bridge", id);
    for (auto& port : sw["ports"].array()) {
      port["interface"] = name_for(gid, "interface", id + "." + port.at("id").text());
      port["peer"] = name_for(gid, "peer", id + "." + port.at("id").text());
    }
  }
  for (auto& router : net["routers"].array()) {
    const auto id = router.at("id").text();
    router["namespace"] = name_for(gid, "namespace", id);
    for (auto& interface : router["interfaces"].array()) {
      const auto logical = id + "." + interface.at("id").text();
      interface["device"] = name_for(gid, "interface", logical);
      interface["peer"] = name_for(gid, "peer", logical);
      Object a{{"id", string_or(interface, "attachment_id", id + "-" + interface.at("id").text())},
               {"kind", "namespace_veth"},
               {"owner", id},
               {"network", interface.at("network")},
               {"address", interface.at("address")},
               {"interface", interface.at("device")},
               {"peer", interface.at("peer")},
               {"switch", interface.at("switch")}};
      if (interface.contains("port")) a["port"] = interface.at("port");
      const auto netdef = std::ranges::find_if(net.at("networks").array(), [&](const auto& n) {
        return n.at("id") == interface.at("network");
      });
      if (netdef == net.at("networks").array().end())
        reject("E_REFERENCE", "network.routers." + id, "unknown network");
      if (netdef->at("profile").text().starts_with("ipvlan")) {
        const auto peer = std::ranges::find_if(net.at("attachments").array(), [&](const auto& p) {
          return p.contains("network") && p.at("network") == interface.at("network") &&
                 p.contains("mac");
        });
        if (peer != net.at("attachments").array().end()) a["mac"] = peer->at("mac");
      }
      net["attachments"].array().push_back(a);
    }
    for (auto& route : router["routes"].is_array() ? router["routes"].array()
                                                   : router["routes"].value.emplace<Array>()) {
      if (!route.contains("interface"))
        reject("E_REFERENCE", "network.routers." + id + ".routes",
               "route must select a router interface");
      const auto found = std::ranges::find_if(router.at("interfaces").array(), [&](const auto& i) {
        return i.at("id") == route.at("interface");
      });
      if (found == router.at("interfaces").array().end())
        reject("E_REFERENCE", "network.routers." + id + ".routes", "unknown route interface");
      route["device"] = found->at("device");
    }
  }
  for (auto& a : net["attachments"].array()) {
    const auto kind = a.at("kind").text();
    const auto id = a.at("id").text();
    if (kind == "external") continue;
    if (!a.contains("interface"))
      a["interface"] = name_for(gid, kind == "qemu_tap" ? "tap" : "interface", id);
    if (kind != "qemu_tap" && !a.contains("peer")) a["peer"] = name_for(gid, "peer", id);
    if (kind == "namespace_veth" && graph.at("nodes").contains(a.at("owner").text()))
      a["namespace"] = name_for(gid, "namespace", a.at("owner").text());
    if (a.contains("port")) {
      const auto sw = std::ranges::find_if(
          net["switches"].array(), [&](const auto& s) { return s.at("id") == a.at("switch"); });
      if (sw == net["switches"].array().end())
        reject("E_REFERENCE", "network.attachments." + id, "unknown switch");
      const auto port = std::ranges::find_if(
          (*sw)["ports"].array(), [&](const auto& p) { return p.at("id") == a.at("port"); });
      if (port == (*sw)["ports"].array().end())
        reject("E_REFERENCE", "network.attachments." + id, "unknown switch port");
      (*port)["interface"] = a.at("interface");
      if (a.contains("peer"))
        (*port)["peer"] = a.at("peer");
      else
        port->object().erase("peer");
      if (port->contains("vlan")) a["vlan"] = port->at("vlan");
    }
  }
  for (auto& capture : net["captures"].array()) {
    const auto attachment = std::ranges::find_if(net.at("attachments").array(), [&](const auto& a) {
      return a.at("id") == capture.at("attachment");
    });
    if (attachment == net.at("attachments").array().end())
      reject("E_REFERENCE", "network.captures", "unknown capture attachment");
    capture["directory"] = "/var/lib/graphx/captures/" + gid + "/" + capture.at("id").text();
  }
  net["management_isolation"] = Object{{"per_node_network", true},
                                       {"ip_forward", false},
                                       {"application_bind", "selected-data-address"},
                                       {"platform_routes_data", false},
                                       {"ovs_management_acl",
                                        "only platform TCP 8080 and UDP 9000; deny node-to-node "
                                        "and forwarding; enforced before release barrier"}};
  for (auto& [key, value] : net.object()) {
    if (value.is_array())
      std::ranges::sort(value.array(), [](const auto& a, const auto& b) {
        return a.at("id").text() < b.at("id").text();
      });
    (void)key;
  }
  return net;
}

void validate_target(const Value& graph, const Catalog& catalog, const std::string& target) {
  if (target != "native-linux" && target != "native-macos" && target != "orbstack" &&
      target != "lima")
    reject("E_TARGET_CAPABILITY", "target", "unknown execution target");
  if (graph.contains("network") && (target == "native-macos" || target == "orbstack"))
    reject("E_TARGET_CAPABILITY", "target", "managed OVS requires Linux or Lima");
  for (const auto& [id, node] : graph.at("nodes").object()) {
    if (id == "platform" || id == "prometheus" || id == "grafana" || id.starts_with("mg-"))
      reject("E_NAME_RESERVED", "nodes." + id, "reserved generated name");
    const auto type = catalog.types.find(node.at("type").text());
    if (type == catalog.types.end())
      reject("E_TYPE_UNKNOWN", "nodes." + id + ".type", "unknown catalog type");
    const auto& execution = node.at("execution");
    const auto kind = execution.at("kind").text();
    const std::map<std::string, std::set<std::string>> fields{
        {"container", {"kind"}},
        {"native", {"kind", "ipc_domain"}},
        {"namespace", {"kind", "namespace"}},
        {"qemu", {"kind", "guest", "architecture", "accelerator"}},
        {"external", {"kind", "address", "architecture", "accelerator", "attachment_only"}}};
    for (const auto& [key, value] : execution.object()) {
      (void)value;
      if (!fields.at(kind).contains(key))
        reject("E_SCHEMA", "nodes." + id + ".execution." + key,
               "field is not valid for execution kind");
    }
    if (std::ranges::find(type->second.at("execution").array(), Value(kind)) ==
        type->second.at("execution").array().end())
      reject("E_TARGET_CAPABILITY", "nodes." + id + ".execution",
             "type does not support this execution kind");
    if (kind == "native" && target == "orbstack" && execution.contains("ipc_domain"))
      reject("E_IPC_PLACEMENT", "target", "OrbStack cannot realize native process placement");
    if ((kind == "container" && target == "native-macos") ||
        (kind == "native" && target == "orbstack") ||
        ((kind == "qemu" || kind == "namespace") &&
         (target == "native-macos" || target == "orbstack")))
      reject("E_TARGET_CAPABILITY", "target", "target cannot realize authored placement");
    if (kind == "qemu") {
      const auto ref = string_or(execution, "guest");
      if (!catalog.guests.contains(ref))
        reject("E_GUEST_ARTIFACT", "nodes." + id + ".execution.guest",
               "no pinned guest artifact or build recipe");
      const auto& guest = catalog.guests.at(ref);
      if (!execution.contains("architecture") ||
          execution.at("architecture") != guest.at("architecture"))
        reject("E_GUEST_ARTIFACT", "nodes." + id + ".execution.architecture",
               "guest architecture mismatch");
      if (!execution.contains("accelerator") ||
          std::ranges::find(guest.at("accelerators").array(), execution.at("accelerator")) ==
              guest.at("accelerators").array().end())
        reject("E_TARGET_CAPABILITY", "nodes." + id + ".execution.accelerator",
               "guest accelerator is not supported");
      if (!guest.contains("application") || guest.at("application") != node.at("type"))
        reject("E_GUEST_ARTIFACT", "nodes." + id + ".execution.guest",
               "guest does not implement the node type");
    }
  }
}

Value portable_value(const Value& graph, bool native) {
  if (native || graph.contains("network")) return {};
  if (graph.contains("portable_network")) {
    const auto& portable = graph.at("portable_network");
    const auto subnet = cidr(portable.at("subnet").text(), "portable_network.subnet");
    if ((subnet.address & subnet.mask) != subnet.address)
      reject("E_ADDRESS", "portable_network.subnet", "subnet must use network address");
    std::set<std::uint32_t> used;
    for (const auto& [id, value] : portable.at("addresses").object()) {
      if (!graph.at("nodes").contains(id))
        reject("E_REFERENCE", "portable_network.addresses." + id, "unknown node");
      const auto address = ip(value.text(), "portable_network.addresses." + id);
      if ((address & subnet.mask) != subnet.address || address == subnet.address ||
          address == (subnet.address | ~subnet.mask))
        reject("E_ADDRESS", "portable_network.addresses." + id,
               "address must be a usable host in subnet");
      if (!used.insert(address).second)
        reject("E_ADDRESS_CONFLICT", "portable_network.addresses." + id,
               "duplicate portable address");
    }
    return portable;
  }
  const auto digest = sha256(graph.at("graph").at("id").text());
  const auto octet = std::stoul(digest.substr(0, 4), nullptr, 16) % 128;
  const auto prefix = "172.28." + std::to_string(octet) + ".";
  Object addresses;
  unsigned index = 10;
  for (const auto& [id, node] : graph.at("nodes").object()) {
    (void)node;
    if (index > 253)
      reject("E_ADDRESS", "portable_network", "derived subnet address capacity exceeded");
    addresses[id] = prefix + std::to_string(index++);
  }
  return Object{{"subnet", prefix + "0/24"}, {"addresses", addresses}};
}

std::pair<std::string, std::string> endpoint(const std::string& text) {
  const auto dot = text.find('.');
  return {text.substr(0, dot), text.substr(dot + 1)};
}

Value connection_values(const Value& graph, const Catalog& catalog, const Value& net,
                        const Value& portable, bool native) {
  Array result;
  std::map<std::string, Value> attachments;
  for (const auto& a : entries(net, "attachments")) attachments.emplace(a.at("id").text(), a);
  std::map<std::string, std::size_t> counts;
  std::set<std::string> listeners;
  std::map<std::string, std::vector<std::string>> adjacency;
  std::size_t ordinal{};
  // Diagnose incompatible ports before cardinality, then maxima before missing
  // required peers or incidental endpoint collisions.
  for (const auto& [id, c] : graph.at("connections").object()) {
    const auto [sn, sp] = endpoint(c.at("from").text());
    const auto [dn, dp] = endpoint(c.at("to").text());
    if (!graph.at("nodes").contains(sn) || !graph.at("nodes").contains(dn)) continue;
    const auto& outputs = catalog.types.at(graph.at("nodes").at(sn).at("type").text()).at("ports");
    const auto& inputs = catalog.types.at(graph.at("nodes").at(dn).at("type").text()).at("ports");
    if (!outputs.contains(sp) || !inputs.contains(dp)) continue;
    if (outputs.at(sp).at("schema") != inputs.at(dp).at("schema"))
      reject("E_SCHEMA_MISMATCH", "connections." + id, "port wire schemas must match");
    ++counts[sn + "." + sp];
    ++counts[dn + "." + dp];
  }
  for (const auto& [id, node] : graph.at("nodes").object())
    for (const auto& [name, port] : catalog.types.at(node.at("type").text()).at("ports").object())
      if (counts[id + "." + name] > static_cast<std::size_t>(port.at("max_connections").integer()))
        reject("E_CARDINALITY", "nodes." + id + ".ports." + name,
               "connection count exceeds type maximum");
  counts.clear();
  for (const auto& [id, connection] : graph.at("connections").object()) {
    const auto path = "connections." + id;
    const auto [source, source_port] = endpoint(connection.at("from").text());
    const auto [target, target_port] = endpoint(connection.at("to").text());
    const auto get_port = [&](const std::string& n, const std::string& p,
                              const std::string& side) -> const Value& {
      if (!graph.at("nodes").contains(n))
        reject("E_NODE_UNKNOWN", path + "." + side, "unknown node");
      const auto& ports = catalog.types.at(graph.at("nodes").at(n).at("type").text()).at("ports");
      if (!ports.contains(p)) reject("E_PORT_UNKNOWN", path + "." + side, "unknown type port");
      return ports.at(p);
    };
    const auto& out = get_port(source, source_port, "from");
    const auto& in = get_port(target, target_port, "to");
    const auto transport = connection.at("transport").text();
    if (out.at("direction") != Value("output") || in.at("direction") != Value("input"))
      reject("E_PORT_DIRECTION", path, "connection must join output to input");
    if (out.at("schema") != in.at("schema") || out.at("encoding") != in.at("encoding"))
      reject("E_SCHEMA_MISMATCH", path, "port wire schemas and encodings must match");
    for (const auto* port : {&out, &in})
      if (std::ranges::find(port->at("transports").array(), Value(transport)) ==
          port->at("transports").array().end())
        reject("E_TRANSPORT", path, "type does not support selected transport");
    if (bool_or(connection, "feedback", false) &&
        (!out.at("feedback").boolean() || !in.at("feedback").boolean()))
      reject("E_FEEDBACK_UNSUPPORTED", path + ".feedback", "port does not support feedback");
    ++counts[source + "." + source_port];
    ++counts[target + "." + target_port];
    adjacency[source].push_back(target);
    auto source_address = native ? "127.0.0.1" : source;
    auto destination_address = native ? "127.0.0.1" : target;
    if (!portable.is_null()) {
      if (!portable.at("addresses").contains(source) || !portable.at("addresses").contains(target))
        reject("E_ADDRESS", "portable_network.addresses",
               "every connected node requires an address");
      source_address = portable.at("addresses").at(source).text();
      destination_address = portable.at("addresses").at(target).text();
    }
    if (graph.contains("network") && !connection.contains("attachments"))
      reject("E_ENDPOINT_AMBIGUOUS", path + ".attachments",
             "managed connection requires explicit source and destination attachments");
    if (connection.contains("attachments")) {
      const auto& selected = connection.at("attachments");
      for (const auto& side : {"from", "to"})
        if (!attachments.contains(selected.at(side).text()))
          reject("E_REFERENCE", path + ".attachments." + side, "unknown attachment");
      if (attachments.at(selected.at("from").text()).at("owner") !=
              member(graph.at("nodes").at(source).at("execution"), "namespace", Value(source)) ||
          attachments.at(selected.at("to").text()).at("owner") !=
              member(graph.at("nodes").at(target).at("execution"), "namespace", Value(target)))
        reject("E_REFERENCE", path + ".attachments",
               "selected attachments must belong to connection endpoints");
      source_address = address_of(attachments.at(selected.at("from").text()));
      destination_address = address_of(attachments.at(selected.at("to").text()));
    }
    Value settings = member(connection, "settings");
    const std::map<std::string, std::set<std::string>> allowed{
        {"tcp",
         {"host", "bind", "port", "framing", "connect_timeout_ms", "send_timeout_ms", "reconnect",
          "retry"}},
        {"udp",
         {"destination", "bind", "port", "framing", "mode", "interface", "ttl", "loopback",
          "reuse_address", "max_datagram_bytes", "receive_buffer_bytes", "send_buffer_bytes"}},
        {"shared_memory",
         {"segment", "capacity", "max_message_bytes", "connect_timeout_ms", "send_timeout_ms",
          "backpressure"}}};
    for (const auto& [key, value] : settings.object()) {
      (void)value;
      if (!allowed.at(transport).contains(key))
        reject("E_TRANSPORT", path + ".settings." + key,
               "setting is not valid for selected transport");
    }
    for (const auto& field : {"host", "bind", "destination", "interface"}) {
      if (!settings.contains(field)) continue;
      const auto& text = settings.at(field).text();
      static const std::regex hostname("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$");
      if (!std::regex_match(text, hostname))
        reject("E_TRANSPORT", path + ".settings." + field,
               "endpoint must be a hostname or numeric IPv4 address, not a URI or path");
    }
    if (transport == "shared_memory" && settings.contains("segment")) {
      static const std::regex segment("^[A-Za-z][A-Za-z0-9_-]{0,63}$");
      if (!std::regex_match(settings.at("segment").text(), segment))
        reject("E_TRANSPORT", path + ".settings.segment", "invalid shared-memory identifier");
      if (settings.contains("capacity") && settings.contains("max_message_bytes") &&
          settings.at("capacity").integer() * settings.at("max_message_bytes").integer() >
              268435456)
        reject("E_BOUND", path + ".settings", "shared-memory allocation exceeds 256 MiB");
    }
    const auto graphx_encoding = in.at("encoding") == Value("graphx");
    if (settings.contains("framing") &&
        settings.at("framing") != Value(graphx_encoding ? "u32be" : "none"))
      reject("E_TRANSPORT", path + ".settings.framing", "framing does not match port encoding");
    if (connection.contains("security")) {
      const auto& security = connection.at("security");
      if (security.at("profile") != Value("none") && transport != "tcp")
        reject("E_TRANSPORT", path + ".security", "TLS requires TCP");
      for (const auto& [key, ref] : security.object()) {
        if (key == "profile" || key == "server_name") continue;
        if (!member(graph, "credentials").contains(ref.text()))
          reject("E_CREDENTIAL", path + ".security." + key, "unknown credential reference");
      }
    }
    if (transport == "shared_memory") {
      const auto& a = graph.at("nodes").at(source).at("execution");
      const auto& b = graph.at("nodes").at(target).at("execution");
      if (a.at("kind") != Value("native") || b.at("kind") != Value("native") ||
          !a.contains("ipc_domain") || !b.contains("ipc_domain") ||
          a.at("ipc_domain") != b.at("ipc_domain"))
        reject("E_IPC_PLACEMENT", path, "shared memory requires one explicit native IPC domain");
      for (const auto& key : {"segment", "capacity", "max_message_bytes", "connect_timeout_ms",
                              "send_timeout_ms", "backpressure"})
        if (!settings.contains(key))
          reject("E_SCHEMA", path + ".settings." + key, "shared-memory setting is required");
    } else {
      const auto default_port = in.at("default_port").is_null()
                                    ? 20000 + static_cast<std::int64_t>(ordinal)
                                    : in.at("default_port").integer();
      Value defaults = Object{{"port", default_port},
                              {"bind", destination_address},
                              {"framing", graphx_encoding ? "u32be" : "none"}};
      if (transport == "tcp") {
        defaults["host"] = destination_address;
        defaults["connect_timeout_ms"] = 5000;
        defaults["send_timeout_ms"] = 5000;
        defaults["reconnect"] = true;
        defaults["retry"] =
            Object{{"max_attempts", 60}, {"initial_backoff_ms", 100}, {"max_backoff_ms", 2000}};
      } else {
        defaults["destination"] = destination_address;
        defaults["mode"] = "unicast";
        defaults["ttl"] = 1;
        defaults["loopback"] = true;
        defaults["reuse_address"] = false;
        defaults["max_datagram_bytes"] = 1400;
        defaults["receive_buffer_bytes"] = 65536;
        defaults["send_buffer_bytes"] = 65536;
      }
      settings = merged(defaults, settings);
      if (transport == "tcp" && settings.at("retry").at("initial_backoff_ms").integer() >
                                    settings.at("retry").at("max_backoff_ms").integer())
        reject("E_TRANSPORT", path + ".settings.retry", "initial backoff exceeds maximum backoff");
      if (settings.at("bind") == Value("0.0.0.0") &&
          string_or(settings, "mode", "unicast") == "unicast")
        settings["bind"] = destination_address;
      const auto namespace_key = native ? "native" : target;
      const auto listener = namespace_key + ":" + transport + ":" + settings.at("bind").text() +
                            ":" + std::to_string(settings.at("port").integer());
      if (!listeners.insert(listener).second)
        reject("E_PORT_CONFLICT", path + ".settings.port",
               "listener address and port conflict in namespace");
      if (transport == "udp" && settings.at("mode") != Value("unicast")) {
        if (!settings.contains("interface"))
          reject("E_ENDPOINT_AMBIGUOUS", path + ".settings.interface",
                 "broadcast/multicast sender interface is required");
        (void)ip(settings.at("interface").text(), path + ".settings.interface");
        const auto group = ip(settings.at("destination").text(), path + ".settings.destination");
        if (settings.at("mode") == Value("multicast") && (group & 0xf0000000U) != 0xe0000000U)
          reject("E_TRANSPORT", path + ".settings.destination",
                 "multicast requires an address in 224.0.0.0/4");
        if (settings.at("mode") == Value("broadcast") && group != 0xffffffffU) {
          if (portable.is_null())
            reject("E_ENDPOINT_AMBIGUOUS", path + ".settings.destination",
                   "directed broadcast requires an explicit portable subnet");
          const auto subnet = cidr(portable.at("subnet").text(), "portable_network.subnet");
          if (group != (subnet.address | ~subnet.mask))
            reject("E_TRANSPORT", path + ".settings.destination",
                   "destination is not the selected subnet broadcast address");
        }
      }
    }
    ++ordinal;
    result.emplace_back(
        Object{{"id", id},
               {"from", Object{{"node", source}, {"port", source_port}}},
               {"to", Object{{"node", target}, {"port", target_port}}},
               {"transport", transport},
               {"schema", in.at("schema")},
               {"encoding", in.at("encoding")},
               {"listener", target + "." + target_port},
               {"settings", settings},
               {"source_address", source_address},
               {"destination_address", destination_address},
               {"attachments", member(connection, "attachments")},
               {"security", member(connection, "security", Value(Object{{"profile", "none"}}))}});
  }
  for (const auto& [id, node] : graph.at("nodes").object()) {
    const auto& type = catalog.types.at(node.at("type").text());
    for (const auto& [name, port] : type.at("ports").object()) {
      const auto count = counts[id + "." + name];
      if (count < static_cast<std::size_t>(port.at("min_connections").integer()) ||
          count > static_cast<std::size_t>(port.at("max_connections").integer()))
        reject("E_CARDINALITY", "nodes." + id + ".ports." + name,
               "connection count violates type port minimum/maximum");
    }
  }
  // An edge belongs to a cycle exactly when its destination can reach its source.
  for (const auto& edge : result) {
    const auto from = edge.at("from").at("node").text();
    const auto to = edge.at("to").at("node").text();
    std::set<std::string> seen;
    std::function<bool(const std::string&)> reaches = [&](const auto& node) {
      if (node == from) return true;
      if (!seen.insert(node).second) return false;
      for (const auto& next : adjacency[node])
        if (reaches(next)) return true;
      return false;
    };
    if (reaches(to)) {
      for (const auto& side : {"from", "to"}) {
        const auto& ep = edge.at(side);
        const auto& type =
            catalog.types.at(graph.at("nodes").at(ep.at("node").text()).at("type").text());
        if (!type.at("ports").at(ep.at("port").text()).at("feedback").boolean())
          reject("E_FEEDBACK_UNSUPPORTED", "connections." + edge.at("id").text(),
                 "cycle requires feedback capability on every participating port");
      }
    }
  }
  return result;
}

Value node_values(const Value& graph, const Catalog& catalog, const Value& connections,
                  const Value& platform, bool native) {
  Array nodes;
  const auto gid = graph.at("graph").at("id").text();
  for (const auto& [id, node] : graph.at("nodes").object()) {
    const auto& type = catalog.types.at(node.at("type").text());
    Object parameters;
    for (const auto& [key, definition] : type.at("parameters").object())
      parameters[key] = definition.at("default");
    for (const auto& [key, value] : member(node, "parameters").object()) {
      if (!type.at("parameters").contains(key))
        reject("E_PARAMETER", "nodes." + id + ".parameters." + key, "unknown type parameter");
      validate_shape(value, type.at("parameters").at(key), "nodes." + id + ".parameters." + key);
      parameters[key] = value;
    }
    for (const auto& [key, reference] : member(node, "credentials").object()) {
      if (std::ranges::find(type.at("credentials").array(), Value(key)) ==
          type.at("credentials").array().end())
        reject("E_CREDENTIAL", "nodes." + id + ".credentials." + key, "unknown credential role");
      if (!member(graph, "credentials").contains(reference.text()))
        reject("E_CREDENTIAL", "nodes." + id + ".credentials." + key,
               "unknown credential reference");
    }
    for (const auto& role : type.at("credentials").array())
      if (!member(node, "credentials").contains(role.text()))
        reject("E_CREDENTIAL", "nodes." + id + ".credentials",
               "required credential role is missing");
    Object bindings;
    for (const auto& [name, port] : type.at("ports").object()) {
      (void)port;
      bindings[name] = Array{};
    }
    for (const auto& connection : connections.array()) {
      for (const auto& side : {"from", "to"}) {
        if (connection.at(side).at("node") != Value(id)) continue;
        Object binding{{"connection", connection.at("id")},
                       {"role", std::string_view(side) == "from" ? "connect" : "listen"}};
        for (const auto& key : {"transport", "schema", "encoding", "settings", "source_address",
                                "destination_address", "security", "attachments"})
          binding[key] = connection.at(key);
        bindings[connection.at(side).at("port").text()].array().emplace_back(binding);
      }
    }
    const bool emitting = node.at("execution").at("kind") != Value("external") &&
                          type.at("observation") == Value("graphx");
    Value capture = platform.at("capture");
    capture["directory"] = native ? "${GX_STATE}/captures/" + id : "/captures";
    nodes.emplace_back(
        Object{{"contract_version", 2},
               {"graph_version", 3},
               {"graph_id", gid},
               {"node_id", id},
               {"type", node.at("type")},
               {"type_revision", type.at("revision")},
               {"execution", node.at("execution")},
               {"parameters", parameters},
               {"control", string_or(type, "control", "none")},
               {"bindings", bindings},
               {"observation", member(node, "observation", Value(Array{type.at("observation")}))},
               {"telemetry", Object{{"host", native ? "127.0.0.1" : "platform"},
                                    {"port", 9000},
                                    {"credential", emitting ? Value("runtime-" + id) : Value{}}}},
               {"credentials", member(node, "credentials")},
               {"readiness", type.at("readiness")},
               {"capture", capture},
               {"startup",
                Object{{"bind_before_connect", true},
                       {"release_barrier", node.at("execution").at("kind") != Value("external")},
                       {"max_wait_ms", 30000}}}});
  }
  return nodes;
}

void project_runtime_types(GraphConfig& config, const Catalog& catalog) {
  // Read-only projections for existing transport and resource APIs. Execution of
  // authored graphs remains explicitly unavailable until the corresponding adapter.
  for (const auto& [id, type] : catalog.types) {
    NodeTypeDefinition definition;
    definition.id = id;
    definition.revision = static_cast<std::uint32_t>(type.at("revision").integer());
    definition.image = type.at("image").text();
    definition.executable = type.at("executable").text();
    for (const auto& [name, port] : type.at("ports").object()) {
      TypePortCapability capability;
      capability.port = {
          name, port.at("direction") == Value("input") ? Direction::input : Direction::output,
          port.at("schema").text()};
      for (const auto& t : port.at("transports").array()) capability.transports.push_back(t.text());
      capability.encoding = port.at("encoding").text();
      capability.minimum_connections =
          static_cast<std::size_t>(port.at("min_connections").integer());
      capability.maximum_connections =
          static_cast<std::size_t>(port.at("max_connections").integer());
      capability.feedback = port.at("feedback").boolean();
      definition.ports.push_back(std::move(capability));
    }
    config.node_types.push_back(std::move(definition));
  }
  for (const auto& node : config.resolved.at("nodes").array()) {
    NodeConfig projected;
    projected.id = node.at("node_id").text();
    projected.kind = node.at("type").text();
    const auto execution = node.at("execution").at("kind").text();
    projected.execution = execution;
    projected.runtime = execution == "container"                            ? "docker"
                        : execution == "native" || execution == "namespace" ? "process"
                                                                            : execution;
    projected.lifecycle = execution == "external" ? "external" : "managed";
    projected.accelerator = string_or(node.at("execution"), "accelerator");
    projected.architecture = string_or(node.at("execution"), "architecture");
    const auto& type = catalog.types.at(projected.kind);
    for (const auto& [name, port] : type.at("ports").object())
      projected.ports.push_back(
          {name, port.at("direction") == Value("input") ? Direction::input : Direction::output,
           port.at("schema").text()});
    config.nodes.push_back(std::move(projected));
  }
  for (const auto& connection : config.resolved.at("connections").array()) {
    EdgeConfig edge;
    edge.edge.id = connection.at("id").text();
    edge.edge.from_node = connection.at("from").at("node").text();
    edge.edge.from_port = connection.at("from").at("port").text();
    edge.edge.to_node = connection.at("to").at("node").text();
    edge.edge.to_port = connection.at("to").at("port").text();
    edge.transport = config_internal::resolved_transport(connection);
    edge.data_plane = connection.at("encoding") == Value("raw") ? "external" : "graphx";
    config.edges.push_back(std::move(edge));
  }
}
}  // namespace
}  // namespace graphx::config_internal

namespace graphx {
GraphConfig load_graph(const std::filesystem::path& path, const ConfigLoadOptions& options) {
  using namespace config_internal;
  const auto source = read_document(path);
  const auto graph = parse_document(source);
  if (!graph.is_object() || !graph.contains("version") || graph.at("version") != Value(3))
    reject("E_VERSION", "version", "only configuration version 3 is accepted");
  static const auto schema = parse_document(authored_schema);
  validate_shape(graph, schema);
  const auto catalog = load_catalog(graph, options, path);
  validate_target(graph, catalog, options.target);
  validate_network(graph);
  const bool native = !graph.at("nodes").object().empty() &&
                      std::ranges::all_of(graph.at("nodes").object(), [](const auto& pair) {
                        return pair.second.at("execution").at("kind") == Value("native");
                      });
  const auto network = network_value(graph);
  auto expanded = graph;
  expanded["network"] = network;
  if (graph.contains("network")) validate_network(expanded);
  const auto portable = portable_value(graph, native);
  const auto connections = connection_values(graph, catalog, network, portable, native);
  Value platform = merged(catalog.platform, member(graph, "platform"));
  platform["history"]["database_file"] =
      native ? "${GX_STATE}/history/history.sqlite" : "/var/lib/graphx/history/history.sqlite";
  platform["capture"]["directory"] = native ? "${GX_STATE}/captures" : "/captures";
  platform["telemetry"]["host"] = native ? "127.0.0.1" : "platform";
  platform["control"]["allowed_origins"] = Array{
      Value("http://127.0.0.1:" + std::to_string(platform.at("console").at("port").integer()))};
  platform["normalization"] = "resolved.json";
  platform["credential_manifest"] = "credentials.json";
  if (graph.contains("network")) {
    platform["capture"]["enabled"] = !network.at("captures").array().empty();
    platform["capture"]["provider"] = "ovs-span";
  }
  if (platform.at("otlp").at("enabled").boolean()) {
    const auto& otlp = platform.at("otlp");
    for (const auto& key : {"endpoint", "mode", "credentials"})
      if (!otlp.contains(key))
        reject("E_SCHEMA", std::string("platform.otlp.") + key,
               "enabled OTLP requires endpoint, mode and credentials");
    if (!member(graph, "credentials").contains(otlp.at("credentials").text()))
      reject("E_CREDENTIAL", "platform.otlp.credentials", "unknown credential reference");
    const auto endpoint = otlp.at("endpoint").text();
    if (endpoint.find('@') != std::string::npos || endpoint.find('#') != std::string::npos ||
        endpoint.find('?') != std::string::npos || endpoint.size() <= 8)
      reject("E_SCHEMA", "platform.otlp.endpoint",
             "OTLP requires an HTTPS endpoint without embedded credentials, query or fragment");
  }
  for (const auto& grant : platform.at("control").at("grants").array()) {
    if (!member(graph, "credentials").contains(grant.at("credential").text()))
      reject("E_CREDENTIAL", "platform.control.grants", "unknown grant credential");
    for (const auto& id : grant.at("nodes").array())
      if (!graph.at("nodes").contains(id.text()))
        reject("E_REFERENCE", "platform.control.grants", "unknown granted node");
  }
  const auto nodes = node_values(graph, catalog, connections, platform, native);
  std::set<std::string> credentials{"observer"};
  for (const auto& [id, value] : member(graph, "credentials").object()) {
    (void)value;
    credentials.insert(id);
  }
  for (const auto& node : nodes.array())
    if (!node.at("telemetry").at("credential").is_null())
      credentials.insert(node.at("telemetry").at("credential").text());
  Array credential_references;
  for (const auto& id : credentials) credential_references.emplace_back(id);
  GraphConfig config;
  config.version = 3;
  config.id = graph.at("graph").at("id").text();
  config.authored = graph;
  config.resolved = Object{
      {"contract_version", 2},
      {"graph_version", 3},
      {"graph_id", config.id},
      {"target", options.target},
      {"catalog_digest", catalog.digest},
      {"input_digest", sha256(config_value_json(graph, false))},
      {"nodes", nodes},
      {"connections", connections},
      {"network", network},
      {"portable_network", portable},
      {"platform", platform},
      {"credential_references", credential_references},
      {"ordering", "lexicographic-ascii"},
      {"execution_values", Array{Value("GX_STATE"), Value("GX_CREDENTIALS"), Value("GX_OUTPUT"),
                                 Value("GX_RELEASE"), Value("GX_OWNER")}}};
  static const auto output_schema = parse_document(normalized_schema);
  validate_shape(config.resolved, output_schema);
  if (config_value_json(config.resolved).size() > 4 * kMaxConfigBytes)
    reject("E_BOUND", "$", "normalized contract exceeds 4 MiB");
  project_runtime_types(config, catalog);
  return config;
}
}  // namespace graphx
