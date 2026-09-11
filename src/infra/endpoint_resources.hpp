#pragma once

#include "infra/ownership_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace graphx::infra::detail {

struct ResolvedContainer {
  std::string id;
  std::uint32_t pid{};
  std::uint64_t namespace_inode{};
};

ResolvedContainer resolve_container(const GraphConfig& config, std::string_view owner);
std::optional<std::uint32_t> link_ifindex(const std::string& name);
std::string link_alias(const std::string& name);
std::string endpoint_alias(const OwnershipState& state, std::string_view attachment,
                           std::string_view side);
std::string tap_owner_identity(std::uint32_t uid, std::uint32_t gid);
bool tap_owner_matches(const ExpectedEndpoint& expected);
const ExpectedEndpoint& expected_endpoint(const OwnershipState& state, std::string_view id);
bool ovs_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool host_endpoint_owned(const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool endpoint_names_absent_or_recorded(const OwnedResourceIdentity& endpoint);
bool delete_owned_endpoint(const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool tap_endpoint_healthy(const ExpectedEndpoint& expected, const OwnedResourceIdentity& endpoint,
                          const OwnershipState& state);
bool container_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state,
                                const GraphConfig& config);
bool namespace_endpoint_healthy(const ExpectedEndpoint& expected,
                                const OwnedResourceIdentity& endpoint, const OwnershipState& state);
bool mirror_endpoint_healthy(const ExpectedEndpoint& expected,
                             const OwnedResourceIdentity& endpoint, const OwnershipState& state);
void check_endpoint_collision(const ExpectedEndpoint& endpoint, std::uint32_t pid);
OwnedResourceIdentity create_endpoint(const ExpectedEndpoint& endpoint, std::uint32_t pid,
                                      const OwnershipState& state);
OwnedResourceIdentity create_namespace_endpoint(const ExpectedEndpoint& endpoint,
                                                const OwnershipState& state);
OwnedResourceIdentity create_tap_endpoint(const ExpectedEndpoint& endpoint,
                                          const OwnershipState& state);
OwnedResourceIdentity create_mirror_endpoint(const ExpectedEndpoint& endpoint,
                                             const OwnershipState& state);

}  // namespace graphx::infra::detail
