#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace graphx {

// Bounded, schema-checked configuration values. No YAML nodes cross the public API.
struct ConfigValue {
  using Array = std::vector<ConfigValue>;
  using Object = std::map<std::string, ConfigValue, std::less<>>;
  using Storage =
      std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;
  Storage value;

  ConfigValue() = default;
  ConfigValue(bool input) : value(input) {}
  ConfigValue(std::int64_t input) : value(input) {}
  ConfigValue(int input) : value(static_cast<std::int64_t>(input)) {}
  ConfigValue(double input) : value(input) {}
  ConfigValue(std::string input) : value(std::move(input)) {}
  ConfigValue(const char* input) : value(std::string(input)) {}
  ConfigValue(Array input) : value(std::move(input)) {}
  ConfigValue(Object input) : value(std::move(input)) {}

  [[nodiscard]] bool is_object() const { return std::holds_alternative<Object>(value); }
  [[nodiscard]] bool is_array() const { return std::holds_alternative<Array>(value); }
  [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(value); }
  [[nodiscard]] bool is_null() const { return std::holds_alternative<std::monostate>(value); }
  [[nodiscard]] const Object& object() const { return std::get<Object>(value); }
  [[nodiscard]] Object& object() { return std::get<Object>(value); }
  [[nodiscard]] const Array& array() const { return std::get<Array>(value); }
  [[nodiscard]] Array& array() { return std::get<Array>(value); }
  [[nodiscard]] const std::string& text() const { return std::get<std::string>(value); }
  [[nodiscard]] std::int64_t integer() const { return std::get<std::int64_t>(value); }
  [[nodiscard]] bool boolean() const { return std::get<bool>(value); }
  [[nodiscard]] bool contains(std::string_view key) const {
    return is_object() && object().contains(key);
  }
  [[nodiscard]] const ConfigValue& at(std::string_view key) const {
    return object().at(std::string(key));
  }
  ConfigValue& operator[](std::string key) {
    if (is_null()) value = Object{};
    return object()[std::move(key)];
  }
  bool operator==(const ConfigValue&) const = default;
};

[[nodiscard]] std::string config_value_json(const ConfigValue& value, bool pretty = true);

}  // namespace graphx
