#include "graphx/in_process_transport.hpp"

#include <stdexcept>

namespace graphx {

void InProcessChannel::push(Envelope envelope) {
  {
    std::lock_guard lock(mutex_);
    if (closed_) throw std::runtime_error("in-process channel is closed");
    queue_.push_back(std::move(envelope));
  }
  ready_.notify_one();
}

std::optional<Envelope> InProcessChannel::pop(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  const auto predicate = [&] { return closed_ || !queue_.empty(); };
  if (timeout.count() < 0) ready_.wait(lock, predicate);
  else if (!ready_.wait_for(lock, timeout, predicate)) return std::nullopt;
  if (queue_.empty()) return std::nullopt;
  auto envelope = std::move(queue_.front());
  queue_.pop_front();
  return envelope;
}

void InProcessChannel::close() {
  {
    std::lock_guard lock(mutex_);
    closed_ = true;
  }
  ready_.notify_all();
}

void InProcessTransport::send(const Envelope& envelope) { channel_->push(envelope); }
std::optional<Envelope> InProcessTransport::receive(std::chrono::milliseconds timeout) {
  return channel_->pop(timeout);
}
void InProcessTransport::close() { channel_->close(); }

}  // namespace graphx
