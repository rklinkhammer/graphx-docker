#include "graphx/sample_application.hpp"
#include "graphx/node_settings.hpp"
#include <arpa/inet.h>
#include <array>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace graphx {
namespace {
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
struct Sockets {
  std::vector<pollfd> values;
  ~Sockets() {
    for (const auto& item : values) ::close(item.fd);
  }
};
}  // namespace
int run_diagnostic_application(int argc, char** argv) {
  try {
    const auto arguments = node_arguments(argc, argv);
    const auto settings = load_node_settings(arguments.config, arguments.node);
    if (!settings.resolved.at("type").text().starts_with("diagnostic."))
      throw std::invalid_argument("E_TYPE: diagnostic node required");
    Sockets sockets;
    for (const auto& [port, bindings] : settings.bindings) {
      (void)port;
      for (const auto& binding : bindings) {
        if (binding.role != ConnectionMode::listen) continue;
        const auto* external = std::get_if<ExternalTransportConfig>(&binding.edge.transport);
        const auto* udp = external ? std::get_if<UdpTransportConfig>(&external->protocol) : nullptr;
        if (!udp || binding.encoding != "raw")
          throw std::invalid_argument("E_BINDING: diagnostic requires raw UDP input");
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) throw std::runtime_error("E_LISTENER: socket failed");
        sockets.values.push_back({fd, POLLIN, 0});
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(udp->port);
        if (::inet_pton(AF_INET, udp->bind.c_str(), &address.sin_addr) != 1 ||
            ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
          throw std::runtime_error("E_LISTENER: diagnostic bind failed");
      }
    }
    stopped = 0;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    if (!await_node_release(settings, arguments, [] { return stopped != 0; })) return 0;
    std::uint64_t received{};
    while (!stopped) {
      if (::poll(sockets.values.data(), static_cast<nfds_t>(sockets.values.size()), 100) <= 0)
        continue;
      for (const auto& item : sockets.values) {
        if (!(item.revents & POLLIN)) continue;
        std::array<unsigned char, 135> bytes{};
        const auto size = ::recv(item.fd, bytes.data(), bytes.size(), 0);
        if (size < 7 || size > 134 || bytes[0] != 'G' || bytes[1] != 'X' || bytes[2] != 'R' ||
            bytes[3] != '1' || bytes[4] != 0 || bytes[5] < 1 || bytes[5] > 128 ||
            size != 6 + bytes[5])
          continue;
        bool safe = true;
        for (std::size_t i = 6; i < static_cast<std::size_t>(size); ++i)
          safe = safe && bytes[i] >= 32 && bytes[i] < 127;
        if (!safe) continue;
        // Bounded diagnostic evidence; never echo or forward a packet.
        if (++received <= 4096)
          std::cout << "received node=" << settings.id()
                    << " token=" << std::string(reinterpret_cast<char*>(bytes.data() + 6), bytes[5])
                    << std::endl;
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
}  // namespace graphx
