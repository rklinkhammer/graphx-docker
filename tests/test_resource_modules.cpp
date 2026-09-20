#include "graphx/node_settings.hpp"
#include "infra/endpoint_resources.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/management_policy.hpp"
#include "infra/ovs_resources.hpp"
#include "infra/compose_execution.hpp"

#include <iostream>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_network_barrier() {
  auto directory = std::filesystem::temp_directory_path() /
                   ("graphx-network-barrier-" + std::to_string(getpid()));
  std::filesystem::create_directory(directory);
  graphx::NodeArguments args;
  args.release_file = directory / "release";
  args.release_token = "owned-token";
  const auto marker = directory / "network-ready";
  require(!graphx::await_node_network(args, [] { return true; }), "interruption ignored");
  for (const auto& token : {std::string("foreign-token"), std::string(129, 'x')}) {
    {
      std::ofstream file(marker);
      file << token;
    }
    bool rejected = false;
    try {
      graphx::await_node_network(args, [] { return false; });
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, "invalid network barrier accepted");
  }
  {
    std::ofstream file(marker);
    file << args.release_token;
  }
  require(graphx::await_node_network(args, [] { return false; }), "owned barrier rejected");
  std::filesystem::remove_all(directory);
}

void test_compose_recorder_capabilities() {
  using Value = graphx::ConfigValue;
  using Array = Value::Array;
  using Object = Value::Object;
  using graphx::infra::detail::validate_compose_service_fields;
  const Value recorder = Object{{"node_id", "recorder"},
                                {"type", "vita.recorder"},
                                {"execution", Object{{"kind", "container"}}},
                                {"recorder", Object{{"interface", "mirror"}, {"mtu", 9000}}}};
  const Value resolved = Object{{"nodes", Array{recorder}}};
  const Value service = Object{{"cap_drop", Array{"ALL"}}, {"cap_add", Array{"NET_RAW"}}};
  validate_compose_service_fields("recorder", service, resolved);
  validate_compose_service_fields("platform", Object{{"cap_drop", Array{"ALL"}}}, resolved);
  const auto rejects = [&](const std::string& name, const Value& candidate, const Value& graph) {
    bool rejected = false;
    try {
      validate_compose_service_fields(name, candidate, graph);
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).starts_with("E_COMPOSE_SECURITY:");
    }
    require(rejected, "unsafe Compose capability policy accepted");
  };
  for (const Value& capabilities :
       {Value(Array{}), Value(Array{"NET_ADMIN"}), Value(Array{"NET_RAW", "NET_ADMIN"}),
        Value(Array{"NET_RAW", "NET_RAW"}), Value("NET_RAW"), Value()}) {
    auto bad = service;
    bad["cap_add"] = capabilities;
    rejects("recorder", bad, resolved);
  }
  rejects("recorder", Object{{"cap_drop", Array{"ALL"}}}, resolved);
  for (const auto* name : {"platform", "prometheus", "grafana", "unknown"})
    rejects(name, service, resolved);
  auto ordinary = recorder;
  ordinary["type"] = "sample.sink";
  rejects("recorder", service, Object{{"nodes", Array{ordinary}}});
  ordinary = recorder;
  ordinary.object().erase("recorder");
  rejects("recorder", service, Object{{"nodes", Array{ordinary}}});
  ordinary = recorder;
  ordinary["execution"]["kind"] = "native";
  rejects("recorder", service, Object{{"nodes", Array{ordinary}}});
  for (const auto* key : {"privileged", "network_mode", "build"}) {
    auto bad = service;
    bad[key] = true;
    rejects("recorder", bad, resolved);
  }
}

void test_diagnostic_independence() {
  using namespace graphx::infra::detail;
  ExpectedEndpoint recorder;
  recorder.kind = graphx::AttachmentKind::mirror;
  recorder.mirror_container = true;
  recorder.owner = "recorder";
  recorder.network_switch = "bridge";
  recorder.container_id = std::string(64, 'a');
  recorder.namespace_inode = 20;
  recorder.mtu = 9000;
  const auto diagnostic = diagnostic_mirror_endpoint(recorder, "graph-a", "capture", 10);
  require(!diagnostic.mirror_container && diagnostic.container_id.empty() &&
              diagnostic.namespace_inode == 10 && diagnostic.owner != recorder.owner,
          "diagnostic delivery must not depend on recorder identity or namespace");
  require(diagnostic.mtu == 9000 && diagnostic.network_switch == "bridge" &&
              diagnostic.host_interface != diagnostic.target_interface &&
              diagnostic.host_interface.size() <= 15,
          "diagnostic mirror must preserve path and interface bounds");
  require(diagnostic_mirror_endpoint(recorder, "graph-b", "capture", 10).host_interface !=
              diagnostic.host_interface,
          "independent graphs must not collide");
  require(diagnostic_mirror_endpoint(recorder, "graph-a", "other", 10).host_interface !=
              diagnostic.host_interface,
          "independent captures must not collide");
  recorder.namespace_inode = 0;
  recorder.container_id.clear();
  require(diagnostic_mirror_endpoint(recorder, "graph-a", "capture", 10).host_interface ==
              diagnostic.host_interface,
          "recorder death must not replace diagnostic identity");
  bool rejected = false;
  try {
    static_cast<void>(diagnostic_mirror_endpoint(recorder, "graph-a", "capture", 0));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "missing host namespace must fail closed");
}

void test_policy_identity() {
  const std::string initial =
      R"({"nftables":[{"metainfo":{"version":"1"}},{"rule":{"handle":1,"expr":[{"counter":{"packets":1,"bytes":1}},{"accept":null}]}}]})";
  const std::string counted =
      R"({"nftables":[{"metainfo":{"version":"2"}},{"rule":{"handle":7,"expr":[{"counter":{"packets":77,"bytes":999}},{"accept":null}]}}]})";
  const std::string replaced = R"({"nftables":[{"rule":{"expr":[{"counter":{}},{"drop":null}]}}]})";
  const auto digest = [](const auto& json) {
    return graphx::infra::detail::nft_policy_identity({"printf", "%s", json});
  };
  require(digest(initial) == digest(counted),
          "counters and kernel handles are not policy identity");
  require(digest(initial) != digest(replaced), "policy replacement must change identity");
}
void test_mirror_query_scope() {
  std::array<char, 64> pattern{};
  const std::string prefix = "/tmp/graphx-ovs-query-XXXXXX";
  std::copy(prefix.begin(), prefix.end(), pattern.begin());
  const auto* created = ::mkdtemp(pattern.data());
  require(created, "temporary query fixture");
  const std::filesystem::path root = created;
  const std::string previous = std::getenv("PATH") ? std::getenv("PATH") : "";
  try {
    const auto tool = root / "ovs-vsctl";
    std::ofstream file(tool);
    file << R"(#!/usr/bin/env python3
import sys, json
# Two graphs deliberately share the same logical attachment ID.
rows = [('owner-a', '11111111-1111-1111-1111-111111111111'),
        ('owner-b', '22222222-2222-2222-2222-222222222222')]
assert sys.argv[1:6] == ['--data=bare', '--no-heading', '--columns=_uuid', 'find', 'Mirror']
assert json.loads(sys.argv[6].split('=', 1)[1]) in ('mirror', 'diagnostic:diagnostic')
for owner, identity in rows:
    if len(sys.argv) == 7 or 'external_ids:graphx_owner=' + owner in sys.argv[7:]:
        print(identity)
)";
    file.close();
    std::filesystem::permissions(tool, std::filesystem::perms::owner_all);
    const auto search = root.string() + ":" + previous;
    require(::setenv("PATH", search.c_str(), 1) == 0, "query fixture PATH");
    using graphx::infra::detail::ovs_find_uuid;
    require(ovs_find_uuid("Mirror", "external_ids:graphx_attachment", "mirror", "owner-a") ==
                "11111111-1111-1111-1111-111111111111",
            "mirror query escaped owner a");
    require(ovs_find_uuid("Mirror", "external_ids:graphx_attachment", "mirror", "owner-b") ==
                "22222222-2222-2222-2222-222222222222",
            "mirror query escaped owner b");
    require(ovs_find_uuid("Mirror", "external_ids:graphx_attachment", "mirror", "foreign").empty(),
            "foreign mirror adopted");
    require(ovs_find_uuid("Mirror", "external_ids:graphx_attachment", "diagnostic:diagnostic",
                          "owner-a") == "11111111-1111-1111-1111-111111111111",
            "diagnostic identity was not quoted");
    std::ofstream recovery(tool);
    recovery << R"(#!/usr/bin/env python3
import json, sys
from pathlib import Path
mode = Path(__file__).with_name('mode').read_text()
identity = '11111111-1111-1111-1111-111111111111'
if mode == 'unavailable': sys.exit(1)
if sys.argv[1] == '--if-exists':
    assert sys.argv[2:] == ['get', 'Mirror', identity, '_uuid']
    if mode not in ('absent', 'replacement'): print(identity)
elif sys.argv[1] == 'get':
    assert sys.argv[2:4] == ['Mirror', identity]
    print('graph' if sys.argv[4].endswith('graph') else ('foreign' if mode == 'wrong-hash' else 'hash'))
else:
    assert sys.argv[1:6] == ['--data=bare', '--no-heading', '--columns=_uuid', 'find', 'Mirror']
    condition = sys.argv[6]
    if condition.startswith('name='): assert json.loads(condition[5:]) == 'gxmirror'
    if condition.startswith('external_ids:graphx_owner='):
        assert json.loads(sys.argv[7].split('=', 1)[1]) == 'diagnostic:diagnostic'
    if mode == 'absent': pass
    elif mode == 'renamed' and condition.startswith('name='): pass
    elif mode == 'foreign' and condition.startswith('external_ids:graphx_owner='): pass
    elif mode == 'replacement': print('22222222-2222-2222-2222-222222222222')
    else:
        print(identity)
        if mode == 'duplicate': print('22222222-2222-2222-2222-222222222222')
)";
    recovery.close();
    graphx::infra::detail::OwnershipState state;
    state.owner_token = "owner-a";
    state.graph_id = "graph";
    state.config_hash = "hash";
    graphx::OwnedResourceIdentity endpoint;
    endpoint.name = "gxmirror";
    endpoint.attachment_id = "diagnostic:diagnostic";
    endpoint.route_identity = "11111111-1111-1111-1111-111111111111";
    for (const auto* mode : {"owned", "absent", "unavailable", "renamed", "foreign", "replacement",
                             "duplicate", "wrong-hash"}) {
      {
        std::ofstream selected(root / "mode");
        selected << mode;
      }
      const bool safe = std::string(mode) == "owned" || std::string(mode) == "absent";
      require(graphx::infra::detail::mirror_absent_or_owned(endpoint, state) == safe,
              "mirror recovery did not distinguish absence from replacement or query failure");
    }
  } catch (...) {
    ::setenv("PATH", previous.c_str(), 1);
    std::filesystem::remove_all(root);
    throw;
  }
  ::setenv("PATH", previous.c_str(), 1);
  std::filesystem::remove_all(root);
}

void test_owned_names() {
  graphx::infra::detail::OwnershipState state;
  state.owner_token = "0123456789abcdef0123456789abcdef";
  require(graphx::infra::detail::namespace_alias(state, "router") ==
              "graphx:0123456789abcdef0123456789abcdef:netns:router",
          "namespace ownership alias changed");
  require(graphx::infra::detail::endpoint_alias(state, "worker-data", "host") ==
              "graphx:0123456789abcdef0123456789abcdef:worker-data:host",
          "endpoint ownership alias changed");
  require(graphx::infra::detail::tap_owner_identity(65532, 65532) == "65532:65532",
          "TAP owner identity changed");
}

void test_ovs_planning_helpers() {
  graphx::SwitchDefinition network_switch;
  network_switch.id = "br-test";
  const auto command = graphx::infra::detail::planned_create(network_switch, "test-graph",
                                                             "owner-token", "configuration-hash");
  require(command ==
              "ovs-vsctl -- add-br br-test -- set Bridge br-test datapath_type=system "
              "external_ids:graphx_owner=owner-token "
              "external_ids:graphx_config_hash=configuration-hash "
              "external_ids:graphx_graph=test-graph",
          "OVS bridge plan changed");
  require(graphx::infra::detail::address_host("10.41.2.7/24") == "10.41.2.7",
          "profile address normalization changed");
}

}  // namespace

int main() {
  try {
    test_owned_names();
    test_network_barrier();
    test_compose_recorder_capabilities();
    test_diagnostic_independence();
    test_mirror_query_scope();
    test_policy_identity();
    test_ovs_planning_helpers();
    std::cout << "GraphX resource module tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "resource module test failed: " << error.what() << '\n';
    return 1;
  }
}
