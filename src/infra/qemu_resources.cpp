#include "infra/qemu_resources.hpp"
#include "config_document.hpp"
#include "graphx/config_schemas.hpp"
#include <regex>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <thread>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {
using namespace config_internal;
using Clock = std::chrono::steady_clock;
constexpr std::size_t frame_limit = 1024 * 1024;
Value read(const std::filesystem::path& path) {
  safe_execution_path(path);
  return parse_document(read_document(path, frame_limit));
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
ConfigValue verify_guest_artifacts(const ExecutionOptions& options, const ConfigValue& resolved) {
  Array expected_guests;
  for (const auto& node : resolved.at("nodes").array()) {
    if (node.at("execution").at("kind") != Value("qemu")) continue;
    const auto id = node.at("execution").at("guest").text();
    require(std::regex_match(id, std::regex("^[a-z][a-z0-9_-]{0,63}$")),
            "E_GUEST_IDENTITY: invalid guest ID");
    const auto recipe = read(options.output / "guest-build" / (id + ".json"));
    validate_shape(recipe, parse_document(guest_schema));
    require(recipe.at("exists_today") == Value(true),
            "E_GUEST_UNAVAILABLE: guest recipe has no verified release artifacts");
    require(recipe.at("architecture") == Value("x86_64") &&
                node.at("execution").at("architecture") == recipe.at("architecture") &&
                node.at("execution").at("accelerator") == Value("tcg") &&
                recipe.at("application") == node.at("type"),
            "E_GUEST_IDENTITY: guest architecture or application mismatch");
    expected_guests.push_back(compiled_guest_plan(node, recipe, resolved.at("network")));
    const auto root = options.release / "guests" / id;
    require(std::filesystem::is_directory(root),
            "E_GUEST_UNAVAILABLE: guest artifacts are missing");
    std::set<std::string> expected;
    for (const auto& file : recipe.at("outputs").array()) {
      const auto name = file.at("path").text();
      require(name == "bzImage" || name == "rootfs.cpio.gz" || name == "licenses.json" ||
                  name == "artifact-manifest.json",
              "E_GUEST_IDENTITY: unsupported guest artifact");
      require(expected.insert(name).second, "E_GUEST_IDENTITY: duplicate artifact");
      require(std::filesystem::symlink_status(root / name).type() !=
                  std::filesystem::file_type::not_found,
              "E_GUEST_UNAVAILABLE: guest artifact is missing");
      safe_execution_path(root / name);
      const auto size = std::filesystem::file_size(root / name);
      require(size > 0 && size <= 512ULL * 1024 * 1024,
              "E_GUEST_IDENTITY: artifact size exceeds bounds");
      require(configuration_hash(root / name) == file.at("sha256").text(),
              "E_GUEST_IDENTITY: artifact checksum mismatch");
    }
    require(expected == std::set<std::string>{"bzImage", "rootfs.cpio.gz", "licenses.json",
                                              "artifact-manifest.json"},
            "E_GUEST_IDENTITY: incomplete artifact inventory");
    for (const auto& file : std::filesystem::directory_iterator(root))
      require(expected.contains(file.path().filename().string()),
              "E_GUEST_IDENTITY: unexpected artifact file");
    const auto manifest = read(root / "artifact-manifest.json");
    require(manifest.contains("guest_files") && manifest.at("guest_files").is_object(),
            "E_GUEST_IDENTITY: verified guest filesystem inventory required");
    const std::set<std::string> guest_files{"usr/bin/graphx",
                                            "usr/bin/graphx-packet-guest",
                                            "usr/lib/graphx/agent.py",
                                            "usr/lib/graphx/application",
                                            "etc/init.d/S80graphx",
                                            "usr/lib/graphx/sdr/radio.py",
                                            "usr/lib/graphx/sdr/sdr_simulator.py"};
    require(manifest.at("guest_files").object().size() == guest_files.size(),
            "E_GUEST_IDENTITY: invalid guest filesystem inventory");
    for (const auto& name : guest_files)
      require(manifest.at("guest_files").contains(name) &&
                  std::regex_match(manifest.at("guest_files").at(name).text(),
                                   std::regex("^[a-f0-9]{64}$")),
              "E_GUEST_IDENTITY: invalid guest filesystem pin");
    require(manifest.at("guest_files").at("usr/lib/graphx/application") ==
                Value(sha256(recipe.at("application").text() + "\n")),
            "E_GUEST_IDENTITY: application filesystem identity mismatch");
    for (const auto* key : {"id", "application", "architecture", "recipe_revision", "source_digest",
                            "source_date_epoch", "builder_image", "build_argv"})
      require(manifest.at(key) == recipe.at(key), "E_GUEST_IDENTITY: build provenance mismatch");
  }
  if (!expected_guests.empty())
    require(
        read(options.output / "qemu-plan.json") ==
            Value(Object{
                {"version", 1}, {"adapter", "qemu-process-adapter"}, {"guests", expected_guests}}),
        "E_GUEST_IDENTITY: guest plan differs from authoritative projection");
  return expected_guests;
}
void verify_guest_directory(const OwnedResourceIdentity& resource) {
  if (resource.runtime_directory.empty()) return;
  const std::filesystem::path path(resource.runtime_directory);
  safe_execution_path(path);
  struct stat metadata{};
  require(::lstat(path.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode) &&
              std::to_string(metadata.st_dev) + ":" + std::to_string(metadata.st_ino) ==
                  resource.runtime_identity &&
              (metadata.st_uid == 65532 || metadata.st_uid == 0) &&
              (metadata.st_mode & 0777) == 0700,
          "E_GUEST_IDENTITY: runtime directory replaced; nothing removed");
  const std::set<std::string> files{"bzImage", "rootfs.cpio.gz", "qemu.log"};
  const std::set<std::string> sockets{"qmp.sock", "config.sock", "credentials.sock", "ready.sock"};
  for (const auto& item : std::filesystem::directory_iterator(path)) {
    const auto name = item.path().filename().string();
    struct stat entry{};
    require(::lstat(item.path().c_str(), &entry) == 0 &&
                ((files.contains(name) && S_ISREG(entry.st_mode) && entry.st_nlink == 1 &&
                  entry.st_uid == 0) ||
                 (sockets.contains(name) && S_ISSOCK(entry.st_mode) && entry.st_uid == 65532)),
            "E_GUEST_IDENTITY: unexpected runtime file; nothing removed");
  }
}
void cleanup_guest_directory(const OwnedResourceIdentity& resource) {
  if (resource.runtime_directory.empty()) return;
  verify_guest_directory(resource);
  const std::filesystem::path path(resource.runtime_directory);
  for (const auto& item : std::filesystem::directory_iterator(path)) {
    if (item.path().filename() == "qemu.log") {
      require(::chmod(item.path().c_str(), 0400) == 0, "E_GUEST_IDENTITY: cannot seal log");
    } else {
      std::filesystem::remove(item.path());
    }
  }
  require(::chown(path.c_str(), 0, 0) == 0, "E_GUEST_IDENTITY: cannot seal evidence directory");
}
void require_guest_account() {
#if defined(__linux__)
  const auto* user = ::getpwuid(65532);
  require(user && std::string(user->pw_name) == "graphx-qemu" && user->pw_gid == 65532,
          "E_GUEST_ACCOUNT: dedicated graphx-qemu UID/GID 65532 required");
  const auto* group = ::getgrgid(65532);
  require(group && std::string(group->gr_name) == "graphx-qemu",
          "E_GUEST_ACCOUNT: dedicated guest group required");
#else
  throw std::runtime_error("E_TARGET: managed guests require Linux");
#endif
}
struct GuestSession::Impl {
  int directory{-1};
  std::vector<int> sockets;
  int ready{-1};
  OwnedResourceIdentity process;
  Value identity;
  Clock::time_point deadline;
  Clock::time_point process_checked{};
  std::function<bool()> cancelled;
  ~Impl() {
    for (const auto fd : sockets) ::close(fd);
    if (directory >= 0) ::close(directory);
  }
  void check() {
    require(!cancelled(), "E_INTERRUPTED: guest startup interrupted");
    require(Clock::now() < deadline, "E_GUEST_TIMEOUT: bounded guest handshake expired");
    const auto now = Clock::now();
    if (now - process_checked >= std::chrono::milliseconds(100)) {
      require(native_process_status(process) != 0, "E_GUEST_EXIT: guest process exited");
      process_checked = now;
    }
  }
  void wait(int fd, short events) {
    for (;;) {
      check();
      pollfd item{fd, events, 0};
      const auto result = ::poll(&item, 1, 50);
      if (result < 0 && errno == EINTR) continue;
      require(result >= 0, "E_GUEST_CHANNEL: poll failed");
      if (result > 0) {
        require((item.revents & events) != 0, "E_GUEST_CHANNEL: disconnected channel");
        return;
      }
    }
  }
  int connect(const std::string& name) {
    const auto path = "/proc/self/fd/" + std::to_string(directory) + "/" + name + ".sock";
    for (;;) {
      check();
      struct stat before{};
      if (::lstat(path.c_str(), &before) != 0 && errno == ENOENT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      require(S_ISSOCK(before.st_mode) && before.st_uid == 65532 &&
                  ((before.st_mode & 0777) == 0700 || (before.st_mode & 0777) == 0600),
              "E_GUEST_CHANNEL_IDENTITY: socket owner or mode mismatch");
      require(::chmod(path.c_str(), 0600) == 0, "E_GUEST_CHANNEL_IDENTITY: cannot seal socket");
      const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
      require(fd >= 0, "E_GUEST_CHANNEL: socket failed");
      sockets.push_back(fd);
      require(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "E_GUEST_CHANNEL: descriptor setup failed");
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      require(path.size() < sizeof(address.sun_path), "E_GUEST_CHANNEL: socket path exceeds bound");
      std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
      require(::fcntl(fd, F_SETFL, O_NONBLOCK) == 0, "E_GUEST_CHANNEL: nonblocking setup failed");
      if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        const auto error = errno;
        if (error == ECONNREFUSED || error == EAGAIN || error == ENOENT) {
          ::close(fd);
          sockets.pop_back();
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          continue;
        }
        require(error == EINPROGRESS, "E_GUEST_CHANNEL: connection failed");
        wait(fd, POLLOUT);
        int pending{};
        socklen_t pending_size = sizeof(pending);
        require(
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &pending_size) == 0 && pending == 0,
            "E_GUEST_CHANNEL: connection failed");
      }
#if defined(__linux__)
      struct ucred peer{};
      socklen_t length = sizeof(peer);
      require(::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0 && peer.uid == 65532 &&
                  std::to_string(peer.pid) == process.stable_id,
              "E_GUEST_CHANNEL_IDENTITY: wrong QEMU peer");
#endif
      struct stat after{};
      require(::lstat(path.c_str(), &after) == 0 && before.st_ino == after.st_ino &&
                  before.st_dev == after.st_dev,
              "E_GUEST_CHANNEL_IDENTITY: socket replaced");
      return fd;
    }
  }
  void transfer(int fd, char* bytes, std::size_t size, bool output) {
    std::size_t offset{};
    while (offset < size) {
      wait(fd, output ? POLLOUT : POLLIN);
      const auto count = output ? ::send(fd, bytes + offset, size - offset, MSG_NOSIGNAL)
                                : ::recv(fd, bytes + offset, size - offset, 0);
      if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      require(count > 0, "E_GUEST_CHANNEL: truncated frame");
      offset += static_cast<std::size_t>(count);
    }
  }
  void send(int fd, const Value& value) {
    auto bytes = config_value_json(value);
    require(bytes.size() <= frame_limit, "E_GUEST_CHANNEL: oversized frame");
    std::array<char, 4> prefix{};
    for (unsigned i = 0; i < 4; ++i) prefix[i] = static_cast<char>(bytes.size() >> (24 - i * 8));
    transfer(fd, prefix.data(), 4, true);
    transfer(fd, bytes.data(), bytes.size(), true);
  }
  Value qmp_receive(int fd) {
    std::string line;
    while (line.size() < 65536) {
      char byte{};
      transfer(fd, &byte, 1, false);
      if (byte == '\n') return parse_document(line);
      line += byte;
    }
    throw std::runtime_error("E_GUEST_CHANNEL: oversized QMP response");
  }
  Value qmp_command(int fd, const std::string& command, const Value& arguments = Object{}) {
    auto bytes =
        config_value_json(Object{{"execute", command}, {"id", command}, {"arguments", arguments}}) +
        "\r\n";
    transfer(fd, bytes.data(), bytes.size(), true);
    for (unsigned i = 0; i < 32; ++i) {
      const auto response = qmp_receive(fd);
      if (response.contains("event")) continue;
      require(response.contains("return") && response.at("id") == Value(command),
              "E_GUEST_CHANNEL: QMP command rejected");
      return response.at("return");
    }
    throw std::runtime_error("E_GUEST_CHANNEL: excessive QMP events");
  }
  Value receive(int fd) {
    std::array<char, 4> prefix{};
    transfer(fd, prefix.data(), 4, false);
    std::size_t size{};
    for (const unsigned char byte : prefix) size = (size << 8) | byte;
    require(size > 0 && size <= 4096, "E_GUEST_CHANNEL: invalid readiness frame size");
    std::string bytes(size, '\0');
    transfer(fd, bytes.data(), bytes.size(), false);
    return parse_document(bytes);
  }
};
ConfigValue inspect_guest(const OwnedResourceIdentity& process, bool pause_resume) {
  verify_guest_directory(process);
  GuestSession::Impl session;
  session.process = process;
  session.cancelled = [] { return false; };
  session.deadline = Clock::now() + std::chrono::seconds(5);
  session.directory =
      ::open(process.runtime_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  require(session.directory >= 0, "E_GUEST_IDENTITY: directory unavailable");
  const auto fd = session.connect("qmp");
  require(session.qmp_receive(fd).contains("QMP"), "E_GUEST_CHANNEL: invalid QMP greeting");
  session.qmp_command(fd, "qmp_capabilities");
  require(session.qmp_command(fd, "query-status").at("running") == Value(true),
          "E_GUEST_STATE: guest is not running");
  if (pause_resume) {
    session.qmp_command(fd, "stop");
    try {
      require(session.qmp_command(fd, "query-status").at("running") == Value(false),
              "E_GUEST_STATE: guest did not pause");
      session.qmp_command(fd, "cont");
      require(session.qmp_command(fd, "query-status").at("running") == Value(true),
              "E_GUEST_STATE: guest did not resume");
    } catch (...) {
      try {
        session.qmp_command(fd, "cont");
      } catch (...) {
      }
      throw;
    }
  }
  std::string serial;
  for (unsigned chunk = 0; chunk < 128; ++chunk) {
    const auto part =
        session.qmp_command(fd, "ringbuf-read", Object{{"device", "serial"}, {"size", 8192}})
            .text();
    if (part.empty()) break;
    serial += part;
  }
  return serial;
}
GuestSession::~GuestSession() = default;
GuestSession::GuestSession(
    const ExecutionOptions& options, const ConfigValue& guest, const ConfigValue& node,
    const ConfigValue& credentials, const std::filesystem::path& directory,
    const std::string& token, Clock::time_point deadline,
    const std::function<void(const OwnedResourceIdentity&)>& register_identity,
    const std::function<bool()>& cancelled)
    : impl_(std::make_unique<Impl>()) {
  require_guest_account();
  require(std::filesystem::create_directory(directory), "E_GUEST_IDENTITY: directory collision");
  require(::chmod(directory.c_str(), 0700) == 0 && ::chown(directory.c_str(), 65532, 65532) == 0,
          "E_GUEST_IDENTITY: cannot assign guest directory");
  impl_->directory = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  require(impl_->directory >= 0, "E_GUEST_IDENTITY: cannot open guest directory");
  const auto recipe = guest.at("recipe").text();
  for (const auto* name : {"bzImage", "rootfs.cpio.gz"}) {
    std::filesystem::copy_file(options.release / "guests" / recipe / name, directory / name);
    const auto& outputs = guest.at("artifact_verification").array();
    const auto pin =
        std::ranges::find(outputs, Value(name), [](const auto& file) { return file.at("path"); });
    require(
        pin != outputs.end() && configuration_hash(directory / name) == pin->at("sha256").text(),
        "E_GUEST_IDENTITY: boot copy checksum mismatch");
    require(::chmod((directory / name).c_str(), 0444) == 0,
            "E_GUEST_IDENTITY: cannot seal boot copy");
  }
  NativeProcessOptions start;
  start.id = node.at("node_id").text();
  start.cwd = directory;
  start.log = directory / "qemu.log";
  start.executable = std::filesystem::canonical("/usr/bin/qemu-system-x86_64");
  start.guest_identity = true;
  start.file_bytes_limit = 2 * 1024 * 1024;
  start.argv.push_back(start.executable.string());
  const auto& planned = guest.at("argv").array();
  for (std::size_t i = 1; i < planned.size(); ++i) {
    auto argument = planned[i].text();
    const auto replace = [&](const std::string& key, const std::string& value) {
      for (auto pos = argument.find(key); pos != std::string::npos; pos = argument.find(key))
        argument.replace(pos, key.size(), value);
    };
    replace("${GX_RELEASE}/guests/" + recipe + "/", "./");
    replace("${GX_STATE}/" + start.id + "/", "./");
    require(argument.find("${") == std::string::npos, "E_GUEST_PLAN: unresolved guest argv");
    start.argv.push_back(argument);
  }
  struct stat owned_directory{};
  require(::fstat(impl_->directory, &owned_directory) == 0,
          "E_GUEST_IDENTITY: directory identity unavailable");
  impl_->process = start_native_process(start, [&](const auto& process) {
    auto owned = process;
    owned.runtime_directory = directory.string();
    owned.runtime_identity =
        std::to_string(owned_directory.st_dev) + ":" + std::to_string(owned_directory.st_ino);
    register_identity(owned);
  });
  impl_->cancelled = cancelled;
  impl_->deadline = std::min(
      deadline,
      Clock::now() + std::chrono::milliseconds(guest.at("readiness").at("timeout_ms").integer()));
  const auto qmp = impl_->connect("qmp");
  require(impl_->qmp_receive(qmp).contains("QMP"), "E_GUEST_CHANNEL: invalid QMP greeting");
  impl_->qmp_command(qmp, "qmp_capabilities");
  impl_->qmp_command(qmp, "query-status");
  try {
    const auto config_fd = impl_->connect("config");
    const auto credentials_fd = impl_->connect("credentials");
    impl_->ready = impl_->connect("ready");
    // A listening QEMU socket does not mean the guest has opened its virtio port.
    // Connect every backend first, then wait before sending provisioning bytes.
    for (;;) {
      const auto channels = impl_->qmp_command(qmp, "query-chardev");
      std::set<std::string> opened;
      for (const auto& channel : channels.array())
        if (channel.at("frontend-open") == Value(true)) opened.insert(channel.at("label").text());
      if (opened.contains("config") && opened.contains("credentials") && opened.contains("ready"))
        break;
      impl_->check();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto config_bytes =
        read_document(options.output / "nodes" / (start.id + ".json"), frame_limit);
    require(parse_document(config_bytes) == node,
            "E_GUEST_IDENTITY: node configuration changed after verification");
    impl_->identity =
        Object{{"node", start.id}, {"config_sha256", sha256(config_bytes)}, {"token", token}};
    auto config = impl_->identity;
    config["config"] = config_bytes;
    config["network"] = guest.at("network");
    config["version"] = 1;
    auto secret = impl_->identity;
    secret["credentials"] = credentials;
    impl_->send(config_fd, config);
    impl_->send(credentials_fd, secret);
    auto expected = impl_->identity;
    expected["state"] = "listeners-bound";
    require(impl_->receive(impl_->ready) == expected,
            "E_GUEST_CHANNEL_IDENTITY: readiness identity mismatch");
  } catch (const std::exception& error) {
    const std::string failure = error.what();
    std::string serial;
    try {
      impl_->deadline = Clock::now() + std::chrono::seconds(2);
      for (unsigned chunk = 0; chunk < 128; ++chunk) {
        const auto part =
            impl_->qmp_command(qmp, "ringbuf-read", Object{{"device", "serial"}, {"size", 8192}})
                .text();
        if (part.empty()) break;
        serial += part;
        if (serial.size() > 8192) serial.erase(0, serial.size() - 8192);
      }
    } catch (const std::exception&) {
      // Preserve the original failure when QMP is unavailable during rollback.
    }
    throw std::runtime_error(failure +
                             (serial.empty() ? "" : "\nGuest serial (bounded):\n" + serial));
  }
}
void GuestSession::release() {
  impl_->deadline = Clock::now() + std::chrono::seconds(10);
  auto frame = impl_->identity;
  frame["state"] = "release";
  impl_->send(impl_->ready, frame);
  frame["state"] = "released";
  require(impl_->receive(impl_->ready) == frame,
          "E_GUEST_CHANNEL_IDENTITY: release acknowledgment mismatch");
}
}  // namespace graphx::infra::detail
