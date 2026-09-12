#include "infra/capture_resources.hpp"

#include "infra/command_runner.hpp"
#include "infra/endpoint_resources.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace graphx::infra::detail {
namespace {
class FileDescriptor {
 public:
  explicit FileDescriptor(int value = -1) : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0) ::close(value_);
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  [[nodiscard]] int get() const noexcept { return value_; }

 private:
  int value_;
};

}  // namespace

std::string process_start_time(std::uint32_t pid) {
  return infra::detail::inspect_process(pid).start_time;
}

std::string process_command(std::uint32_t pid) {
  return infra::detail::inspect_process(pid).command;
}

std::string process_name(std::uint32_t pid) { return infra::detail::inspect_process(pid).name; }

void detach_child_io() {
  ::setsid();
  const auto null = ::open("/dev/null", O_RDWR);
  if (null >= 0) {
    ::dup2(null, STDIN_FILENO);
    ::dup2(null, STDOUT_FILENO);
    ::dup2(null, STDERR_FILENO);
    if (null > STDERR_FILENO) ::close(null);
  }
  const auto maximum = ::sysconf(_SC_OPEN_MAX);
  for (int descriptor = 3; descriptor < (maximum > 0 ? maximum : 1024); ++descriptor)
    ::close(descriptor);
}

std::uint32_t spawn_process(const std::vector<std::string>& arguments, std::string_view marker,
                            const std::filesystem::path& diagnostic_path) {
  const auto spawned =
      infra::detail::spawn_background({.arguments = arguments, .diagnostic_path = diagnostic_path});
  const auto child = static_cast<pid_t>(spawned.pid);
  if (spawned.exec_error != 0) {
    while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
    throw std::system_error(spawned.exec_error, std::generic_category(), "execvp");
  }
  const auto pid = static_cast<std::uint32_t>(child);
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto start = process_start_time(pid);
    const auto command = process_command(pid);
    if (!start.empty() && command.find(marker) != std::string::npos) return pid;
    int status{};
    if (::waitpid(child, &status, WNOHANG) == child)
      throw std::runtime_error("owned process exited during startup");
    ::usleep(20'000);
  }
  ::kill(child, SIGKILL);
  ::waitpid(child, nullptr, 0);
  throw std::runtime_error("owned process did not become identifiable");
}

bool process_owned(std::uint32_t pid, const std::string& start_time, std::string_view marker) {
  return pid != 0 && !start_time.empty() && process_start_time(pid) == start_time &&
         process_command(pid).find(marker) != std::string::npos;
}

bool timer_owned(std::uint32_t pid, const std::string& start_time) {
  const auto name = process_name(pid);
  return pid != 0 && !start_time.empty() && process_start_time(pid) == start_time &&
         name == "graphx-fault";
}

bool stop_owned_process(std::uint32_t pid, const std::string& start_time, std::string_view marker,
                        bool timer) {
  const auto owned = timer ? timer_owned(pid, start_time) : process_owned(pid, start_time, marker);
  if (!owned) return process_start_time(pid).empty();
  ::kill(static_cast<pid_t>(pid), SIGTERM);
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (process_start_time(pid).empty()) return true;
    ::usleep(20'000);
  }
  if ((timer ? timer_owned(pid, start_time) : process_owned(pid, start_time, marker)))
    ::kill(static_cast<pid_t>(pid), SIGKILL);
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (process_start_time(pid).empty()) return true;
    ::usleep(20'000);
  }
  return process_start_time(pid).empty();
}

bool capture_directory_metadata_matches(const OwnedCapture& capture, const struct stat& metadata,
                                        std::uint32_t required_mode) {
  return S_ISDIR(metadata.st_mode) &&
         static_cast<std::uint64_t>(metadata.st_dev) == capture.directory_device &&
         static_cast<std::uint64_t>(metadata.st_ino) == capture.directory_inode &&
         static_cast<std::uint32_t>(metadata.st_uid) == capture.directory_uid &&
         static_cast<std::uint32_t>(metadata.st_gid) == capture.directory_gid &&
         static_cast<std::uint32_t>(metadata.st_mode & 0777) == required_mode &&
         capture.directory_uid == 0 && capture.directory_gid == 0 && capture.directory_mode == 0700;
}

bool directory_identity_matches(const OwnedCapture& capture) {
  const FileDescriptor directory(
      ::open(capture.session_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  struct stat metadata{};
  return directory.get() >= 0 && ::fstat(directory.get(), &metadata) == 0 &&
         capture_directory_metadata_matches(capture, metadata);
}

bool capture_file_metadata_is_safe(const OwnedCapture& capture, const struct stat& metadata) {
  return S_ISREG(metadata.st_mode) && metadata.st_nlink == 1 &&
         static_cast<std::uint32_t>(metadata.st_uid) == capture.directory_uid &&
         static_cast<std::uint32_t>(metadata.st_gid) == capture.directory_gid &&
         (metadata.st_mode & 0022) == 0;
}

struct CaptureFileIdentity {
  std::string name;
  std::uint64_t device{};
  std::uint64_t inode{};
  std::uint32_t mode{};
};

bool capture_file_name_is_approved(std::string_view name) {
  return name == "dumpcap.stderr" || name.ends_with(".pcapng");
}

bool retained_capture_file_metadata_is_safe(const struct stat& metadata) {
  return S_ISREG(metadata.st_mode) && metadata.st_nlink == 1 && metadata.st_uid == 0 &&
         metadata.st_gid == 0 && (metadata.st_mode & 0222) == 0;
}

std::optional<std::vector<CaptureFileIdentity>> inspect_capture_session_files(
    int directory, bool require_read_only) {
  const auto duplicate = ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (duplicate < 0) return std::nullopt;
  auto* entries = ::fdopendir(duplicate);
  if (!entries) {
    ::close(duplicate);
    return std::nullopt;
  }
  std::vector<CaptureFileIdentity> files;
  bool safe = true;
  bool has_pcapng = false;
  for (;;) {
    errno = 0;
    const auto* entry = ::readdir(entries);
    if (!entry) {
      if (errno != 0) safe = false;
      break;
    }
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    struct stat metadata{};
    if (!capture_file_name_is_approved(name) ||
        ::fstatat(directory, name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 || metadata.st_uid != 0 ||
        metadata.st_gid != 0 || (metadata.st_mode & 0022) != 0 ||
        (require_read_only && (metadata.st_mode & 0222) != 0)) {
      safe = false;
      break;
    }
    has_pcapng = has_pcapng || name.ends_with(".pcapng");
    files.push_back({name, static_cast<std::uint64_t>(metadata.st_dev),
                     static_cast<std::uint64_t>(metadata.st_ino),
                     static_cast<std::uint32_t>(metadata.st_mode & 0777)});
  }
  ::closedir(entries);
  if (!safe || !has_pcapng) return std::nullopt;
  return files;
}

bool seal_capture_session_files(int directory) {
  const auto files = inspect_capture_session_files(directory, false);
  if (!files) return false;
  for (const auto& expected : *files) {
    const FileDescriptor file(::openat(directory, expected.name.c_str(), O_RDONLY | O_NOFOLLOW));
    struct stat metadata{};
    if (file.get() < 0 || ::fstat(file.get(), &metadata) != 0 ||
        static_cast<std::uint64_t>(metadata.st_dev) != expected.device ||
        static_cast<std::uint64_t>(metadata.st_ino) != expected.inode ||
        !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 || metadata.st_uid != 0 ||
        metadata.st_gid != 0 || (metadata.st_mode & 0022) != 0 ||
        ::fchmod(file.get(), expected.mode & ~0222U) != 0 || ::fstat(file.get(), &metadata) != 0 ||
        !retained_capture_file_metadata_is_safe(metadata))
      return false;
  }
  const auto sealed = inspect_capture_session_files(directory, true);
  if (!sealed || sealed->size() != files->size()) return false;
  for (const auto& expected : *files) {
    const auto found = std::find_if(sealed->begin(), sealed->end(), [&](const auto& candidate) {
      return candidate.name == expected.name && candidate.device == expected.device &&
             candidate.inode == expected.inode;
    });
    if (found == sealed->end()) return false;
  }
  return true;
}

std::size_t capture_file_count(const OwnedCapture& capture, std::uint64_t maximum_size) {
  const FileDescriptor directory(
      ::open(capture.session_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  struct stat directory_metadata{};
  if (directory.get() < 0 || ::fstat(directory.get(), &directory_metadata) != 0 ||
      !capture_directory_metadata_matches(capture, directory_metadata))
    return 0;
  const auto duplicate = ::dup(directory.get());
  if (duplicate < 0) return 0;
  auto* entries = ::fdopendir(duplicate);
  if (!entries) {
    ::close(duplicate);
    return 0;
  }
  std::size_t count{};
  bool safe = true;
  for (;;) {
    errno = 0;
    const auto* entry = ::readdir(entries);
    if (!entry) {
      if (errno != 0) safe = false;
      break;
    }
    const std::string name = entry->d_name;
    if (!name.ends_with(".pcapng")) continue;
    struct stat metadata{};
    if (::fstatat(directory.get(), name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
        !capture_file_metadata_is_safe(capture, metadata) || metadata.st_size < 12 ||
        static_cast<std::uint64_t>(metadata.st_size) > maximum_size + 266240) {
      safe = false;
      break;
    }
    const FileDescriptor input(::openat(directory.get(), name.c_str(), O_RDONLY | O_NOFOLLOW));
    std::array<unsigned char, 4> magic{};
    if (input.get() < 0 ||
        ::pread(input.get(), magic.data(), magic.size(), 0) != static_cast<ssize_t>(magic.size()) ||
        magic != std::array<unsigned char, 4>{0x0a, 0x0d, 0x0d, 0x0a}) {
      safe = false;
      break;
    }
    ++count;
  }
  ::closedir(entries);
  return safe ? count : 0;
}

void ensure_real_directory_tree(const std::filesystem::path& directory) {
  if (!directory.is_absolute()) throw std::runtime_error("capture root must be absolute");
  std::filesystem::path current{directory.root_path()};
  for (const auto& component : directory.relative_path()) {
    current /= component;
    struct stat metadata{};
    if (::lstat(current.c_str(), &metadata) != 0) {
      if (errno != ENOENT || ::mkdir(current.c_str(), 0750) != 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot create capture directory " + current.string());
      if (::lstat(current.c_str(), &metadata) != 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot inspect capture directory " + current.string());
    }
    if (!S_ISDIR(metadata.st_mode) || S_ISLNK(metadata.st_mode))
      throw std::runtime_error("capture directory path must not contain symlinks: " +
                               current.string());
  }
}

bool capture_session_name(std::string_view name, std::string_view capture_id) {
  const auto prefix = std::string(capture_id) + "-";
  if (!name.starts_with(prefix) || name.size() != prefix.size() + 32) return false;
  return std::ranges::all_of(name.substr(prefix.size()),
                             [](unsigned char value) { return std::isxdigit(value) != 0; });
}

void prune_expired_capture_sessions(const NetworkCaptureDefinition& definition) {
  const auto now = std::time(nullptr);
  for (const auto& entry : std::filesystem::directory_iterator(definition.directory)) {
    const auto name = entry.path().filename().string();
    if (!capture_session_name(name, definition.id)) continue;
    const FileDescriptor descriptor(
        ::open(entry.path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
    struct stat directory{};
    if (descriptor.get() < 0 || ::fstat(descriptor.get(), &directory) != 0 ||
        !S_ISDIR(directory.st_mode) || directory.st_uid != 0 || directory.st_gid != 0 ||
        (directory.st_mode & 0777) != 0550 || now < directory.st_mtime ||
        static_cast<std::uint64_t>(now - directory.st_mtime) <= definition.retention_seconds)
      continue;
    const auto files = inspect_capture_session_files(descriptor.get(), true);
    if (!files) continue;
    bool unchanged = true;
    for (const auto& file : *files) {
      struct stat metadata{};
      if (::fstatat(descriptor.get(), file.name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
          static_cast<std::uint64_t>(metadata.st_dev) != file.device ||
          static_cast<std::uint64_t>(metadata.st_ino) != file.inode ||
          !retained_capture_file_metadata_is_safe(metadata)) {
        unchanged = false;
        break;
      }
    }
    if (!unchanged) continue;
    for (const auto& file : *files)
      if (::unlinkat(descriptor.get(), file.name.c_str(), 0) != 0)
        throw std::system_error(errno, std::generic_category(),
                                "cannot expire retained capture file");
    struct stat published{};
    if (::lstat(entry.path().c_str(), &published) != 0 ||
        static_cast<std::uint64_t>(published.st_dev) !=
            static_cast<std::uint64_t>(directory.st_dev) ||
        static_cast<std::uint64_t>(published.st_ino) !=
            static_cast<std::uint64_t>(directory.st_ino) ||
        !S_ISDIR(published.st_mode) || published.st_uid != 0 || published.st_gid != 0 ||
        (published.st_mode & 0777) != 0550)
      throw std::runtime_error("retained capture directory changed during expiry");
    if (::rmdir(entry.path().c_str()) != 0)
      throw std::system_error(errno, std::generic_category(),
                              "cannot expire retained capture session");
  }
}

OwnedCapture create_capture(const ExpectedCapture& expected, const OwnershipState& state) {
  const auto& definition = expected.definition;
  std::error_code error;
  ensure_real_directory_tree(definition.directory);
  struct stat storage_boundary{};
  if (::lstat("/var/lib/graphx/captures", &storage_boundary) != 0 ||
      !S_ISDIR(storage_boundary.st_mode) || storage_boundary.st_uid != 0 ||
      storage_boundary.st_gid != 0 || (storage_boundary.st_mode & 0022) != 0)
    throw std::runtime_error(
        "capture storage boundary must be a root-owned, non-writable real directory");
  prune_expired_capture_sessions(definition);
  const auto root_status = std::filesystem::symlink_status(definition.directory, error);
  if (error || !std::filesystem::is_directory(root_status) ||
      std::filesystem::is_symlink(root_status))
    throw std::runtime_error("capture root must be a real directory");
  const auto session =
      std::filesystem::path(definition.directory) / (definition.id + "-" + state.owner_token);
  if (::mkdir(session.c_str(), 0700) != 0)
    throw std::system_error(errno, std::generic_category(), "cannot create capture session");
  struct stat metadata{};
  if (::lstat(session.c_str(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) ||
      metadata.st_uid != 0 || metadata.st_gid != 0 || (metadata.st_mode & 0777) != 0700)
    throw std::runtime_error("cannot record capture directory identity");
  const auto kibibytes = (definition.max_file_bytes + 1023) / 1024;
  const auto output = (session / "ethernet.pcapng").string();
  const auto diagnostic = session / "dumpcap.stderr";
  std::vector<std::string> command = {
      "dumpcap", "-q",
      "-i",      expected.interface,
      "-s",      std::to_string(definition.snaplen),
      "-b",      "filesize:" + std::to_string(kibibytes),
      "-b",      "duration:" + std::to_string(definition.rotation_seconds),
      "-b",      "files:" + std::to_string(definition.max_files),
      "-w",      output};
  OwnedCapture capture;
  capture.id = definition.id;
  capture.attachment_id = definition.attachment;
  capture.interface = expected.interface;
  capture.session_directory = session;
  capture.ifindex = link_ifindex(expected.interface).value_or(0);
  capture.directory_device = static_cast<std::uint64_t>(metadata.st_dev);
  capture.directory_inode = static_cast<std::uint64_t>(metadata.st_ino);
  capture.directory_uid = static_cast<std::uint32_t>(metadata.st_uid);
  capture.directory_gid = static_cast<std::uint32_t>(metadata.st_gid);
  capture.directory_mode = static_cast<std::uint32_t>(metadata.st_mode & 0777);
  try {
    capture.pid = spawn_process(command, session.string(), diagnostic);
    capture.process_start_time = process_start_time(capture.pid);
    if (!capture.ifindex || capture.process_start_time.empty())
      throw std::runtime_error("cannot record capture process identity");
    for (int attempt = 0;
         attempt < 100 && capture_file_count(capture, definition.max_file_bytes) == 0; ++attempt)
      ::usleep(20'000);
    if (capture_file_count(capture, definition.max_file_bytes) == 0)
      throw std::runtime_error("capture process did not publish a PCAPNG section");
  } catch (...) {
    const auto original = std::current_exception();
    if (capture.pid) stop_owned_process(capture.pid, capture.process_start_time, session.string());
    std::ifstream diagnostic_input(diagnostic);
    std::string diagnostic_text((std::istreambuf_iterator<char>(diagnostic_input)),
                                std::istreambuf_iterator<char>());
    if (diagnostic_text.size() > 4096) diagnostic_text.resize(4096);
    std::filesystem::remove_all(session, error);
    if (!diagnostic_text.empty())
      throw std::runtime_error("dumpcap startup failed: " + diagnostic_text);
    std::rethrow_exception(original);
  }
  return capture;
}

bool capture_healthy(const ExpectedCapture& expected, const OwnedCapture& capture) {
  const auto count = capture_file_count(capture, expected.definition.max_file_bytes);
  const bool active =
      process_owned(capture.pid, capture.process_start_time, capture.session_directory.string());
  return directory_identity_matches(capture) &&
         link_ifindex(capture.interface) == capture.ifindex && count > 0 &&
         count <= expected.definition.max_files && active;
}

bool stop_capture(const OwnedCapture& capture) {
  const FileDescriptor directory(
      ::open(capture.session_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW));
  struct stat metadata{};
  if (directory.get() < 0 || ::fstat(directory.get(), &metadata) != 0 ||
      !capture_directory_metadata_matches(capture, metadata))
    return false;
  if (!stop_owned_process(capture.pid, capture.process_start_time,
                          capture.session_directory.string()))
    return false;
  if (::fstat(directory.get(), &metadata) != 0 ||
      !capture_directory_metadata_matches(capture, metadata) ||
      !seal_capture_session_files(directory.get()) || ::fchmod(directory.get(), 0550) != 0)
    return false;
  struct stat published{};
  return ::lstat(capture.session_directory.c_str(), &published) == 0 &&
         capture_directory_metadata_matches(capture, published, 0550);
}

std::uint32_t little_u32(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool complete_pcapng_snapshot(int descriptor, std::uint64_t size) {
  if (size < 28 || size > static_cast<std::uint64_t>(INT64_MAX)) return false;
  std::array<unsigned char, 4> bytes{};
  if (::pread(descriptor, bytes.data(), bytes.size(), 0) != static_cast<ssize_t>(bytes.size()) ||
      bytes != std::array<unsigned char, 4>{0x0a, 0x0d, 0x0d, 0x0a})
    return false;
  if (::pread(descriptor, bytes.data(), bytes.size(), static_cast<off_t>(size - 4)) !=
      static_cast<ssize_t>(bytes.size()))
    return false;
  const auto length = little_u32(bytes);
  if (length < 12 || length > size || length % 4 != 0) return false;
  std::array<unsigned char, 4> leading{};
  if (::pread(descriptor, leading.data(), leading.size(), static_cast<off_t>(size - length + 4)) !=
      static_cast<ssize_t>(leading.size()))
    return false;
  return little_u32(leading) == length;
}

}  // namespace graphx::infra::detail
