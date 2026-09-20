#include "infra/endpoint_resources.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/management_policy.hpp"
#include "infra/ovs_resources.hpp"

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
import sys
# Two graphs deliberately share the same logical attachment ID.
rows = [('owner-a', '11111111-1111-1111-1111-111111111111'),
        ('owner-b', '22222222-2222-2222-2222-222222222222')]
assert sys.argv[1:7] == ['--data=bare', '--no-heading', '--columns=_uuid', 'find', 'Mirror', 'external_ids:graphx_attachment=mirror']
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
