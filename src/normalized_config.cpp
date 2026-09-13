#include "graphx/normalized_config.hpp"
#include "config_document.hpp"
#include "graphx/config_schemas.hpp"

namespace graphx {
std::string normalize_config_json(const GraphConfig& config) {
  static const auto schema = config_internal::parse_document(config_internal::normalized_schema);
  config_internal::validate_shape(config.resolved, schema);
  return config_value_json(config.resolved);
}
}  // namespace graphx
