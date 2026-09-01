#include "graphx/in_process_transport.hpp"

#include <algorithm>
#include <stdexcept>

namespace graphx {

InProcessChannel::InProcessChannel(InProcessOptions options) : options_(options) {
  if (options_.capacity == 0 || options_.capacity > 65536)
    throw std::invalid_argument("in-process capacity must be between 1 and 65536");
  if (options_.send_timeout.count() <= 0)
    throw std::invalid_argument("in-process send timeout must be positive");
}

ChannelPushResult InProcessChannel::push(Envelope envelope) {
  const auto started = std::chrono::steady_clock::now();
  std::unique_lock lock(mutex_);
  if (closed_) return {ChannelPushStatus::closed, {}};
  bool pressured = false;
  if (queue_.size() >= options_.capacity) {
    pressured = true;
    if (options_.backpressure == InProcessBackpressure::reject)
      return {ChannelPushStatus::rejected, {}};
    if (!not_full_.wait_for(lock, options_.send_timeout,
                            [&] { return closed_ || queue_.size() < options_.capacity; }))
      return {ChannelPushStatus::timed_out, std::chrono::steady_clock::now() - started};
    if (closed_) return {ChannelPushStatus::closed, std::chrono::steady_clock::now() - started};
  }
  queue_.push_back(std::move(envelope));
  const auto waited =
      pressured ? std::chrono::steady_clock::now() - started : std::chrono::nanoseconds{};
  lock.unlock();
  not_empty_.notify_one();
  return {ChannelPushStatus::accepted, waited};
}

ChannelPopResult InProcessChannel::pop(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  const auto predicate = [&] { return closed_ || !queue_.empty(); };
  if (timeout.count() < 0)
    not_empty_.wait(lock, predicate);
  else if (!not_empty_.wait_for(lock, timeout, predicate))
    return {ChannelPopStatus::timeout, std::nullopt};
  if (queue_.empty()) return {ChannelPopStatus::closed, std::nullopt};
  auto envelope = std::move(queue_.front());
  queue_.pop_front();
  lock.unlock();
  not_full_.notify_one();
  return {ChannelPopStatus::message, std::move(envelope)};
}

void InProcessChannel::close() {
  {
    std::lock_guard lock(mutex_);
    closed_ = true;
  }
  not_empty_.notify_all();
  not_full_.notify_all();
}

void InProcessTransport::send(const Envelope& envelope) {
  if (closed_) throw std::runtime_error("send on closed in-process transport");
  const auto result = channel_->push(envelope);
  if (result.status != ChannelPushStatus::accepted) {
    const bool rejected = result.status == ChannelPushStatus::rejected;
    if (rejected || result.status == ChannelPushStatus::timed_out)
      trace_sink_->on_backpressure(edge_id_, result.wait, rejected);
    switch (result.status) {
      case ChannelPushStatus::closed:
        throw std::runtime_error("in-process channel is closed");
      case ChannelPushStatus::timed_out:
        throw std::runtime_error("in-process send timed out waiting for channel capacity");
      case ChannelPushStatus::rejected:
        throw std::runtime_error("in-process channel is full (backpressure=reject)");
      case ChannelPushStatus::accepted:
        break;
    }
  }
  if (result.wait.count() > 0) trace_sink_->on_backpressure(edge_id_, result.wait, false);
  trace_sink_->on_send(edge_id_, envelope, serialize(envelope).size());
}
ReceiveResult InProcessTransport::receive_result(std::chrono::milliseconds timeout) {
  if (closed_) return {ReceiveStatus::cancelled, std::nullopt};
  auto result = channel_->pop(timeout);
  if (result.status == ChannelPopStatus::message) {
    auto& envelope = *result.envelope;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    trace_sink_->on_receive(
        edge_id_, envelope, serialize(envelope).size(),
        std::chrono::nanoseconds(std::max<std::int64_t>(0, now_ns - envelope.timestamp_ns)));
    return {ReceiveStatus::message, std::move(result.envelope)};
  }
  if (closed_) return {ReceiveStatus::cancelled, std::nullopt};
  return {result.status == ChannelPopStatus::timeout ? ReceiveStatus::timeout
                                                     : ReceiveStatus::end_of_stream,
          std::nullopt};
}
void InProcessTransport::close() {
  if (closed_.exchange(true)) return;
  channel_->close();
  try {
    trace_sink_->on_connection(edge_id_, ConnectionState::closed);
  } catch (...) {
    // Cancellation and destruction must not depend on a best-effort observer.
  }
}

std::string_view to_string(InProcessBackpressure policy) noexcept {
  switch (policy) {
    case InProcessBackpressure::block:
      return "block";
    case InProcessBackpressure::reject:
      return "reject";
  }
  return "unknown";
}

}  // namespace graphx
