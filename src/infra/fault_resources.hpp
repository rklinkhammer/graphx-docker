#pragma once

#include "infra/ownership_state.hpp"

#include <string>

namespace graphx::infra::detail {

std::string qdisc_state(const std::string& interface);
OwnedFault create_fault(const ExpectedFault& expected);
bool fault_deadline_elapsed(const OwnedFault& fault);
bool fault_healthy(const OwnedFault& fault);
bool clear_fault(const OwnedFault& fault);

}  // namespace graphx::infra::detail
