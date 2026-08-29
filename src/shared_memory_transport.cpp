#include "graphx/shared_memory_transport.hpp"

#include "graphx/framing.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <pthread.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace graphx {
namespace {

constexpr std::uint64_t kMagic = 0x475853484d52494eULL;  // GXSHMRIN
constexpr std::uint32_t kLayoutVersion = 1;
constexpr std::size_t kMaximumMappingBytes = 256 * 1024 * 1024;
constexpr auto kPeerCheckInterval = std::chrono::seconds(1);

struct SharedHeader {
  std::uint64_t magic{};
  std::uint32_t version{};
  std::uint32_t capacity{};
  std::uint32_t max_message_bytes{};
  std::uint32_t reserved{};
  std::uint64_t head{};
  std::uint64_t tail{};
  std::uint64_t recovery_count{};
  pid_t producer_pid{};
  pid_t consumer_pid{};
  bool closed{};
  pthread_mutex_t mutex{};
  pthread_cond_t not_empty{};
  pthread_cond_t not_full{};
};

std::runtime_error posix_error(std::string_view action, int error = errno) {
  return std::runtime_error(std::string(action) + ": " + std::strerror(error));
}

std::string normalize_name(std::string name) {
  if (name.empty()) throw std::invalid_argument("shared-memory segment name is empty");
  if (name.front() != '/') name.insert(name.begin(), '/');
  if (name.size() > 200 || name.find('/', 1) != std::string::npos)
    throw std::invalid_argument("shared-memory segment must be one POSIX name");
  return name;
}

std::size_t slot_stride(std::size_t maximum) {
  constexpr auto alignment = alignof(std::max_align_t);
  const auto raw = sizeof(std::uint32_t) + maximum;
  return (raw + alignment - 1) / alignment * alignment;
}

std::size_t mapping_size(const SharedMemoryOptions& options) {
  if (options.capacity == 0 || options.capacity > 65536)
    throw std::invalid_argument("shared-memory capacity must be between 1 and 65536");
  if (options.max_message_bytes < 64 || options.max_message_bytes > kMaxFrameBytes + 4)
    throw std::invalid_argument("shared-memory maximum message size must be between 64 and 16777220");
  const auto stride = slot_stride(options.max_message_bytes);
  if (options.capacity > (kMaximumMappingBytes - sizeof(SharedHeader)) / stride)
    throw std::invalid_argument("shared-memory mapping exceeds 256 MiB");
  return sizeof(SharedHeader) + options.capacity * stride;
}

bool process_alive(pid_t process) {
  if (process <= 0) return false;
  if (::kill(process, 0) == 0) return true;
  return errno == EPERM;
}

void remove_stale_segment(const std::string& name) {
  const int descriptor = ::shm_open(name.c_str(), O_RDWR, 0600);
  if (descriptor < 0) {
    if (errno == ENOENT) return;
    throw posix_error("inspect existing shared-memory segment");
  }
  bool live_consumer{};
  struct stat status {};
  if (::fstat(descriptor, &status) == 0 &&
      status.st_size >= static_cast<off_t>(sizeof(SharedHeader))) {
    void* mapping =
        ::mmap(nullptr, sizeof(SharedHeader), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    if (mapping != MAP_FAILED) {
      auto* header = static_cast<SharedHeader*>(mapping);
      live_consumer =
          std::atomic_ref(header->magic).load(std::memory_order_acquire) == kMagic &&
          process_alive(header->consumer_pid);
      ::munmap(mapping, sizeof(SharedHeader));
    }
  }
  ::close(descriptor);
  if (live_consumer)
    throw std::runtime_error("shared-memory segment already has a live consumer");
  if (::shm_unlink(name.c_str()) != 0 && errno != ENOENT)
    throw posix_error("remove stale shared-memory segment");
}

timespec realtime_after(std::chrono::steady_clock::duration duration) {
  const auto deadline = std::chrono::system_clock::now() + duration;
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(deadline);
  const auto nanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - seconds);
  return {static_cast<time_t>(seconds.time_since_epoch().count()),
          static_cast<long>(nanoseconds.count())};
}

bool lock_header(SharedHeader& header) {
  const int status = ::pthread_mutex_lock(&header.mutex);
#if defined(__linux__)
  if (status == EOWNERDEAD) {
    header.closed = true;
    header.tail = header.head;
    ++header.recovery_count;
    ::pthread_mutex_consistent(&header.mutex);
    return true;
  }
#endif
  if (status != 0) throw posix_error("lock shared-memory ring", status);
  return false;
}

int wait_condition(pthread_cond_t& condition, SharedHeader& header,
                   const timespec& absolute) {
  const int status = ::pthread_cond_timedwait(&condition, &header.mutex, &absolute);
#if defined(__linux__)
  if (status == EOWNERDEAD) {
    header.closed = true;
    header.tail = header.head;
    ++header.recovery_count;
    ::pthread_mutex_consistent(&header.mutex);
  }
#endif
  return status;
}

class Unlock final {
 public:
  explicit Unlock(pthread_mutex_t& mutex) : mutex_(&mutex) {}
  ~Unlock() {
    if (mutex_) ::pthread_mutex_unlock(mutex_);
  }
  void release() {
    if (mutex_) ::pthread_mutex_unlock(mutex_);
    mutex_ = nullptr;
  }

 private:
  pthread_mutex_t* mutex_;
};

void initialize_sync(SharedHeader& header) {
  pthread_mutexattr_t mutex_attributes;
  pthread_condattr_t condition_attributes;
  int status = ::pthread_mutexattr_init(&mutex_attributes);
  if (status != 0) throw posix_error("initialize shared mutex attributes", status);
  status = ::pthread_mutexattr_setpshared(&mutex_attributes, PTHREAD_PROCESS_SHARED);
#if defined(__linux__)
  if (status == 0)
    status = ::pthread_mutexattr_setrobust(&mutex_attributes, PTHREAD_MUTEX_ROBUST);
#endif
  if (status == 0) status = ::pthread_mutex_init(&header.mutex, &mutex_attributes);
  ::pthread_mutexattr_destroy(&mutex_attributes);
  if (status != 0) throw posix_error("initialize process-shared mutex", status);

  status = ::pthread_condattr_init(&condition_attributes);
  if (status == 0)
    status = ::pthread_condattr_setpshared(&condition_attributes, PTHREAD_PROCESS_SHARED);
  if (status == 0) status = ::pthread_cond_init(&header.not_empty, &condition_attributes);
  if (status == 0) status = ::pthread_cond_init(&header.not_full, &condition_attributes);
  ::pthread_condattr_destroy(&condition_attributes);
  if (status != 0) throw posix_error("initialize process-shared condition", status);
}

}  // namespace

struct SharedMemoryTransport::Impl {
  int descriptor{-1};
  void* mapping{MAP_FAILED};
  std::size_t mapping_bytes{};
  std::string segment;
  std::string edge_id;
  SharedMemoryOptions options;
  SharedHeader* header{};
  bool owner{};
  bool owns_name{};
  bool producer{};
  bool locally_closed{};
  bool sync_ready{};
  bool role_claimed{};
  TraceSink* trace_sink{};
  NullTraceSink null_trace_sink;

  ~Impl() { close(); }

  [[nodiscard]] std::string context(std::string_view action) const {
    std::string result = "shared memory";
    if (!edge_id.empty()) result += " edge '" + edge_id + "'";
    result += " segment '" + segment + "' " + std::string(action);
    return result;
  }

  [[nodiscard]] std::byte* slot(std::uint64_t sequence) const {
    auto* base = static_cast<std::byte*>(mapping) + sizeof(SharedHeader);
    return base + (sequence % header->capacity) * slot_stride(header->max_message_bytes);
  }

  [[noreturn]] void fail(std::string detail) {
    auto message = context(detail);
    trace_sink->on_error(edge_id, message);
    throw std::runtime_error(std::move(message));
  }

  void close() noexcept {
    if (locally_closed) return;
    locally_closed = true;
    if (sync_ready && role_claimed && header && mapping != MAP_FAILED) {
      const int status = ::pthread_mutex_lock(&header->mutex);
      if (status == 0
#if defined(__linux__)
          || status == EOWNERDEAD
#endif
      ) {
#if defined(__linux__)
        if (status == EOWNERDEAD) ::pthread_mutex_consistent(&header->mutex);
#endif
        header->closed = true;
        if (producer && header->producer_pid == ::getpid()) header->producer_pid = 0;
        if (!producer && header->consumer_pid == ::getpid()) header->consumer_pid = 0;
        ::pthread_cond_broadcast(&header->not_empty);
        ::pthread_cond_broadcast(&header->not_full);
        ::pthread_mutex_unlock(&header->mutex);
      }
    }
    if (mapping != MAP_FAILED) {
      ::munmap(mapping, mapping_bytes);
      mapping = MAP_FAILED;
      header = nullptr;
    }
    if (descriptor >= 0) {
      ::close(descriptor);
      descriptor = -1;
    }
    if (owns_name) ::shm_unlink(segment.c_str());
  }
};

std::unique_ptr<SharedMemoryTransport::Impl> SharedMemoryTransport::create_impl(
    std::string segment, std::string edge_id, TraceSink* trace_sink,
    SharedMemoryOptions options, bool owner) {
  auto impl = std::make_unique<SharedMemoryTransport::Impl>();
  impl->segment = normalize_name(segment);
  impl->edge_id = std::move(edge_id);
  impl->options = options;
  impl->owner = owner;
  impl->producer = !owner;
  impl->trace_sink = trace_sink ? trace_sink : &impl->null_trace_sink;
  const auto connect_deadline = std::chrono::steady_clock::now() + options.connect_timeout;

  if (owner) {
    impl->mapping_bytes = mapping_size(options);
    remove_stale_segment(impl->segment);
    impl->descriptor = ::shm_open(impl->segment.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (impl->descriptor < 0) throw posix_error("create shared-memory segment");
    impl->owns_name = true;
    if (::ftruncate(impl->descriptor, static_cast<off_t>(impl->mapping_bytes)) != 0)
      throw posix_error("size shared-memory segment");
  } else {
    do {
      impl->descriptor = ::shm_open(impl->segment.c_str(), O_RDWR, 0600);
      if (impl->descriptor >= 0 || errno != ENOENT) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < connect_deadline);
    if (impl->descriptor < 0) throw posix_error("open shared-memory segment");
    struct stat status {};
    if (::fstat(impl->descriptor, &status) != 0) throw posix_error("inspect shared-memory segment");
    if (status.st_size < static_cast<off_t>(sizeof(SharedHeader)) ||
        status.st_size > static_cast<off_t>(kMaximumMappingBytes))
      throw std::runtime_error("shared-memory segment has invalid size");
    impl->mapping_bytes = static_cast<std::size_t>(status.st_size);
  }

  impl->mapping =
      ::mmap(nullptr, impl->mapping_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, impl->descriptor, 0);
  if (impl->mapping == MAP_FAILED) throw posix_error("map shared-memory segment");
  impl->header = static_cast<SharedHeader*>(impl->mapping);

  if (owner) {
    std::memset(impl->mapping, 0, impl->mapping_bytes);
    impl->header->version = kLayoutVersion;
    impl->header->capacity = static_cast<std::uint32_t>(options.capacity);
    impl->header->max_message_bytes = static_cast<std::uint32_t>(options.max_message_bytes);
    impl->header->consumer_pid = ::getpid();
    initialize_sync(*impl->header);
    impl->sync_ready = true;
    std::atomic_ref(impl->header->magic).store(kMagic, std::memory_order_release);
    impl->role_claimed = true;
  } else {
    while (std::atomic_ref(impl->header->magic).load(std::memory_order_acquire) != kMagic &&
           std::chrono::steady_clock::now() < connect_deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (std::atomic_ref(impl->header->magic).load(std::memory_order_acquire) != kMagic ||
        impl->header->version != kLayoutVersion)
      throw std::runtime_error("shared-memory segment layout is incompatible or incomplete");
    const auto expected_mapping = mapping_size(options);
    const auto page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const auto rounded_mapping = (expected_mapping + page_size - 1) / page_size * page_size;
    if (impl->header->capacity != options.capacity ||
        impl->header->max_message_bytes != options.max_message_bytes ||
        impl->mapping_bytes < expected_mapping || impl->mapping_bytes > rounded_mapping)
      throw std::runtime_error(
          "shared-memory segment settings do not match configuration (capacity " +
          std::to_string(impl->header->capacity) + "/" + std::to_string(options.capacity) +
          ", maximum " + std::to_string(impl->header->max_message_bytes) + "/" +
          std::to_string(options.max_message_bytes) + ", mapping " +
          std::to_string(impl->mapping_bytes) + "/" + std::to_string(expected_mapping) + ")");
    impl->sync_ready = true;
    const bool recovered = lock_header(*impl->header);
    Unlock unlock(impl->header->mutex);
    if (recovered) impl->fail("recovered an abandoned mutex; recreate the segment");
    if (impl->header->closed) impl->fail("is closed");
    if (impl->header->producer_pid != 0 && process_alive(impl->header->producer_pid))
      impl->fail("already has a live producer");
    impl->header->producer_pid = ::getpid();
    impl->role_claimed = true;
  }
  impl->trace_sink->on_connection(impl->edge_id, ConnectionState::connected);
  return impl;
}

SharedMemoryTransport::SharedMemoryTransport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SharedMemoryTransport SharedMemoryTransport::connect(std::string segment, std::string edge_id,
                                                     TraceSink* trace_sink,
                                                     SharedMemoryOptions options) {
  return SharedMemoryTransport(
      create_impl(std::move(segment), std::move(edge_id), trace_sink, options, false));
}

SharedMemoryTransport SharedMemoryTransport::listen(std::string segment, std::string edge_id,
                                                    TraceSink* trace_sink,
                                                    SharedMemoryOptions options) {
  return SharedMemoryTransport(
      create_impl(std::move(segment), std::move(edge_id), trace_sink, options, true));
}

SharedMemoryTransport::SharedMemoryTransport(SharedMemoryTransport&&) noexcept = default;
SharedMemoryTransport& SharedMemoryTransport::operator=(SharedMemoryTransport&&) noexcept = default;
SharedMemoryTransport::~SharedMemoryTransport() = default;

void SharedMemoryTransport::send(const Envelope& envelope) {
  if (!impl_ || impl_->locally_closed) throw std::runtime_error("send on closed shared memory");
  const auto serialized = serialize(envelope);
  const auto framed = frame(serialized);
  if (framed.size() > impl_->options.max_message_bytes)
    impl_->fail("frame exceeds configured maximum message size");

  const bool recovered = lock_header(*impl_->header);
  Unlock unlock(impl_->header->mutex);
  if (recovered) impl_->fail("recovered an abandoned mutex; recreate the segment");
  if (impl_->header->closed) impl_->fail("send on closed channel");
  if (!process_alive(impl_->header->consumer_pid)) {
    impl_->header->closed = true;
    impl_->fail("consumer process is no longer alive");
  }

  const auto pressure_start = std::chrono::steady_clock::now();
  const auto deadline = pressure_start + impl_->options.send_timeout;
  bool pressured = false;
  while (impl_->header->head - impl_->header->tail >= impl_->header->capacity) {
    pressured = true;
    if (impl_->options.backpressure == SharedMemoryBackpressure::reject) {
      impl_->trace_sink->on_backpressure(impl_->edge_id, {}, true);
      impl_->fail("ring is full (backpressure=reject)");
    }
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero())
      impl_->fail("send timed out waiting for ring capacity");
    const auto absolute = realtime_after(remaining);
    const int status = wait_condition(impl_->header->not_full, *impl_->header, absolute);
#if defined(__linux__)
    if (status == EOWNERDEAD)
      impl_->fail("peer died while holding the ring mutex; recreate the segment");
#endif
    if (status != 0 && status != ETIMEDOUT)
      impl_->fail("wait for ring capacity failed: " + std::string(std::strerror(status)));
    if (impl_->header->closed) impl_->fail("channel closed while waiting for ring capacity");
    if (!process_alive(impl_->header->consumer_pid)) {
      impl_->header->closed = true;
      impl_->fail("consumer process exited while producer was blocked");
    }
  }

  auto* slot = impl_->slot(impl_->header->head);
  const auto length = static_cast<std::uint32_t>(framed.size());
  std::memcpy(slot, &length, sizeof(length));
  std::memcpy(slot + sizeof(length), framed.data(), framed.size());
  ++impl_->header->head;
  ::pthread_cond_signal(&impl_->header->not_empty);
  unlock.release();
  if (pressured)
    impl_->trace_sink->on_backpressure(impl_->edge_id,
                                      std::chrono::steady_clock::now() - pressure_start, false);
  impl_->trace_sink->on_send(impl_->edge_id, envelope, framed.size());
}

std::optional<Envelope> SharedMemoryTransport::receive(std::chrono::milliseconds timeout) {
  if (!impl_ || impl_->locally_closed) throw std::runtime_error("receive on closed shared memory");
  const bool finite = timeout.count() >= 0;
  const auto deadline = finite ? std::chrono::steady_clock::now() + timeout
                               : std::chrono::steady_clock::time_point::max();
  const bool recovered = lock_header(*impl_->header);
  Unlock unlock(impl_->header->mutex);
  if (recovered) impl_->fail("recovered an abandoned mutex; recreate the segment");

  while (impl_->header->head == impl_->header->tail) {
    if (impl_->header->closed) return std::nullopt;
    if (impl_->header->producer_pid != 0 && !process_alive(impl_->header->producer_pid)) {
      impl_->header->closed = true;
      ::pthread_cond_broadcast(&impl_->header->not_full);
      return std::nullopt;
    }
    const auto now = std::chrono::steady_clock::now();
    if (finite && now >= deadline) return std::nullopt;
    const auto peer_check =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(kPeerCheckInterval);
    const auto wait = finite ? std::min(deadline - now, peer_check)
                             : std::chrono::steady_clock::duration(kPeerCheckInterval);
    const auto absolute = realtime_after(wait);
    const int status = wait_condition(impl_->header->not_empty, *impl_->header, absolute);
#if defined(__linux__)
    if (status == EOWNERDEAD)
      impl_->fail("peer died while holding the ring mutex; recreate the segment");
#endif
    if (status != 0 && status != ETIMEDOUT)
      impl_->fail("wait for message failed: " + std::string(std::strerror(status)));
  }

  auto* slot = impl_->slot(impl_->header->tail);
  std::uint32_t length{};
  std::memcpy(&length, slot, sizeof(length));
  if (length < 4 || length > impl_->header->max_message_bytes) {
    impl_->header->closed = true;
    impl_->fail("ring slot contains an invalid frame length");
  }
  std::vector<std::byte> framed(length);
  std::memcpy(framed.data(), slot + sizeof(length), length);
  ++impl_->header->tail;
  ::pthread_cond_signal(&impl_->header->not_full);
  unlock.release();

  std::uint32_t size{};
  try {
    size = decode_frame_size(std::span<const std::byte, 4>(framed.data(), 4));
  } catch (const std::exception& error) {
    impl_->fail("invalid frame prefix: " + std::string(error.what()));
  }
  if (size != framed.size() - 4) impl_->fail("ring slot frame length does not match prefix");
  Envelope envelope;
  try {
    envelope = deserialize(std::span(framed).subspan(4));
  } catch (const std::exception& error) {
    impl_->fail("malformed envelope: " + std::string(error.what()));
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  impl_->trace_sink->on_receive(
      impl_->edge_id, envelope, framed.size(),
      std::chrono::nanoseconds(std::max<std::int64_t>(0, now_ns - envelope.timestamp_ns)));
  return envelope;
}

void SharedMemoryTransport::close() {
  if (impl_ && !impl_->locally_closed) {
    impl_->trace_sink->on_connection(impl_->edge_id, ConnectionState::closed);
    impl_->close();
  }
}

std::string_view to_string(SharedMemoryBackpressure policy) noexcept {
  switch (policy) {
    case SharedMemoryBackpressure::block:
      return "block";
    case SharedMemoryBackpressure::reject:
      return "reject";
  }
  return "unknown";
}

}  // namespace graphx
