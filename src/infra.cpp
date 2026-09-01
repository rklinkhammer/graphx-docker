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
  return {std::vector<std::string>(arguments), {}, ignore_failure};
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
  commands.push_back(command({"ip", "link", "add", first, "type", "veth", "peer", "name", second}));
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

}  // namespace

std::vector<InfraCommand> infrastructure_plan(const GraphConfig& config, InfraAction action) {
  const auto& infrastructure = config.network_infrastructure;
  std::vector<InfraCommand> commands;
  if (action == InfraAction::create) {
    for (const auto& network_switch : infrastructure.switches)
      for (const auto& port : network_switch.ports)
        if (!port.peer.empty()) append_veth(commands, port.interface, port.peer);

    for (const auto& network_switch : infrastructure.switches) {
      commands.push_back(command({"ovs-vsctl", "--may-exist", "add-br", network_switch.id}));
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
      commands.push_back(command({"ip", "netns", "add", router.namespace_name}));
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
  for (const auto& command_value : commands) {
    output << "+ " << format_command(command_value) << '\n';
    if (dry_run) continue;
    const auto status = execute_command(command_value, errors);
    if (status != 0 && !command_value.ignore_failure) {
      errors << "graphx: command failed with status " << status << ": "
             << format_command(command_value) << '\n';
      return status;
    }
  }
  return 0;
}

}  // namespace graphx
