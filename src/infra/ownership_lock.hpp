#pragma once

#include <filesystem>

namespace graphx::infra::detail {

enum class OwnershipLockMode { shared, exclusive };

class OwnershipLock {
 public:
  static OwnershipLock open_existing(const std::filesystem::path& path, OwnershipLockMode mode);
  static OwnershipLock open_or_create(const std::filesystem::path& path, OwnershipLockMode mode);

  ~OwnershipLock();
  OwnershipLock(const OwnershipLock&) = delete;
  OwnershipLock& operator=(const OwnershipLock&) = delete;
  OwnershipLock(OwnershipLock&& other) noexcept;
  OwnershipLock& operator=(OwnershipLock&& other) noexcept;

 private:
  explicit OwnershipLock(int descriptor) noexcept : descriptor_(descriptor) {}
  int descriptor_{-1};
};

}  // namespace graphx::infra::detail
