#include "graphx/sample_application.hpp"
#include "graphx/node_settings.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace graphx {
namespace {
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
using Clock = std::chrono::steady_clock;
sockaddr_in address(const std::string& ip, std::int64_t port) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  result.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::inet_pton(AF_INET, ip.c_str(), &result.sin_addr) != 1)
    throw std::runtime_error("E_PACKET_ADDRESS: IPv4 address required");
  return result;
}
void bind_interface(int fd, const sockaddr_in& endpoint) {
  if (endpoint.sin_addr.s_addr == INADDR_ANY) return;
  ifaddrs* raw{};
  if (::getifaddrs(&raw) != 0)
    throw std::runtime_error("E_PACKET_INTERFACE: interface inventory unavailable");
  std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> inventory(raw, ::freeifaddrs);
  std::string name;
  for (auto* item = raw; item; item = item->ifa_next) {
    if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
        reinterpret_cast<const sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr !=
            endpoint.sin_addr.s_addr)
      continue;
    if (!name.empty() && name != item->ifa_name)
      throw std::runtime_error("E_PACKET_INTERFACE: ambiguous source interface");
    name = item->ifa_name;
  }
  if (name.empty()) throw std::runtime_error("E_PACKET_INTERFACE: resolved source is not local");
#if defined(__linux__)
  // Bind a new socket once; supported without CAP_NET_RAW on the supported Linux kernels.
  const auto result = ::setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name.c_str(),
                                   static_cast<socklen_t>(name.size() + 1));
#elif defined(__APPLE__)
  const auto index = ::if_nametoindex(name.c_str());
  const auto result = ::setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &index, sizeof(index));
#else
  const int result = -1;
#endif
  if (result != 0)
    throw std::runtime_error("E_PACKET_INTERFACE: cannot bind resolved source interface");
}
struct Descriptor {
  int value{-1};
  explicit Descriptor(int fd) : value(fd) {
    if (value < 0) throw std::runtime_error("E_PACKET_SOCKET: socket failed");
  }
  ~Descriptor() { ::close(value); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
};
}  // namespace
int run_packet_application(int argc, char** argv, std::string_view type) {
  try {
    const auto arguments = node_arguments(argc, argv);
    const auto settings = load_node_settings(arguments.config, arguments.node);
    if (settings.resolved.at("type").text() != type)
      throw std::runtime_error("E_TYPE: packet application type mismatch");
    const auto& bindings = settings.resolved.at("bindings");
    const auto& tcp = bindings.at("tcp_in").array().at(0).at("settings");
    const auto& udp = bindings.at("udp_in").array().at(0).at("settings");
    const auto udp_limit = std::min<std::int64_t>(1400, udp.at("max_datagram_bytes").integer());
    Descriptor stream(::socket(AF_INET, SOCK_STREAM, 0)),
        datagram(::socket(AF_INET, SOCK_DGRAM, 0));
    const int yes = 1;
    ::setsockopt(stream.value, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    const auto tcp_address = address(tcp.at("bind").text(), tcp.at("port").integer());
    const auto udp_address = address(udp.at("bind").text(), udp.at("port").integer());
    bind_interface(stream.value, tcp_address);
    bind_interface(datagram.value, udp_address);
    if (::bind(stream.value, reinterpret_cast<const sockaddr*>(&tcp_address),
               sizeof(tcp_address)) != 0 ||
        ::listen(stream.value, 8) != 0 ||
        ::bind(datagram.value, reinterpret_cast<const sockaddr*>(&udp_address),
               sizeof(udp_address)) != 0)
      throw std::runtime_error("E_PACKET_BIND: listener bind failed");
    stopped = 0;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::signal(SIGPIPE, SIG_IGN);
    if (!await_node_release(settings, arguments, [] { return stopped != 0; })) return 0;
    auto next = Clock::now();
    std::uint64_t sequence{}, records{};
    while (!stopped) {
      std::array<pollfd, 2> inputs{{{stream.value, POLLIN, 0}, {datagram.value, POLLIN, 0}}};
      ::poll(inputs.data(), inputs.size(), 50);
      std::array<char, 1400> bytes{};
      if (inputs[1].revents & POLLIN) {
        sockaddr_in peer{};
        iovec buffer{bytes.data(), bytes.size()};
        msghdr message{};
        message.msg_name = &peer;
        message.msg_namelen = sizeof(peer);
        message.msg_iov = &buffer;
        message.msg_iovlen = 1;
        const auto count = ::recvmsg(datagram.value, &message, 0);
        if (count > 0 && count <= udp_limit && !(message.msg_flags & MSG_TRUNC)) {
          ::sendto(datagram.value, bytes.data(), static_cast<std::size_t>(count), 0,
                   reinterpret_cast<sockaddr*>(&peer), message.msg_namelen);
          if (++records <= 4096)
            std::cout << "packet node=" << settings.id() << " udp bytes=" << count << std::endl;
        }
      }
      if (inputs[0].revents & POLLIN) {
        Descriptor client(::accept(stream.value, nullptr, nullptr));
        ::fcntl(client.value, F_SETFL, O_NONBLOCK);
        pollfd item{client.value, POLLIN, 0};
        if (::poll(&item, 1, 100) > 0) {
          const auto count = ::recv(client.value, bytes.data(), bytes.size(), 0);
          if (count > 0) {
            ::send(client.value, bytes.data(), static_cast<std::size_t>(count), MSG_NOSIGNAL);
            if (++records <= 4096)
              std::cout << "packet node=" << settings.id() << " tcp bytes=" << count << std::endl;
          }
        }
      }
      if (Clock::now() < next) continue;
      next = Clock::now() + std::chrono::seconds(1);
      const auto message = "node=" + settings.id() + " seq=" + std::to_string(++sequence);
      for (const auto* port : {"tcp_out", "udp_out"}) {
        const auto& peer = bindings.at(port).array().at(0);
        const bool is_tcp = peer.at("transport").text() == "tcp";
        const auto& configuration = peer.at("settings");
        if (!is_tcp && message.size() > static_cast<std::size_t>(
                                            configuration.at("max_datagram_bytes").integer()))
          continue;
        Descriptor output(::socket(AF_INET, is_tcp ? SOCK_STREAM : SOCK_DGRAM, 0));
        const auto local = address(peer.at("source_address").text(), 0);
        bind_interface(output.value, local);
        if (::bind(output.value, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
          throw std::runtime_error("E_PACKET_BIND: output source bind failed");
        const auto remote = address(configuration.at(is_tcp ? "host" : "destination").text(),
                                    configuration.at("port").integer());
        ::fcntl(output.value, F_SETFL, O_NONBLOCK);
        const int connected =
            ::connect(output.value, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
        pollfd writable{output.value, POLLOUT, 0};
        if (connected != 0 && (errno != EINPROGRESS || ::poll(&writable, 1, 100) <= 0)) continue;
        ::send(output.value, message.data(), message.size(), MSG_NOSIGNAL);
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
}  // namespace graphx
