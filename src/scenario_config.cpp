#include "config_document.hpp"
#include <algorithm>
#include <set>

namespace graphx::config_internal {
namespace {
void require(bool condition, const std::string& message) {
  if (!condition) reject("E_SCENARIO", "scenario.actions", message);
}
const Value& find(const Value& values, std::string_view key, const Value& id) {
  const auto item =
      std::ranges::find(values.array(), id, [key](const auto& value) { return value.at(key); });
  require(item != values.array().end(), "unknown resource reference");
  return *item;
}
}  // namespace
Value scenario_plan(const GraphConfig& graph) {
  Array actions;
  if (graph.authored.contains("scenario"))
    actions = graph.authored.at("scenario").at("actions").array();
  std::set<std::string> ids;
  for (const auto& action : actions)
    require(ids.insert(action.at("id").text()).second, "duplicate action ID");
  Object credentials;
  if (graph.authored.contains("credentials"))
    credentials = graph.authored.at("credentials").object();
  for (const auto& node : graph.resolved.at("nodes").array()) {
    const auto& ref = node.at("telemetry").at("credential");
    if (!ref.is_null())
      credentials.emplace(ref.text(), Object{{"provider", "runtime-generated"},
                                             {"identity", node.at("node_id")},
                                             {"members", Array{Value("hmac")}}});
  }
  for (const auto& action : actions) {
    const auto kind = action.at("action").text();
    std::set<std::string> allowed{"id", "action"}, required;
    if (kind == "fault") {
      allowed.insert({"attachment", "duration_seconds", "delay_ms", "jitter_ms", "loss_percent"});
      required = {"attachment", "duration_seconds"};
    } else if (kind == "route-apply" || kind == "route-clear") {
      allowed.insert({"router", "destination"});
      required = {"router", "destination"};
    } else if (kind == "traffic") {
      allowed.insert({"connections", "expect"});
      required = {"connections", "expect"};
    } else if (kind == "credential-rotate") {
      allowed.insert({"credential", "next", "grace_seconds", "verify"});
      required = {"credential", "next", "grace_seconds"};
    } else if (kind == "external-simulator") {
      allowed.insert({"address", "attachment", "node", "type", "credentials", "client_credentials",
                      "exclusive_with"});
      required = {"address",     "attachment",         "node",          "type",
                  "credentials", "client_credentials", "exclusive_with"};
    } else
      require(false, "unknown operation");
    for (const auto& [key, value] : action.object()) {
      (void)value;
      require(allowed.contains(key), "field does not belong to operation: " + key);
    }
    for (const auto& key : required)
      require(action.contains(key), "missing operation field: " + key);
    const auto& network = graph.resolved.at("network");
    if (kind == "fault") {
      const auto& attachment = find(network.at("attachments"), "id", action.at("attachment"));
      require(
          attachment.at("kind") != Value("external") && attachment.at("kind") != Value("mirror"),
          "fault needs an owned data attachment");
      require(integer_or(action, "jitter_ms", 0) <= integer_or(action, "delay_ms", 0),
              "jitter exceeds delay");
      require(integer_or(action, "delay_ms", 0) > 0 ||
                  (action.contains("loss_percent") &&
                   std::stod(config_value_json(action.at("loss_percent"), false)) > 0),
              "empty fault");
    } else if (kind.starts_with("route-")) {
      const auto& router = find(network.at("routers"), "id", action.at("router"));
      const auto& route = find(router.at("routes"), "destination", action.at("destination"));
      require(route.contains("install") && route.at("install") == Value("manual"),
              "scenario routes must be declared manual");
    } else if (kind == "traffic") {
      require(!action.at("connections").array().empty() && !action.at("expect").array().empty(),
              "traffic requires connections and expectations");
      std::set<std::string> refs;
      for (const auto& id : action.at("connections").array()) {
        find(graph.resolved.at("connections"), "id", id);
        require(refs.insert(id.text()).second, "duplicate traffic connection");
      }
      for (const auto& expected : action.at("expect").array()) {
        const auto& value = expected.text();
        const std::set<std::string> known{"pass",
                                          "drop",
                                          "bidirectional-tcp-udp",
                                          "vlan-43-isolated",
                                          "capture-nonempty",
                                          "qmp-pause-resume"};
        require(known.contains(value) ||
                    (value.starts_with("pass-only-after-") && ids.contains(value.substr(16))),
                "unknown traffic expectation");
      }
    } else if (kind == "credential-rotate") {
      const auto ref = action.at("credential").text(), next = action.at("next").text();
      require(ref != next && credentials.contains(ref) && credentials.contains(next),
              "unknown or identical rotation reference");
      for (const auto* key : {"provider", "identity", "members"})
        require(credentials.at(ref).at(key) == credentials.at(next).at(key),
                "rotation changes credential identity or roles");
      require(action.at("grace_seconds").integer() <= 60, "rotation overlap exceeds 60 seconds");
      if (action.contains("verify"))
        for (const auto& value : action.at("verify").array())
          require(value == Value("old-and-new-during-grace") ||
                      value == Value("old-rejected-after-grace") ||
                      value == Value("per-node-identity-bound"),
                  "unknown rotation verification");
    } else {
      const auto& node = find(graph.resolved.at("nodes"), "node_id", action.at("node"));
      const auto& attachment = find(network.at("attachments"), "id", action.at("attachment"));
      require(node.at("execution").at("kind") == Value("external") &&
                  node.at("execution").at("address") == action.at("address"),
              "laboratory selection must replace the declared external address");
      require(action.at("type") == Value("sdr.simulator") &&
                  action.at("exclusive_with") == Value("physical-device") &&
                  attachment.at("owner") == action.at("node"),
              "invalid laboratory substitute");
      const auto test_ref = [&](const Value& ref) {
        require(credentials.contains(ref.text()) &&
                    credentials.at(ref.text()).at("provider") == Value("lab-generated"),
                "laboratory requires test credentials");
      };
      test_ref(action.at("credentials"));
      for (const auto& [client, ref] : action.at("client_credentials").object()) {
        find(graph.resolved.at("nodes"), "node_id", Value(client));
        test_ref(ref);
      }
    }
  }
  return Object{{"version", 1},
                {"implicit_start", false},
                {"baseline_config", "resolved.json"},
                {"actions", actions}};
}
void select_laboratory(Value& graph, const std::string& id) {
  require(graph.contains("scenario"), "laboratory action not declared");
  const auto actions = graph.at("scenario").at("actions").array();
  const auto action = find(Value(actions), "id", Value(id));
  require(action.at("action") == Value("external-simulator"),
          "pre-start selection requires external-simulator");
  const auto node = action.at("node").text();
  auto& selected = graph["nodes"][node];
  require(selected.at("execution").at("kind") == Value("external") &&
              selected.at("execution").at("address") == action.at("address"),
          "physical declaration mismatch");
  std::set<std::string> old_refs;
  old_refs.insert(selected.at("credentials").at("control").text());
  selected["execution"] = Object{{"kind", "container"}};
  selected["type"] = action.at("type");
  selected["credentials"]["control"] = action.at("credentials");
  for (const auto& [client, ref] : action.at("client_credentials").object()) {
    old_refs.insert(graph.at("nodes").at(client).at("credentials").at("control").text());
    graph["nodes"][client]["credentials"]["control"] = ref;
  }
  for (auto& attachment : graph["network"]["attachments"].array()) {
    if (attachment.at("id") != action.at("attachment")) continue;
    if (!attachment.contains("switch")) {
      std::set<std::string> switches;
      for (const auto& peer : graph.at("network").at("attachments").array())
        if (peer.contains("network") && peer.at("network") == attachment.at("network") &&
            peer.contains("switch") && peer.at("kind") != Value("external"))
          switches.insert(peer.at("switch").text());
      require(switches.size() == 1, "laboratory attachment requires one unambiguous owned switch");
      attachment["switch"] = *switches.begin();
    }
    attachment["kind"] = "container_veth";
  }
  for (const auto& ref : old_refs) {
    bool used{};
    for (const auto& [name, value] : graph.at("nodes").object()) {
      (void)name;
      if (value.contains("credentials"))
        for (const auto& [role, current] : value.at("credentials").object()) {
          (void)role;
          used = used || current == Value(ref);
        }
    }
    if (!used) graph["credentials"].object().erase(ref);
  }
  auto& remaining = graph["scenario"]["actions"].array();
  std::erase_if(remaining, [&](const auto& value) { return value.at("id") == Value(id); });
}
}  // namespace graphx::config_internal
