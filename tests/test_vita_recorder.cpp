#include "graphx/vita/recorder.hpp"
#include <iostream>
#include <stdexcept>
#ifdef __linux__
#include <cerrno>
#include <csignal>
#include <array>
#include <atomic>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
void require(bool ok) {
  if (!ok) throw std::runtime_error("recorder assertion");
}
int main() try {
  graphx::vita::RecorderCounters count;
  count.receive(14, 9022);
  count.receive(9022, 9022);
  count.receive(9023, 9022);
  count.receive(0, 9022);
  require(count.packets == 4 && count.bytes == 18059 && count.truncated == 1 && count.invalid == 1);
  for (int i = 0; i < 100000; ++i) count.receive(4170, 9022);
  require(count.packets == 100004 && count.bytes == 417018059 && count.truncated == 1);
#ifdef __linux__
  int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
  require(socket >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  require(bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  require(getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0);
  const auto sender = ::socket(AF_INET, SOCK_DGRAM, 0);
  require(sender >= 0);
  std::array<char, 9014> frame{};
  frame.front() = 42;
  frame.back() = 17;
  require(sendto(sender, frame.data(), frame.size(), 0, reinterpret_cast<sockaddr*>(&address),
                 length) == static_cast<ssize_t>(frame.size()));
  close(sender);
  const auto child = fork();
  require(child >= 0);
  if (!child) {
    std::atomic<bool> ready{false}, filtered{false};
    std::atomic<int> worker_result{-1};
    std::thread worker([&] {
      ready = true;
      while (!filtered.load()) std::this_thread::yield();
      char byte{};
      worker_result = (write(socket, &byte, 1) == -1 && errno == EPERM) ? 0 : 1;
    });
    while (!ready.load()) std::this_thread::yield();
    graphx::vita::restrict_recorder_descriptor(socket);
    filtered = true;
    worker.join();
    if (worker_result.load() != 0) _exit(10);
    stack_t alternate{};
    if (sigaltstack(nullptr, &alternate) != 0) _exit(11);
    if (fork() != -1 || errno != EPERM) _exit(12);
    std::array<char, 9022> received{};
    iovec vector{received.data(), received.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    if (recvmsg(socket, &message, MSG_TRUNC) != 9014 || received.front() != 42 ||
        received[9013] != 17)
      _exit(8);
    const int file = openat(AT_FDCWD, "/dev/null", O_RDONLY);
    if (file < 0) _exit(9);
    close(file);
    char byte{};
    if (write(socket, &byte, 1) != -1 || errno != EPERM) _exit(2);
    if (sendto(socket, &byte, 1, 0, nullptr, 0) != -1 || errno != EPERM) _exit(3);
    if (dup(socket) != -1 || errno != EPERM) _exit(4);
    if (fcntl(socket, F_DUPFD, 20) != -1 || errno != EPERM) _exit(5);
    if (::socket(AF_INET, SOCK_DGRAM, 0) != -1 || errno != EPERM) _exit(6);
    if (openat(AT_FDCWD, "/tmp/graphx-recorder-forbidden", O_WRONLY | O_CREAT, 0600) != -1 ||
        errno != EPERM)
      _exit(7);
    _exit(0);
  }
  int status{};
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  close(socket);
  std::cout << "Linux receive-descriptor restrictions passed without network privileges\n";
#else
  std::cout << "Linux syscall enforcement unrun on this platform\n";
#endif
  std::cout
      << "bounded frame accounting, jumbo boundary, truncation and no-retention load passed\n";
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
