#pragma once

#include "graphx/transport.hpp"
#include "graphx/types.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphx {

class Node {
 public:
  explicit Node(std::string id) : id_(std::move(id)) {}
  virtual ~Node() = default;

  const std::string& id() const { return id_; }
  const std::vector<Port>& ports() const { return ports_; }
  void add_port(Port port) { ports_.push_back(std::move(port)); }
  virtual void run() = 0;

 private:
  std::string id_;
  std::vector<Port> ports_;
};

class FunctionNode final : public Node {
 public:
  using Function = std::function<Envelope(Envelope)>;
  FunctionNode(std::string id, Function function)
      : Node(std::move(id)), function_(std::move(function)) {}
  void run() override {}
  Envelope process(Envelope envelope) { return function_(std::move(envelope)); }

 private:
  Function function_;
};

}  // namespace graphx
