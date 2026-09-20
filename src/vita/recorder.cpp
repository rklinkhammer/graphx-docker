#include "graphx/vita/recorder.hpp"
#include "graphx/node_settings.hpp"
#include "../application_observer.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/seccomp.h>
#include <net/if.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#endif
namespace graphx::vita {
void RecorderCounters::receive(std::size_t reported, std::size_t capacity) noexcept {
  ++packets;
  bytes += reported;
  if (reported > capacity) ++truncated;
  if (reported < 14) ++invalid;
}
void restrict_recorder_descriptor(int descriptor) {
  if (descriptor < 0) throw std::invalid_argument("invalid recorder descriptor");
#ifdef __linux__
#if defined(__aarch64__)
  constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#elif defined(__x86_64__)
  constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#else
#error Unsupported recorder seccomp architecture
#endif
  std::vector<sock_filter> code;
  const auto statement = [&](unsigned short op, unsigned value) {
    code.push_back(BPF_STMT(op, value));
  };
  const auto jump = [&](unsigned short op, unsigned value, unsigned char yes, unsigned char no) {
    code.push_back(BPF_JUMP(op, value, yes, no));
  };
  statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
  jump(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0);
  statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
  statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));
  // A whitelist blocks all descriptor duplication/transfer, new sockets, alternate
  // transmit APIs, exec and io_uring. Existing telemetry/log descriptors can write.
  for (auto number : {__NR_write, __NR_writev, __NR_sendto}) {
    jump(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 4);
    statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
    jump(BPF_JMP | BPF_JEQ | BPF_K, descriptor, 0, 1);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  }
  // Runtime/sanitizer pipes may query or set status flags, but cannot duplicate
  // the protected descriptor with F_DUPFD/F_DUPFD_CLOEXEC.
  jump(BPF_JMP | BPF_JEQ | BPF_K, __NR_fcntl, 0, 5);
  statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[1]));
  jump(BPF_JMP | BPF_JEQ | BPF_K, F_GETFL, 2, 0);
  jump(BPF_JMP | BPF_JEQ | BPF_K, F_SETFL, 1, 0);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  jump(BPF_JMP | BPF_JEQ | BPF_K, __NR_recvmsg, 0, 4);
  statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0]));
  jump(BPF_JMP | BPF_JEQ | BPF_K, descriptor, 1, 0);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  jump(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 4);
  statement(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[2]));
  jump(BPF_JMP | BPF_JSET | BPF_K, O_WRONLY | O_RDWR | O_CREAT | O_TRUNC, 0, 1);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
  statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  const auto allow = [&](unsigned n) {
    jump(BPF_JMP | BPF_JEQ | BPF_K, n, 0, 1);
    statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  };
  for (auto number : {__NR_read,          __NR_pread64,         __NR_close,        __NR_lseek,
                      __NR_fstat,         __NR_newfstatat,      __NR_statx,        __NR_mmap,
                      __NR_mprotect,      __NR_munmap,          __NR_brk,          __NR_madvise,
                      __NR_clock_gettime, __NR_clock_nanosleep, __NR_nanosleep,    __NR_futex,
                      __NR_rt_sigaction,  __NR_rt_sigprocmask,  __NR_rt_sigreturn, __NR_sigaltstack,
                      __NR_exit,          __NR_exit_group,      __NR_getpid,       __NR_gettid,
                      __NR_getrandom,     __NR_recvfrom,        __NR_getsockopt,   __NR_ppoll,
                      __NR_sched_yield})
    allow(number);
#ifdef __NR_rseq
  allow(__NR_rseq);
#endif
  allow(__NR_set_robust_list);
  allow(__NR_pipe2);
  allow(__NR_readlinkat);
#ifdef __NR_pipe
  allow(__NR_pipe);
#endif
#ifdef __NR_readlink
  allow(__NR_readlink);
#endif
#ifdef __NR_poll
  allow(__NR_poll);
#endif
#ifdef __NR_restart_syscall
  allow(__NR_restart_syscall);
#endif
  statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);
  sock_fprog filter{static_cast<unsigned short>(code.size()), code.data()};
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
      syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &filter))
    throw std::runtime_error("recorder seccomp prerequisite");
#else
  (void)descriptor;
  throw std::runtime_error("recorder requires Linux");
#endif
}
namespace {
#ifdef __linux__
volatile std::sig_atomic_t stopped{};
void stop(int) { stopped = 1; }
struct Socket {
  int fd{-1};
  ~Socket() {
    if (fd >= 0) ::close(fd);
  }
};
#endif
}  // namespace
int run_recorder(int argc, char** argv) try {
#ifdef __linux__
  // Open an inactive socket, then drop capabilities before common startup can
  // create output/telemetry threads. Binding activates it only on the owned peer.
  // The image grants permitted-only NET_RAW. Activate only that capability;
  // the container bounding set and no-new-privileges still constrain the grant.
  __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
  std::array<__user_cap_data_struct, 2> caps{};
  caps[0].permitted = caps[0].effective = 1U << CAP_NET_RAW;
  if (syscall(SYS_capset, &header, caps.data()))
    throw std::runtime_error("recorder requires initial NET_RAW capability");
  Socket socket{::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
  if (socket.fd < 0) throw std::runtime_error("recorder requires initial NET_RAW capability");
  caps = {};
  if (syscall(SYS_capset, &header, caps.data()))
    throw std::runtime_error("cannot drop recorder capabilities");
  // The common log drainer duplicates stdout: make its destination nonblocking
  // before it starts, so shutdown cannot wait on an unread logging pipe.
  for (const auto fd : {STDOUT_FILENO, STDERR_FILENO})
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0)
      throw std::runtime_error("recorder log binding");
  stopped = 0;
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  std::signal(SIGPIPE, SIG_IGN);
  auto args = node_arguments(argc, argv);
  auto node = load_node_settings(args.config, args.node, "vita.recorder");
  if (!node.resolved.contains("recorder") ||
      node.resolved.at("execution").at("kind").text() != "container")
    throw std::runtime_error("recorder requires normalized passive container attachment");
  if (!await_node_network(args, [] { return stopped != 0; })) return 0;
  const auto interface = node.resolved.at("recorder").at("interface").text();
  auto index = if_nametoindex(interface.c_str());
  if (!index) throw std::runtime_error("recorder interface missing");
  ifreq request{};
  if (interface.size() >= sizeof(request.ifr_name))
    throw std::runtime_error("recorder interface name");
  std::copy(interface.begin(), interface.end(), request.ifr_name);
  if (ioctl(socket.fd, SIOCGIFMTU, &request) ||
      request.ifr_mtu != node.resolved.at("recorder").at("mtu").integer())
    throw std::runtime_error("recorder interface MTU mismatch");
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = index;
  int buffer = 1024 * 1024, auxiliary = 1;
  if (setsockopt(socket.fd, SOL_PACKET, PACKET_AUXDATA, &auxiliary, sizeof(auxiliary)))
    throw std::runtime_error("recorder VLAN metadata prerequisite");
  if (setsockopt(socket.fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer)) ||
      bind(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
    throw std::runtime_error("recorder receive binding");
  int receive_buffer{};
  socklen_t receive_buffer_size = sizeof(receive_buffer);
  if (getsockopt(socket.fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, &receive_buffer_size) ||
      receive_buffer <= 0 || receive_buffer > 2 * buffer)
    throw std::runtime_error("recorder receive storage bound");
  auto trace = std::unique_ptr<UdpJsonTraceSink>{};
  if (!node.resolved.at("telemetry").at("credential").is_null()) {
    auto secret = demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET");
    if (secret.empty()) throw std::runtime_error("recorder telemetry credentials");
    trace = std::make_unique<UdpJsonTraceSink>(
        node.id(), node.resolved.at("telemetry").at("host").text(),
        node.resolved.at("telemetry").at("port").integer(), secret,
        [] { return demo::secret_env("GRAPHX_TELEMETRY_SHARED_SECRET"); });
  }
  if (fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK) < 0)
    throw std::runtime_error("recorder output binding");
  restrict_recorder_descriptor(socket.fd);
  if (!await_node_release(node, args, [] { return stopped != 0; })) return 0;
  RecorderCounters counters;
  std::array<std::byte, 9022> bytes{};
  const auto capacity =
      static_cast<std::size_t>(node.resolved.at("recorder").at("mtu").integer() + 22);
  auto next = std::chrono::steady_clock::now();
  std::string pending;
  std::size_t offset = 0;
  while (!stopped) {
    for (int turn = 0; turn < 64; ++turn) {
      alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(tpacket_auxdata))> auxiliary_data{};
      iovec vector{bytes.data(), capacity};
      msghdr message{};
      message.msg_iov = &vector;
      message.msg_iovlen = 1;
      message.msg_control = auxiliary_data.data();
      message.msg_controllen = auxiliary_data.size();
      auto count = recvmsg(socket.fd, &message, MSG_TRUNC);
      if (count < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ++counters.errors;
        break;
      }
      std::size_t vlan = 0;
      for (auto* c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
        if (c->cmsg_level == SOL_PACKET && c->cmsg_type == PACKET_AUXDATA &&
            c->cmsg_len >= CMSG_LEN(sizeof(tpacket_auxdata))) {
          auto* a = reinterpret_cast<const tpacket_auxdata*>(CMSG_DATA(c));
          if (a->tp_status & TP_STATUS_VLAN_VALID) vlan = 4;
        }
      if (message.msg_flags & MSG_CTRUNC) ++counters.errors;
      counters.receive(count + vlan, capacity);
    }
    if (std::chrono::steady_clock::now() >= next) {
      tpacket_stats stats{};
      socklen_t size = sizeof(stats);
      if (getsockopt(socket.fd, SOL_PACKET, PACKET_STATISTICS, &stats, &size) == 0)
        counters.kernel_drops += stats.tp_drops;
      else
        ++counters.errors;
      if (pending.empty()) {
        std::ostringstream text;
        text << "recorder packets=" << counters.packets << " bytes=" << counters.bytes
             << " truncated=" << counters.truncated << " invalid=" << counters.invalid
             << " kernel_drops=" << counters.kernel_drops << " errors=" << counters.errors
             << " log_drops=" << counters.log_drops << " socket_buffer_bytes=" << receive_buffer
             << " persistence=none\n";
        pending = text.str();
        offset = 0;
      } else
        ++counters.log_drops;
      if (trace) trace->on_heartbeat(node.id(), 0);
      next = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
    if (!pending.empty()) {
      auto n = write(STDOUT_FILENO, pending.data() + offset, pending.size() - offset);
      if (n > 0) {
        offset += n;
        if (offset == pending.size()) pending.clear();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return 0;
#else
  (void)argc;
  (void)argv;
  throw std::runtime_error("recorder requires Linux AF_PACKET");
#endif
} catch (const std::exception& e) {
  std::cerr << "graphx-vita-recorder: " << e.what() << '\n';
  return 1;
}
}  // namespace graphx::vita
