#pragma once

#include "graphx/config.hpp"
#include "graphx/config_value.hpp"

#include <filesystem>
#include <string_view>

namespace graphx::config_internal {
using Value = ConfigValue;
using Object = Value::Object;
using Array = Value::Array;

[[noreturn]] void reject(std::string code, std::string path, std::string message);
Value parse_document(std::string_view source);
std::string read_document(const std::filesystem::path& path, std::size_t maximum = kMaxConfigBytes);
std::filesystem::path confined_path(const std::filesystem::path& root,
                                    const std::filesystem::path& path);
std::string sha256(std::string_view value);
void validate_shape(const Value& value, const Value& schema, std::string_view path = "$",
                    const Value* root = nullptr);
Value merged(Value defaults, const Value& explicit_values);
std::string string_or(const Value& value, std::string_view key, std::string fallback = {});
std::int64_t integer_or(const Value& value, std::string_view key, std::int64_t fallback);
bool bool_or(const Value& value, std::string_view key, bool fallback);
}  // namespace graphx::config_internal
