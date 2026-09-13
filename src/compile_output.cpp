#include "graphx/compile.hpp"
#include "config_document.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <openssl/rand.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#ifdef __APPLE__
#include <stdio.h>
#endif

namespace graphx {
namespace {
using namespace config_internal;
class Descriptor {
 public:
  explicit Descriptor(int fd) : fd_(fd) {
    if (fd < 0) reject("E_OUTPUT_PATH", "$", "cannot open a non-symlink directory or file");
  }
  ~Descriptor() { ::close(fd_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const { return fd_; }

 private:
  int fd_;
};
std::filesystem::path absolute_path(const std::filesystem::path& path) {
  if (path.native().find('\0') != std::string::npos)
    reject("E_OUTPUT_PATH", "$", "NUL path is forbidden");
  if (path.empty())
    reject("E_OUTPUT_PATH", "$", "explicit input, catalog, source and credential roots required");
  for (const auto& part : path)
    if (part == "..") reject("E_OUTPUT_PATH", "$", "parent traversal is forbidden");
  return std::filesystem::absolute(path).lexically_normal();
}
int open_directory(const std::filesystem::path& path) {
  int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) reject("E_OUTPUT_PATH", "$", "cannot open filesystem root");
  for (const auto& part : path.relative_path()) {
    const int next = ::openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(fd);
    if (next < 0) reject("E_OUTPUT_PATH", "$", "parent must exist and contain no symlinks");
    fd = next;
  }
  return fd;
}
void check_root(const std::filesystem::path& path) {
  // Credential/source roots may not exist yet; never read their contents.
  std::filesystem::path current = "/";
  for (const auto& part : path.relative_path()) {
    current /= part;
    const auto status = std::filesystem::symlink_status(current);
    if (status.type() == std::filesystem::file_type::not_found) return;
    if (!std::filesystem::is_directory(status))
      reject("E_OUTPUT_PATH", "$", "protected roots must contain only directory components");
  }
}
bool within(const std::filesystem::path& path, const std::filesystem::path& root) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void relative_file(const std::string& path) {
  const std::filesystem::path relative(path);
  if (path.empty() || relative.is_absolute() || relative.generic_string() != path)
    reject("E_OUTPUT_PATH", path, "artifact path must be relative");
  for (const auto& part : relative)
    if (part.empty() || part == "." || part == ".." ||
        part.string().find('\\') != std::string::npos)
      reject("E_OUTPUT_PATH", path, "unsafe artifact path");
}
void sync(int fd) {
  if (::fsync(fd) != 0) reject("E_OUTPUT_IO", "$", "cannot sync compiled output");
}
struct Staging {
  int parent{-1};
  int directory{-1};
  std::string name;
  std::vector<std::string> files;
  std::set<std::string> directories;
  bool published{};
  ~Staging() {
    if (published) return;
    struct stat own{}, named{};
    if (::fstat(directory, &own) != 0 ||
        ::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        own.st_dev != named.st_dev || own.st_ino != named.st_ino)
      return;
    for (const auto& file : files) ::unlinkat(directory, file.c_str(), 0);
    for (auto it = directories.rbegin(); it != directories.rend(); ++it)
      ::unlinkat(directory, it->c_str(), AT_REMOVEDIR);
    ::unlinkat(parent, name.c_str(), AT_REMOVEDIR);
  }
};
}  // namespace

void write_compilation(const CompiledGraph& compiled, const std::filesystem::path& output,
                       const CompileRoots& roots) {
  using namespace config_internal;
  const auto destination = absolute_path(output);
  for (const auto* raw : {&roots.input, &roots.catalog, &roots.source, &roots.credentials}) {
    const auto root = absolute_path(*raw);
    check_root(root);
    if (within(destination, root))
      reject("E_OUTPUT_ROOT", "$", "output overlaps an input or credential root");
  }
  if (destination.filename().empty())
    reject("E_OUTPUT_PATH", "$", "output must name a fresh directory");
  Descriptor parent(open_directory(destination.parent_path()));
  struct stat existing{};
  if (::fstatat(parent.get(), destination.filename().c_str(), &existing, AT_SYMLINK_NOFOLLOW) ==
          0 ||
      errno != ENOENT)
    reject("E_OUTPUT_OWNERSHIP", "$",
           "output already exists; replacement requires unavailable runtime inactivity evidence");
  if (!compiled.files.contains("compile-manifest.json") || compiled.files.size() > 4096)
    reject("E_OUTPUT_MANIFEST", "$", "bounded compilation manifest required");
  const auto manifest = parse_document(compiled.files.at("compile-manifest.json"));
  if (manifest.at("graph_id") != Value(compiled.graph_id) ||
      manifest.at("files").array().size() + 1 != compiled.files.size())
    reject("E_OUTPUT_MANIFEST", "$", "manifest identity or file count mismatch");
  std::set<std::string> listed;
  for (const auto& entry : manifest.at("files").array()) {
    const auto& name = entry.at("path").text();
    if (!listed.insert(name).second || !compiled.files.contains(name) ||
        sha256(compiled.files.at(name)) != entry.at("sha256").text())
      reject("E_OUTPUT_MANIFEST", "$", "manifest file hash mismatch");
  }
  std::size_t bytes{};
  for (const auto& [path, contents] : compiled.files) {
    relative_file(path);
    if (path.find('\0') != std::string::npos) reject("E_OUTPUT_PATH", "$", "NUL artifact path");
    bytes += contents.size();
  }
  if (bytes > 32 * kMaxConfigBytes) reject("E_BOUND", "$", "compiled package exceeds 32 MiB");
  unsigned char random[16]{};
  if (RAND_bytes(random, sizeof(random)) != 1)
    reject("E_OUTPUT_IO", "$", "cannot allocate staging identity");
  constexpr char hex[] = "0123456789abcdef";
  std::string name = ".graphx-compile-";
  for (const auto byte : random) {
    name += hex[byte >> 4];
    name += hex[byte & 15];
  }
  if (::mkdirat(parent.get(), name.c_str(), 0700) != 0)
    reject("E_OUTPUT_IO", "$", "cannot create exclusive staging directory");
  Descriptor stage(
      ::openat(parent.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  Staging cleanup{parent.get(), stage.get(), name, {}, {}, false};
  std::vector<std::string> order;
  for (const auto& [path, contents] : compiled.files) {
    (void)contents;
    if (path != "compile-manifest.json") order.push_back(path);
  }
  order.emplace_back("compile-manifest.json");
  for (const auto& path : order) {
    const auto relative = std::filesystem::path(path).parent_path();
    std::filesystem::path directory;
    for (const auto& part : relative) {
      directory /= part;
      if (cleanup.directories.insert(directory.generic_string()).second &&
          ::mkdirat(stage.get(), directory.c_str(), 0700) != 0)
        reject("E_OUTPUT_IO", path, "cannot create artifact directory");
    }
    Descriptor file(::openat(stage.get(), path.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    cleanup.files.push_back(path);
    const auto& contents = compiled.files.at(path);
    std::size_t offset{};
    while (offset < contents.size()) {
      const auto written = ::write(file.get(), contents.data() + offset, contents.size() - offset);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) reject("E_OUTPUT_IO", path, "cannot write artifact");
      offset += static_cast<std::size_t>(written);
    }
    sync(file.get());
  }
  for (const auto& directory : cleanup.directories) {
    Descriptor child(
        ::openat(stage.get(), directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    sync(child.get());
  }
  sync(stage.get());
#ifdef __APPLE__
  const int renamed = ::renameatx_np(parent.get(), name.c_str(), parent.get(),
                                     destination.filename().c_str(), RENAME_EXCL);
#elif defined(__linux__)
  const int renamed =
      static_cast<int>(::syscall(SYS_renameat2, parent.get(), name.c_str(), parent.get(),
                                 destination.filename().c_str(), RENAME_NOREPLACE));
#else
  const int renamed = -1;
#endif
  if (renamed != 0)
    reject("E_OUTPUT_OWNERSHIP", "$",
           "exclusive publication failed; existing output was not replaced");
  cleanup.published = true;
  sync(parent.get());
}
}  // namespace graphx
