#pragma once

#include "infra/ownership_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace graphx::infra::detail {

std::optional<std::uint64_t> namespace_inode(const std::string& name);
std::string namespace_alias(const OwnershipState& state, std::string_view name);
bool namespace_owned(const OwnedResourceIdentity& item, const OwnershipState& state);
OwnedResourceIdentity create_namespace(const RouterDefinition& router, const OwnershipState& state);
bool delete_owned_namespace(const OwnedResourceIdentity& item, const OwnershipState& state);

}  // namespace graphx::infra::detail
