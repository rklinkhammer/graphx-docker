#pragma once

#include "graphx/transport.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>

namespace graphx {

class InProcessChannel {
 public:
  void push(Envelope envelope);
  std::optional<Envelope> pop(std::chrono::milliseconds timeout);
  void close();

 private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Envelope> queue_;
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
  std::optional<Envelope> receive(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) override;
  void close() override;

 private:
  std::shared_ptr<InProcessChannel> channel_;
  std::string edge_id_;
  TraceSink* trace_sink_{};
  NullTraceSink null_trace_sink_;
};

}  // namespace graphx
