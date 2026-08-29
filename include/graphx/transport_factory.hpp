#pragma once

#include "graphx/config.hpp"
#include "graphx/in_process_transport.hpp"
#include "graphx/transport.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace graphx {

enum class ConnectionMode { connect, listen };

class TransportFactory {
 public:
  TransportPtr create(const EdgeConfig& edge, ConnectionMode mode, TraceSink* trace_sink = nullptr);

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::weak_ptr<InProcessChannel>> channels_;
};

}  // namespace graphx
