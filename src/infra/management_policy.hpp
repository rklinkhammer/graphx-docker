#pragma once
#include "infra/ownership_state.hpp"

namespace graphx::infra::detail {
std::string nft_policy_identity(const std::vector<std::string>& command);
void install_management_policy(const GraphConfig& config, OwnershipState& state);
bool management_policy_matches(const GraphConfig& config, const OwnershipState& state,
                               bool allow_absent = false);
}  // namespace graphx::infra::detail
