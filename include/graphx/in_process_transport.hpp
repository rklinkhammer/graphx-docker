#pragma once

#include "graphx/transport.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string_view>

namespace graphx {

enum class InProcessBackpressure { block, reject };

struct InProcessOptions {
  std::size_t capacity{64};
  InProcessBackpressure backpressure{InProcessBackpressure::block};
  std::chrono::milliseconds send_timeout{5000};
};

enum class ChannelPushStatus { accepted, closed, timed_out, rejected };
struct ChannelPushResult {
  ChannelPushStatus status{ChannelPushStatus::closed};
  std::chrono::nanoseconds wait{};
};

enum class ChannelPopStatus { message, timeout, closed };
struct ChannelPopResult {
  ChannelPopStatus status{ChannelPopStatus::closed};
  std::optional<Envelope> envelope;
};

class InProcessChannel {
 public:
  explicit InProcessChannel(InProcessOptions options = {});
  [[nodiscard]] ChannelPushResult push(Envelope envelope);
  [[nodiscard]] ChannelPopResult pop(std::chrono::milliseconds timeout);
  void close();
  [[nodiscard]] const InProcessOptions& options() const noexcept { return options_; }

 private:
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<Envelope> queue_;
  InProcessOptions options_;
  bool closed_{};
};

class InProcessTransport final : public Transport {
 public:
  explicit InProcessTransport(std::shared_ptr<InProcessChannel> channel,
                              std::string edge_id = {}, TraceSink* trace_sink = nullptr)
      : channel_(std::move(channel)), edge_id_(std::move(edge_id)), trace_sink_(trace_sink) {
    if (!trace_sink_) trace_sink_ = &null_trace_sink_;
    trace_sink_->on_connection(edge_id_, ConnectionState::connected);
  }
  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                      -1}) override {
    return std::move(receive_result(timeout).envelope);
  }
  ReceiveResult receive_result(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                   -1}) override;
  void close() override;

 private:
  std::shared_ptr<InProcessChannel> channel_;
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
  std::atomic<bool> closed_{};
};

[[nodiscard]] std::string_view to_string(InProcessBackpressure policy) noexcept;

}  // namespace graphx
