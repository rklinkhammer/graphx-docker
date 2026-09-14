#pragma once

#include "infra/ownership_state.hpp"

#include <string>
#include <functional>

namespace graphx::infra::detail {

std::string qdisc_state(const std::string& interface);
OwnedFault create_fault(const ExpectedFault& expected,
                        const std::function<void(const OwnedFault&)>& register_identity = {});
bool fault_deadline_elapsed(const OwnedFault& fault);
bool fault_healthy(const OwnedFault& fault);
bool clear_fault(const OwnedFault& fault);

}  // namespace graphx::infra::detail
