#pragma once

#include "infra/ownership_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace graphx::infra::detail {

// Host-owned diagnostic delivery survives the recorder namespace lifetime.
ExpectedEndpoint diagnostic_mirror_endpoint(const ExpectedEndpoint& recorder,
                                            std::string_view graph, std::string_view capture,
                                            std::uint64_t host_namespace);

struct ResolvedContainer {
  std::string id;
  std::uint32_t pid{};
  std::uint64_t namespace_inode{};
};

// Reconstruct deployment bindings from the identity-checked compiled ledger.
// Shared by ordinary lifecycle and scenario ownership validation.
void bind_owned_container_services(GraphConfig& config, const OwnershipState& state);

ResolvedContainer resolve_owned_container(const GraphConfig& config, std::string_view owner,
                                          const OwnershipState& state);
ResolvedContainer resolve_container(const GraphConfig& config, std::string_view owner);
std::optional<std::uint32_t> link_ifindex(const std::string& name);
std::optional<std::uint32_t> link_peer_ifindex(const std::string& name);
std::string link_alias(const std::string& name);
std::string endpoint_alias(const OwnershipState& state, std::string_view attachment,
                           std::string_view side);
std::string tap_owner_identity(std::uint32_t uid, std::uint32_t gid);
bool tap_owner_matches(const ExpectedEndpoint& expected);
const ExpectedEndpoint& expected_endpoint(const OwnershipState& state, std::string_view id);
bool ovs_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool host_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool endpoint_names_absent_or_recorded(const OwnedResourceIdentity& endpoint);
bool delete_owned_endpoint(const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                           const GraphConfig* config = nullptr);
bool tap_endpoint_healthy(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint,
                          const OwnershipState& state);
bool container_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                                const GraphConfig& config);
bool namespace_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool mirror_peer_link_matches(std::string_view link, std::uint32_t index, std::string_view alias);
bool jumbo_offloads_disabled(std::string_view features);
bool passive_mirror_filter_matches(std::string_view json);
bool mirror_peer_owned(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint,
                       const OwnershipState& state, const GraphConfig& config);
bool mirror_endpoint_healthy(const ExpectedEndpoint& expected,
                             const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                             const GraphConfig* config = nullptr);
void check_endpoint_collision(const ExpectedEndpoint& endpoint, std::uint32_t pid);
OwnedResourceIdentity create_endpoint(const ExpectedEndpoint& endpoint, std::uint32_t pid,
                                      const OwnershipState& state);
OwnedResourceIdentity create_namespace_endpoint(const ExpectedEndpoint& endpoint,
                                                const OwnershipState& state);
OwnedResourceIdentity create_tap_endpoint(const ExpectedEndpoint& endpoint,
                                          const OwnershipState& state);
OwnedResourceIdentity create_mirror_endpoint(const ExpectedEndpoint& endpoint,
                                             const OwnershipState& state, std::uint32_t pid = 0);

}  // namespace graphx::infra::detail
