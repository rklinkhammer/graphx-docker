#include "graphx/infra.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace graphx {
namespace {

InfraCommand command(std::initializer_list<std::string> arguments, bool ignore_failure = false) {
  return {std::vector<std::string>(arguments), {}, ignore_failure, {}, {}};
}

std::string join(const std::vector<std::uint16_t>& values) {
  std::string result;
  for (const auto value : values) {
    if (!result.empty()) result += ',';
    result += std::to_string(value);
  }
  return result;
}

const SwitchPortDefinition& mirror_port(const SwitchDefinition& network_switch) {
  const auto found = std::ranges::find_if(network_switch.ports, [&](const auto& port) {
    return network_switch.mirror && port.id == network_switch.mirror->output_port;
  });
  if (found == network_switch.ports.end())
    throw std::invalid_argument("mirror output port is invalid");
  return *found;
}

void append_veth(std::vector<InfraCommand>& commands, const std::string& first,
                 const std::string& second) {
  auto create = command({"ip", "link", "add", first, "type", "veth", "peer", "name", second});
  create.rollback_arguments = {"ip", "link", "delete", second};
  create.rollback_identity_arguments = {"cat", "/sys/class/net/" + second + "/ifindex"};
  commands.push_back(std::move(create));
  commands.push_back(command({"ip", "link", "set", first, "up"}));
  commands.push_back(command({"ip", "link", "set", second, "up"}));
}

void append_docker_network(std::vector<InfraCommand>& commands, const NetworkDefinition& network) {
  if (!network.external) return;
  InfraCommand result;
  result.arguments = {"docker", "network", "create", "--driver",
                      std::string(to_string(network.driver))};
  for (const auto& subnet : network.subnets)
    result.arguments.insert(result.arguments.end(), {"--subnet", subnet});
  if (!network.gateway.empty())
    result.arguments.insert(result.arguments.end(), {"--gateway", network.gateway});
  if (network.driver == NetworkDriver::macvlan || network.driver == NetworkDriver::ipvlan)
    result.arguments.insert(result.arguments.end(), {"--opt", "parent=" + network.parent});
  if (network.driver == NetworkDriver::ipvlan)
    result.arguments.insert(result.arguments.end(), {"--opt", "ipvlan_mode=" + network.mode});
  result.arguments.push_back(network.id);
  result.rollback_arguments = {"docker", "network", "rm", network.id};
  result.rollback_identity_arguments = {"docker",   "network", "inspect",
                                        "--format", "{{.Id}}", network.id};
  commands.push_back(std::move(result));
}

std::string nftables_script(const RouterDefinition& router) {
  std::string script =
      "table inet graphx {\n chain forward {\n"
      "  type filter hook forward priority 0; policy accept;\n";
  for (const auto& policy : router.policies) {
    script += "  ";
    if (!policy.source.empty()) script += "ip saddr " + policy.source + " ";
    if (!policy.destination.empty()) script += "ip daddr " + policy.destination + " ";
    script += "counter " + policy.action + " comment \"" + policy.id + "\"\n";
  }
  return script + " }\n}\n";
}

int execute_command(const InfraCommand& command_value, std::ostream& errors) {
  int input_pipe[2] = {-1, -1};
  if (!command_value.standard_input.empty() && ::pipe(input_pipe) != 0) {
    errors << "graphx: pipe: " << std::strerror(errno) << '\n';
    return 1;
  }
  const auto child = ::fork();
  if (child < 0) {
    errors << "graphx: fork: " << std::strerror(errno) << '\n';
    return 1;
  }
  if (child == 0) {
    if (input_pipe[0] >= 0) {
      ::close(input_pipe[1]);
      ::dup2(input_pipe[0], STDIN_FILENO);
      ::close(input_pipe[0]);
    }
    std::vector<char*> argv;
    argv.reserve(command_value.arguments.size() + 1);
    for (const auto& argument : command_value.arguments)
      argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }
  if (input_pipe[0] >= 0) {
    ::close(input_pipe[0]);
    const char* data = command_value.standard_input.data();
    std::size_t remaining = command_value.standard_input.size();
    while (remaining > 0) {
      const auto written = ::write(input_pipe[1], data, remaining);
      if (written <= 0) break;
      data += written;
      remaining -= static_cast<std::size_t>(written);
    }
    ::close(input_pipe[1]);
  }
  int status{};
  if (::waitpid(child, &status, 0) < 0) return 1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

int capture_command(const std::vector<std::string>& arguments, std::string& output,
                    std::ostream& errors) {
  int output_pipe[2];
  if (::pipe(output_pipe) != 0) {
    errors << "graphx: pipe: " << std::strerror(errno) << '\n';
    return 1;
  }
  const auto child = ::fork();
  if (child < 0) {
    ::close(output_pipe[0]);
    ::close(output_pipe[1]);
    errors << "graphx: fork: " << std::strerror(errno) << '\n';
    return 1;
  }
  if (child == 0) {
    ::close(output_pipe[0]);
    ::dup2(output_pipe[1], STDOUT_FILENO);
    ::close(output_pipe[1]);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }
  ::close(output_pipe[1]);
  output.clear();
  char buffer[256];
  for (;;) {
    const auto count = ::read(output_pipe[0], buffer, sizeof(buffer));
    if (count > 0) {
      output.append(buffer, static_cast<std::size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    break;
  }
  ::close(output_pipe[0]);
  int status{};
  if (::waitpid(child, &status, 0) < 0) return 1;
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

}  // namespace

std::vector<InfraCommand> infrastructure_plan(const GraphConfig& config, InfraAction action,
                                              bool transactional) {
  const auto& infrastructure = config.network_infrastructure;
  std::vector<InfraCommand> commands;
  if (action == InfraAction::create) {
    for (const auto& network_switch : infrastructure.switches)
      for (const auto& port : network_switch.ports)
        if (!port.peer.empty()) append_veth(commands, port.interface, port.peer);

    for (const auto& network_switch : infrastructure.switches) {
      auto create_bridge =
          command(transactional
                      ? std::initializer_list<std::string>{"ovs-vsctl", "add-br", network_switch.id}
                      : std::initializer_list<std::string>{"ovs-vsctl", "--may-exist", "add-br",
                                                           network_switch.id});
      create_bridge.rollback_arguments = {"ovs-vsctl", "--if-exists", "del-br", network_switch.id};
      create_bridge.rollback_identity_arguments = {"ovs-vsctl", "get", "Bridge", network_switch.id,
                                                   "_uuid"};
      commands.push_back(std::move(create_bridge));
      commands.push_back(command({"ovs-vsctl", "set", "Bridge", network_switch.id,
                                  "datapath_type=" + network_switch.datapath}));
      commands.push_back(command({"ip", "link", "set", network_switch.id, "up"}));
      for (const auto& port : network_switch.ports) {
        commands.push_back(
            command({"ovs-vsctl", "--may-exist", "add-port", network_switch.id, port.interface}));
        if (port.vlan.access_tag)
          commands.push_back(command({"ovs-vsctl", "set", "Port", port.interface,
                                      "tag=" + std::to_string(*port.vlan.access_tag)}));
        if (!port.vlan.trunks.empty())
          commands.push_back(command(
              {"ovs-vsctl", "set", "Port", port.interface, "trunks=" + join(port.vlan.trunks)}));
      }
    }

    for (const auto& router : infrastructure.routers) {
      if (router.kind != RouterKind::linux_namespace) continue;
      auto create_namespace = command({"ip", "netns", "add", router.namespace_name});
      create_namespace.rollback_arguments = {"ip", "netns", "delete", router.namespace_name};
      create_namespace.rollback_identity_arguments = {"stat", "-Lc", "%i",
                                                      "/run/netns/" + router.namespace_name};
      commands.push_back(std::move(create_namespace));
      commands.push_back(
          command({"ip", "netns", "exec", router.namespace_name, "ip", "link", "set", "lo", "up"}));
      for (const auto& interface : router.interfaces) {
        append_veth(commands, interface.device, interface.peer);
        commands.push_back(
            command({"ip", "link", "set", interface.device, "netns", router.namespace_name}));
        commands.push_back(command({"ip", "netns", "exec", router.namespace_name, "ip", "addr",
                                    "add", interface.address, "dev", interface.device}));
        commands.push_back(command({"ip", "netns", "exec", router.namespace_name, "ip", "link",
                                    "set", interface.device, "up"}));
        commands.push_back(command(
            {"ovs-vsctl", "--may-exist", "add-port", interface.network_switch, interface.peer}));
      }
      if (router.forwarding)
        commands.push_back(command({"ip", "netns", "exec", router.namespace_name, "sysctl", "-w",
                                    "net.ipv4.ip_forward=1"}));
      for (const auto& route : router.routes) {
        if (!route.install_on_create) continue;
        InfraCommand route_command;
        route_command.arguments = {"ip", "netns", "exec", router.namespace_name,
                                   "ip", "route", "add",  route.destination};
        if (!route.via.empty())
          route_command.arguments.insert(route_command.arguments.end(), {"via", route.via});
        if (!route.device.empty())
          route_command.arguments.insert(route_command.arguments.end(), {"dev", route.device});
        commands.push_back(std::move(route_command));
      }
      InfraCommand nft = command({"ip", "netns", "exec", router.namespace_name, "nft", "-f", "-"});
      nft.standard_input = nftables_script(router);
      commands.push_back(std::move(nft));
    }

    for (const auto& network_switch : infrastructure.switches) {
      if (!network_switch.mirror) continue;
      const auto& output = mirror_port(network_switch);
      commands.push_back(command(
          {"ovs-vsctl", "--", "--id=@out", "get", "Port", output.interface, "--", "--id=@mirror",
           "create", "Mirror", "name=" + network_switch.mirror->id,
           std::string("select_all=") + (network_switch.mirror->select_all ? "true" : "false"),
           "output-port=@out", "--", "set", "Bridge", network_switch.id, "mirrors=@mirror"}));
    }
    for (const auto& network : infrastructure.networks) append_docker_network(commands, network);
  } else if (action == InfraAction::destroy) {
    for (auto iterator = infrastructure.networks.rbegin();
         iterator != infrastructure.networks.rend(); ++iterator)
      if (iterator->external)
        commands.push_back(command({"docker", "network", "rm", iterator->id}, true));
    for (auto iterator = infrastructure.routers.rbegin(); iterator != infrastructure.routers.rend();
         ++iterator)
      if (iterator->kind == RouterKind::linux_namespace)
        commands.push_back(command({"ip", "netns", "delete", iterator->namespace_name}, true));
    for (auto iterator = infrastructure.switches.rbegin();
         iterator != infrastructure.switches.rend(); ++iterator)
      commands.push_back(command({"ovs-vsctl", "--if-exists", "del-br", iterator->id}, true));
    for (auto switch_iterator = infrastructure.switches.rbegin();
         switch_iterator != infrastructure.switches.rend(); ++switch_iterator)
      for (auto port_iterator = switch_iterator->ports.rbegin();
           port_iterator != switch_iterator->ports.rend(); ++port_iterator)
        if (!port_iterator->peer.empty())
          commands.push_back(command({"ip", "link", "delete", port_iterator->peer}, true));
  } else {
    for (const auto& network_switch : infrastructure.switches)
      commands.push_back(command({"ovs-vsctl", "list", "Bridge", network_switch.id}, true));
    for (const auto& router : infrastructure.routers)
      if (router.kind == RouterKind::linux_namespace) {
        commands.push_back(
            command({"ip", "netns", "exec", router.namespace_name, "ip", "-br", "address"}, true));
        commands.push_back(
            command({"ip", "netns", "exec", router.namespace_name, "ip", "route", "show"}, true));
        commands.push_back(command(
            {"ip", "netns", "exec", router.namespace_name, "tc", "-s", "qdisc", "show"}, true));
        commands.push_back(command(
            {"ip", "netns", "exec", router.namespace_name, "nft", "list", "ruleset"}, true));
      }
    for (const auto& network : infrastructure.networks)
      commands.push_back(command({"docker", "network", "inspect", network.id}, true));
  }
  return commands;
}

InfraCommand route_command(const GraphConfig& config, std::string_view router_id,
                           std::string_view destination, bool clear) {
  const auto& router = config.network_infrastructure.router(router_id);
  const auto found = std::ranges::find_if(
      router.routes, [&](const auto& value) { return value.destination == destination; });
  if (found == router.routes.end()) throw std::invalid_argument("unknown declared route");
  InfraCommand result;
  result.arguments = {"ip",
                      "netns",
                      "exec",
                      router.namespace_name,
                      "ip",
                      "route",
                      clear ? "delete" : "replace",
                      found->destination};
  if (!clear) {
    if (!found->via.empty()) result.arguments.insert(result.arguments.end(), {"via", found->via});
    if (!found->device.empty())
      result.arguments.insert(result.arguments.end(), {"dev", found->device});
  } else {
    result.ignore_failure = true;
  }
  return result;
}

InfraCommand netem_command(const GraphConfig& config, std::string_view router_id,
                           std::string_view interface_id, bool clear, std::string delay,
                           std::string jitter, std::string loss, std::string rate) {
  const auto& router = config.network_infrastructure.router(router_id);
  const auto found = std::ranges::find_if(router.interfaces, [&](const auto& value) {
    return value.id == interface_id || value.device == interface_id;
  });
  if (found == router.interfaces.end()) throw std::invalid_argument("unknown router interface");
  InfraCommand result;
  result.arguments = {"ip", "netns", "exec", router.namespace_name, "tc", "qdisc"};
  if (clear) {
    result.arguments.insert(result.arguments.end(), {"delete", "dev", found->device, "root"});
    result.ignore_failure = true;
    return result;
  }
  result.arguments.insert(result.arguments.end(),
                          {"replace", "dev", found->device, "root", "netem"});
  if (!delay.empty()) {
    result.arguments.insert(result.arguments.end(), {"delay", delay});
    if (!jitter.empty()) result.arguments.push_back(jitter);
  }
  if (!loss.empty()) result.arguments.insert(result.arguments.end(), {"loss", loss});
  if (!rate.empty()) result.arguments.insert(result.arguments.end(), {"rate", rate});
  if (delay.empty() && loss.empty() && rate.empty())
    throw std::invalid_argument("fault apply requires delay, loss, or rate");
  return result;
}

std::string format_command(const InfraCommand& command_value) {
  std::string result;
  for (const auto& argument : command_value.arguments) {
    if (!result.empty()) result += ' ';
    const bool quote = argument.find_first_of(" \t'\"") != std::string::npos;
    if (!quote)
      result += argument;
    else {
      result += '\'';
      for (const char value : argument) result += value == '\'' ? "'\\''" : std::string(1, value);
      result += '\'';
    }
  }
  return result;
}

int execute_infrastructure_plan(const std::vector<InfraCommand>& commands, bool dry_run,
                                std::ostream& output, std::ostream& errors) {
  struct CompletedCommand {
    const InfraCommand* command{};
    std::string identity;
  };
  std::vector<CompletedCommand> completed;
  const auto rollback_completed = [&] {
    for (auto iterator = completed.rbegin(); iterator != completed.rend(); ++iterator) {
      if (!iterator->command->rollback_identity_arguments.empty()) {
        std::string current_identity;
        const auto identity_status = capture_command(iterator->command->rollback_identity_arguments,
                                                     current_identity, errors);
        if (identity_status != 0 || current_identity != iterator->identity) {
          output << "! rollback skipped; resource identity changed: "
                 << format_command(*iterator->command) << '\n';
          continue;
        }
      }
      InfraCommand rollback;
      rollback.arguments = iterator->command->rollback_arguments;
      output << "- " << format_command(rollback) << '\n';
      const auto rollback_status = execute_command(rollback, errors);
      if (rollback_status != 0)
        errors << "graphx: rollback command failed with status " << rollback_status << ": "
               << format_command(rollback) << '\n';
    }
  };
  for (const auto& command_value : commands) {
    output << "+ " << format_command(command_value) << '\n';
    if (dry_run) continue;
    const auto status = execute_command(command_value, errors);
    if (status != 0 && !command_value.ignore_failure) {
      errors << "graphx: command failed with status " << status << ": "
             << format_command(command_value) << '\n';
      rollback_completed();
      return status;
    }
    if (status == 0 && !command_value.rollback_arguments.empty()) {
      std::string identity;
      if (!command_value.rollback_identity_arguments.empty()) {
        const auto identity_status =
            capture_command(command_value.rollback_identity_arguments, identity, errors);
        if (identity_status != 0) {
          errors << "graphx: failed to identify created resource: " << format_command(command_value)
                 << '\n';
          InfraCommand rollback;
          rollback.arguments = command_value.rollback_arguments;
          output << "- " << format_command(rollback) << '\n';
          const auto rollback_status = execute_command(rollback, errors);
          if (rollback_status != 0)
            errors << "graphx: rollback command failed with status " << rollback_status << ": "
                   << format_command(rollback) << '\n';
          rollback_completed();
          return identity_status;
        }
      }
      completed.push_back({&command_value, std::move(identity)});
    }
  }
  return 0;
}

}  // namespace graphx
