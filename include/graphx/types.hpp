#pragma once

#include <cstdint>
#include <string>

namespace graphx {

using Bytes = std::string;

enum class Direction { input, output };

struct Port {
  std::string name;
  Direction direction{Direction::input};
  std::string schema;
};

struct Edge {
  std::string id;
  std::string from_node;
  std::string from_port;
  std::string to_node;
  std::string to_port;
  std::string transport;
};

}  // namespace graphx
