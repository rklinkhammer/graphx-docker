#include "infra/endpoint_resources.hpp"
#include "infra/namespace_resources.hpp"
#include "infra/ovs_resources.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
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
    test_ovs_planning_helpers();
    std::cout << "GraphX resource module tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "resource module test failed: " << error.what() << '\n';
    return 1;
  }
}
