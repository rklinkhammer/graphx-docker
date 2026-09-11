#include "infra/ownership_lock.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace graphx::infra::detail {
namespace {

int open_lock(const std::filesystem::path& path, OwnershipLockMode mode, bool create) {
  const auto flags = create ? O_RDWR | O_CREAT | O_NOFOLLOW : O_RDONLY | O_NOFOLLOW;
  const auto descriptor = ::open(path.c_str(), flags, 0600);
  if (descriptor < 0)
    throw std::system_error(errno, std::generic_category(), "cannot open ownership lock");

  struct stat metadata{};
  if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size != 0) {
    ::close(descriptor);
    throw std::runtime_error("ownership lock must be a zero-byte regular file");
  }
  if (create && ::fchmod(descriptor, 0600) != 0) {
    const auto failure = errno;
    ::close(descriptor);
    throw std::system_error(failure, std::generic_category(), "cannot secure ownership lock");
  }
  if ((metadata.st_mode & 0777) != 0600 && !create) {
    ::close(descriptor);
    throw std::runtime_error("ownership lock permissions must be 0600");
  }
  const auto operation = mode == OwnershipLockMode::shared ? LOCK_SH | LOCK_NB : LOCK_EX | LOCK_NB;
  if (::flock(descriptor, operation) != 0) {
    ::close(descriptor);
    throw std::runtime_error("another infrastructure operation owns the graph lock");
  }
  return descriptor;
}

}  // namespace

OwnershipLock OwnershipLock::open_existing(const std::filesystem::path& path,
                                           OwnershipLockMode mode) {
  return OwnershipLock(open_lock(path, mode, false));
}

OwnershipLock OwnershipLock::open_or_create(const std::filesystem::path& path,
                                            OwnershipLockMode mode) {
  return OwnershipLock(open_lock(path, mode, true));
}

OwnershipLock::~OwnershipLock() {
  if (descriptor_ >= 0) ::close(descriptor_);
}

OwnershipLock::OwnershipLock(OwnershipLock&& other) noexcept : descriptor_(other.descriptor_) {
  other.descriptor_ = -1;
}

OwnershipLock& OwnershipLock::operator=(OwnershipLock&& other) noexcept {
  if (this != &other) {
    if (descriptor_ >= 0) ::close(descriptor_);
    descriptor_ = other.descriptor_;
    other.descriptor_ = -1;
  }
  return *this;
}

}  // namespace graphx::infra::detail
