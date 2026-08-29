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
  explicit InProcessTransport(std::shared_ptr<InProcessChannel> channel)
      : channel_(std::move(channel)) {}
  void send(const Envelope& envelope) override;
  std::optional<Envelope> receive(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) override;
  void close() override;

 private:
  std::shared_ptr<InProcessChannel> channel_;
};

}  // namespace graphx
