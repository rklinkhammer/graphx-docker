#include "infra/namespace_resources.hpp"

#include "infra/command_runner.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace graphx::infra::detail {
namespace {
int run(const std::vector<std::string>& arguments, std::string* captured = nullptr) {
  CommandOptions options;
  options.arguments = arguments;
  options.capture_output = captured != nullptr;
  auto result = run_command(options);
  if (captured) *captured = std::move(result.output);
  if (result.output_truncated) throw std::runtime_error("command output exceeded safety limit");
  return result.status;
}
}  // namespace

std::optional<std::uint64_t> namespace_inode(const std::string& name) {
  std::string value;
  if (run({"ip", "netns", "exec", name, "stat", "-Lc", "%i", "/proc/self/ns/net"}, &value) != 0)
    return std::nullopt;
  try {
    std::size_t consumed{};
    const auto inode = std::stoull(value, &consumed);
    if (consumed != value.size() || inode == 0) return std::nullopt;
    return inode;
  } catch (...) {
    return std::nullopt;
  }
}

std::string namespace_alias(const OwnershipState& state, std::string_view name) {
  return "graphx:" + state.owner_token + ":netns:" + std::string(name);
}

bool namespace_owned(const OwnedResourceIdentity& item, const OwnershipState& state) {
  const auto inode = namespace_inode(item.name);
  if (!inode || !item.namespace_inode || *inode != *item.namespace_inode) return false;
  std::string loopback;
  return run({"ip", "netns", "exec", item.name, "ip", "-d", "link", "show", "dev", "lo"},
             &loopback) == 0 &&
         loopback.find(namespace_alias(state, item.name)) != std::string::npos;
}

OwnedResourceIdentity create_namespace(const RouterDefinition& router,
                                       const OwnershipState& state) {
  if (namespace_inode(router.namespace_name))
    throw std::runtime_error("refusing unowned Linux namespace collision: " +
                             router.namespace_name);
  if (run({"ip", "netns", "add", router.namespace_name}) != 0 ||
      run({"ip", "netns", "exec", router.namespace_name, "ip", "link", "set", "dev", "lo", "alias",
           namespace_alias(state, router.namespace_name)}) != 0 ||
      run({"ip", "netns", "exec", router.namespace_name, "ip", "link", "set", "dev", "lo", "up"}) !=
          0)
    throw std::runtime_error("cannot create owned Linux namespace " + router.namespace_name);
  const auto inode = namespace_inode(router.namespace_name);
  if (!inode) throw std::runtime_error("cannot capture Linux namespace identity");
  OwnedResourceIdentity identity;
  identity.kind = "linux_namespace";
  identity.name = router.namespace_name;
  identity.namespace_inode = *inode;
  return identity;
}

bool delete_owned_namespace(const OwnedResourceIdentity& item, const OwnershipState& state) {
  if (!namespace_inode(item.name)) return true;
  return namespace_owned(item, state) && run({"ip", "netns", "delete", item.name}) == 0;
}

}  // namespace graphx::infra::detail
