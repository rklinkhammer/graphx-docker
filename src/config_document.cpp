#include "config_document.hpp"

#include <openssl/evp.h>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/parser.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fcntl.h>
#include <iomanip>
#include <locale>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace graphx {
namespace {
void render_string(std::string_view input, std::string& output) {
  constexpr char hex[] = "0123456789abcdef";
  output += '"';
  for (const unsigned char ch : input) {
    if (ch == '"' || ch == '\\') {
      output += '\\';
      output += static_cast<char>(ch);
    } else if (ch < 32) {
      output += "\\u00";
      output += hex[ch >> 4];
      output += hex[ch & 15];
    } else {
      output += static_cast<char>(ch);
    }
  }
  output += '"';
}

void render(const ConfigValue& value, std::string& output, unsigned depth, bool pretty) {
  const auto newline = [&](unsigned level) {
    if (pretty) {
      output += '\n';
      output.append(level * 2U, ' ');
    }
  };
  if (value.is_object()) {
    output += '{';
    bool first = true;
    for (const auto& [key, item] : value.object()) {
      if (!first) output += ',';
      first = false;
      newline(depth + 1);
      render_string(key, output);
      output += pretty ? ": " : ":";
      render(item, output, depth + 1, pretty);
    }
    if (!first) newline(depth);
    output += '}';
  } else if (value.is_array()) {
    output += '[';
    bool first = true;
    for (const auto& item : value.array()) {
      if (!first) output += ',';
      first = false;
      newline(depth + 1);
      render(item, output, depth + 1, pretty);
    }
    if (!first) newline(depth);
    output += ']';
  } else if (value.is_string()) {
    render_string(value.text(), output);
  } else if (const auto* number = std::get_if<std::int64_t>(&value.value)) {
    output += std::to_string(*number);
  } else if (const auto* real = std::get_if<double>(&value.value)) {
    if (!std::isfinite(*real)) throw std::invalid_argument("non-finite configuration value");
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *real);
    if (converted.ec != std::errc{}) throw std::invalid_argument("unrepresentable number");
    output.append(buffer.data(), converted.ptr);
  } else if (const auto* boolean = std::get_if<bool>(&value.value)) {
    output += *boolean ? "true" : "false";
  } else {
    output += "null";
  }
}
}  // namespace

std::string config_value_json(const ConfigValue& value, bool pretty) {
  std::string result;
  render(value, result, 0, pretty);
  if (pretty) result += '\n';
  return result;
}

namespace config_internal {
namespace {
class SyntaxBoundary final : public YAML::EventHandler {
 public:
  void OnDocumentStart(const YAML::Mark&) override {
    if (++documents_ > 1) reject("E_SYNTAX", "$", "exactly one YAML document is required");
  }
  void OnDocumentEnd() override {}
  void OnNull(const YAML::Mark&, YAML::anchor_t anchor) override { check("?", anchor); }
  void OnAlias(const YAML::Mark&, YAML::anchor_t) override {
    reject("E_SYNTAX", "$", "YAML aliases are unsupported");
  }
  void OnAnchor(const YAML::Mark&, const std::string&) override {
    reject("E_SYNTAX", "$", "YAML anchors are unsupported");
  }
  void OnScalar(const YAML::Mark&, const std::string& tag, YAML::anchor_t anchor,
                const std::string&) override {
    check(tag, anchor);
  }
  void OnSequenceStart(const YAML::Mark&, const std::string& tag, YAML::anchor_t anchor,
                       YAML::EmitterStyle::value) override {
    enter(tag, anchor);
  }
  void OnSequenceEnd() override { --depth_; }
  void OnMapStart(const YAML::Mark&, const std::string& tag, YAML::anchor_t anchor,
                  YAML::EmitterStyle::value) override {
    enter(tag, anchor);
  }
  void OnMapEnd() override { --depth_; }

 private:
  static void check(const std::string& tag, YAML::anchor_t anchor) {
    if (anchor != 0 || (tag != "?" && tag != "!"))
      reject("E_SYNTAX", "$", "explicit YAML tags and anchors are unsupported");
  }
  void enter(const std::string& tag, YAML::anchor_t anchor) {
    check(tag, anchor);
    if (++depth_ > 64) reject("E_BOUND", "$", "configuration nesting exceeds 64 levels");
  }
  unsigned depth_{};
  unsigned documents_{};
};

Value convert(const YAML::Node& node) {
  if (node.IsNull()) return {};
  if (node.IsMap()) {
    Object object;
    for (const auto& pair : node) {
      if (!pair.first.IsScalar()) reject("E_SYNTAX", "$", "mapping keys must be strings");
      const auto key = pair.first.Scalar();
      if (key == "<<" || object.contains(key))
        reject("E_DUPLICATE_KEY", key, "duplicate or merge key");
      object.emplace(key, convert(pair.second));
    }
    return object;
  }
  if (node.IsSequence()) {
    Array items;
    for (const auto& item : node) items.push_back(convert(item));
    return items;
  }
  const auto& text = node.Scalar();
  if (node.Tag() == "!") return text;
  if (text == "true") return true;
  if (text == "false") return false;
  std::int64_t integer{};
  const auto number = std::from_chars(text.data(), text.data() + text.size(), integer);
  if (number.ec == std::errc{} && number.ptr == text.data() + text.size()) return integer;
  // Only JSON decimal real syntax is accepted as a number.
  static const std::regex real_pattern{
      R"(-?(0|[1-9][0-9]*)(\.[0-9]+([eE][+-]?[0-9]+)?|[eE][+-]?[0-9]+))"};
  if (std::regex_match(text, real_pattern)) {
    double real{};
    std::istringstream parsed(text);
    parsed.imbue(std::locale::classic());
    parsed >> real;
    if (!parsed || !parsed.eof() || !std::isfinite(real))
      reject("E_BOUND", "$", "invalid numeric value");
    return real;
  }
  return text;
}

bool matches_type(const Value& value, std::string_view type) {
  if (type == "object") return value.is_object();
  if (type == "array") return value.is_array();
  if (type == "string") return value.is_string();
  if (type == "null") return value.is_null();
  if (type == "boolean") return std::holds_alternative<bool>(value.value);
  if (type == "integer") return std::holds_alternative<std::int64_t>(value.value);
  if (type == "number")
    return matches_type(value, "integer") || std::holds_alternative<double>(value.value);
  throw std::logic_error("unknown embedded schema type");
}
}  // namespace

[[noreturn]] void reject(std::string code, std::string path, std::string message) {
  throw ConfigError({{std::move(path), std::move(message), std::move(code)}});
}

Value parse_document(std::string_view source) {
  try {
    std::istringstream stream{std::string(source)};
    YAML::Parser parser(stream);
    SyntaxBoundary boundary;
    while (parser.HandleNextDocument(boundary)) {
    }
    return convert(YAML::Load(std::string(source)));
  } catch (const YAML::Exception&) {
    reject("E_SYNTAX", "$", "malformed YAML/JSON configuration");
  }
}

std::filesystem::path confined_path(const std::filesystem::path& root,
                                    const std::filesystem::path& path) {
  const auto base = std::filesystem::absolute(root).lexically_normal();
  const auto target = std::filesystem::absolute(path).lexically_normal();
  const auto relative = target.lexically_relative(base);
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    reject("E_PATH_ESCAPE", "catalog", "path escapes the explicit catalog root");
  std::filesystem::path current;
  for (const auto& part : target) {
    current /= part;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
      reject("E_PATH_ESCAPE", "catalog", "symlink components are forbidden");
  }
  return target;
}

std::string read_document(const std::filesystem::path& path, std::size_t maximum) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) reject("E_INPUT", "$", "cannot open configuration file: " + path.string());
  struct stat metadata{};
  if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
      static_cast<std::uint64_t>(metadata.st_size) > maximum) {
    ::close(descriptor);
    reject("E_BOUND", "$", "input must be a bounded regular file");
  }
  std::string result(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t offset{};
  while (offset < result.size()) {
    const auto count = ::read(descriptor, result.data() + offset, result.size() - offset);
    if (count <= 0) {
      ::close(descriptor);
      reject("E_INPUT", "$", "input changed or could not be read");
    }
    offset += static_cast<std::size_t>(count);
  }
  char extra{};
  const bool grew = ::read(descriptor, &extra, 1) != 0;
  ::close(descriptor);
  if (grew) reject("E_BOUND", "$", "input changed while reading");
  return result;
}

std::string sha256(std::string_view value) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned length{};
  if (EVP_Digest(value.data(), value.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("SHA-256 failed");
  std::ostringstream output;
  for (unsigned index = 0; index < length; ++index)
    output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(digest[index]);
  return output.str();
}

void validate_shape(const Value& value, const Value& schema, std::string_view path,
                    const Value* root) {
  if (!root) root = &schema;
  const auto fail = [&](std::string message) {
    reject("E_SCHEMA", std::string(path), std::move(message));
  };
  if (schema.contains("$ref")) {
    const auto reference = schema.at("$ref").text();
    if (!reference.starts_with("#/$defs/"))
      throw std::logic_error("nonlocal embedded schema reference");
    validate_shape(value, root->at("$defs").at(reference.substr(8)), path, root);
    return;
  }
  if (schema.contains("allOf"))
    for (const auto& condition : schema.at("allOf").array())
      validate_shape(value, condition, path, root);
  if (schema.contains("if")) {
    bool applies = true;
    try {
      validate_shape(value, schema.at("if"), path, root);
    } catch (const ConfigError&) {
      applies = false;
    }
    if (applies && schema.contains("then")) validate_shape(value, schema.at("then"), path, root);
    if (!applies && schema.contains("else")) validate_shape(value, schema.at("else"), path, root);
  }
  if (schema.contains("type")) {
    const auto& type = schema.at("type");
    const bool matches = type.is_string()
                             ? matches_type(value, type.text())
                             : std::ranges::any_of(type.array(), [&](const auto& item) {
                                 return matches_type(value, item.text());
                               });
    if (!matches) fail("incorrect value type");
  }
  if (schema.contains("const") && value != schema.at("const"))
    fail("value does not match required constant");
  if (schema.contains("enum") &&
      std::ranges::find(schema.at("enum").array(), value) == schema.at("enum").array().end())
    fail("value is not in the allowed enumeration");
  if (value.is_object()) {
    if (schema.contains("maxProperties") &&
        value.object().size() > static_cast<std::size_t>(schema.at("maxProperties").integer()))
      fail("too many properties");
    if (schema.contains("required"))
      for (const auto& key : schema.at("required").array())
        if (!value.contains(key.text())) fail("missing required property " + key.text());
    for (const auto& [key, item] : value.object()) {
      if (key.size() > 128) fail("property name exceeds 128 bytes");
      if (schema.contains("propertyNames"))
        validate_shape(key, schema.at("propertyNames"), path, root);
      const auto child = std::string(path) + "." + key;
      if (schema.contains("properties") && schema.at("properties").contains(key)) {
        validate_shape(item, schema.at("properties").at(key), child, root);
      } else if (schema.contains("additionalProperties")) {
        const auto& additional = schema.at("additionalProperties");
        if (additional.is_object())
          validate_shape(item, additional, child, root);
        else if (!additional.boolean())
          reject("E_SCHEMA", child, "unknown property");
      }
    }
  } else if (value.is_array()) {
    if (schema.contains("maxItems") &&
        value.array().size() > static_cast<std::size_t>(schema.at("maxItems").integer()))
      fail("too many array entries");
    if (schema.contains("minItems") &&
        value.array().size() < static_cast<std::size_t>(schema.at("minItems").integer()))
      fail("too few array entries");
    for (std::size_t index = 0; index < value.array().size(); ++index)
      if (schema.contains("items"))
        validate_shape(value.array()[index], schema.at("items"),
                       std::string(path) + "[" + std::to_string(index) + "]", root);
    if (bool_or(schema, "uniqueItems", false)) {
      std::set<std::string> unique;
      for (const auto& item : value.array())
        if (!unique.insert(config_value_json(item, false)).second) fail("duplicate array entry");
    }
  } else if (value.is_string()) {
    if (value.text().size() > 4096) fail("text exceeds 4096 bytes");
    if (schema.contains("minLength") &&
        value.text().size() < static_cast<std::size_t>(schema.at("minLength").integer()))
      fail("text is too short");
    if (schema.contains("maxLength") &&
        value.text().size() > static_cast<std::size_t>(schema.at("maxLength").integer()))
      fail("text is too long");
    if (schema.contains("pattern") &&
        !std::regex_search(value.text(), std::regex(schema.at("pattern").text())))
      fail("text does not match required pattern");
  } else if (matches_type(value, "number")) {
    const double number = std::holds_alternative<double>(value.value)
                              ? std::get<double>(value.value)
                              : static_cast<double>(value.integer());
    if (schema.contains("minimum") && number < static_cast<double>(schema.at("minimum").integer()))
      fail("number is below minimum");
    if (schema.contains("maximum") && number > static_cast<double>(schema.at("maximum").integer()))
      fail("number is above maximum");
  }
}

Value merged(Value defaults, const Value& explicit_values) {
  for (const auto& [key, value] : explicit_values.object()) {
    if (defaults.contains(key) && defaults.at(key).is_object() && value.is_object())
      defaults[key] = merged(defaults.at(key), value);
    else
      defaults[key] = value;
  }
  return defaults;
}
std::string string_or(const Value& value, std::string_view key, std::string fallback) {
  return value.contains(key) ? value.at(key).text() : std::move(fallback);
}
std::int64_t integer_or(const Value& value, std::string_view key, std::int64_t fallback) {
  return value.contains(key) ? value.at(key).integer() : fallback;
}
bool bool_or(const Value& value, std::string_view key, bool fallback) {
  return value.contains(key) ? value.at(key).boolean() : fallback;
}
}  // namespace config_internal
}  // namespace graphx

namespace graphx::config_internal {
std::string resource_name(std::string_view graph, std::string_view kind, std::string_view id) {
  std::string key(graph);
  key += '\0';
  key += kind;
  key += '\0';
  key += id;
  const char prefix = kind == "bridge"      ? 'b'
                      : kind == "interface" ? 'i'
                      : kind == "peer"      ? 'p'
                      : kind == "namespace" ? 'n'
                                            : 't';
  return "gx" + std::string(1, prefix) + sha256(key).substr(0, 12);
}

}  // namespace graphx::config_internal
