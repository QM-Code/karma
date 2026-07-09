#include "karma/prefabs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/math.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;

components::TransformComponent composeTransform(
    const components::TransformComponent& parent,
    const components::TransformComponent& local) {
  components::TransformComponent transform{};
  const math::Vec3 scaled_local = math::multiply(local.localPosition(), parent.worldScale());
  const math::Vec3 rotated_local = math::rotateVec(parent.worldRotation(), scaled_local);
  transform.setLocalPosition(math::add(parent.worldPosition(), rotated_local));
  transform.setLocalRotation(math::mul(parent.worldRotation(), local.localRotation()));
  transform.setLocalScale(math::multiply(parent.worldScale(), local.localScale()));
  return transform;
}

std::filesystem::path resolvePrefabPath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "prefab.json";
  }
  if (path.extension().empty()) {
    return path / "prefab.json";
  }
  return path;
}

bool readRequiredUint32(const Json& object,
                        std::string_view key,
                        uint32_t& out_value,
                        const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    spdlog::error("Prefab '{}' is missing numeric '{}' field", path.string(), key);
    return false;
  }
  uint64_t value = 0;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      spdlog::error("Prefab '{}' has out-of-range '{}' field", path.string(), key);
      return false;
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > static_cast<uint64_t>(UINT32_MAX)) {
    spdlog::error("Prefab '{}' has out-of-range '{}' field", path.string(), key);
    return false;
  }
  out_value = static_cast<uint32_t>(value);
  return true;
}

bool readRequiredSize(const Json& object,
                      std::string_view key,
                      size_t& out_value,
                      const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    spdlog::error("Prefab '{}' is missing numeric '{}' field", path.string(), key);
    return false;
  }
  uint64_t value = 0;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      spdlog::error("Prefab '{}' has negative '{}' field", path.string(), key);
      return false;
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    spdlog::error("Prefab '{}' has out-of-range '{}' field", path.string(), key);
    return false;
  }
  out_value = static_cast<size_t>(value);
  return true;
}

bool readRequiredString(const Json& object,
                        std::string_view key,
                        std::string& out_value,
                        const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    spdlog::error("Prefab '{}' is missing string '{}' field", path.string(), key);
    return false;
  }
  out_value = it->get<std::string>();
  return true;
}

enum class PrefabVariableType {
  Float,
  Int,
  Bool,
  String,
  Vec3,
  Color,
};

struct PrefabVariable {
  PrefabVariableType type = PrefabVariableType::Float;
  Json value;
};

using PrefabVariableMap = std::unordered_map<std::string, PrefabVariable>;

std::optional<PrefabVariableType> parseVariableType(std::string_view value) {
  if (value == "float") {
    return PrefabVariableType::Float;
  }
  if (value == "int") {
    return PrefabVariableType::Int;
  }
  if (value == "bool") {
    return PrefabVariableType::Bool;
  }
  if (value == "string") {
    return PrefabVariableType::String;
  }
  if (value == "vec3") {
    return PrefabVariableType::Vec3;
  }
  if (value == "color") {
    return PrefabVariableType::Color;
  }
  return std::nullopt;
}

const char* variableTypeName(PrefabVariableType type) {
  switch (type) {
    case PrefabVariableType::Float:
      return "float";
    case PrefabVariableType::Int:
      return "int";
    case PrefabVariableType::Bool:
      return "bool";
    case PrefabVariableType::String:
      return "string";
    case PrefabVariableType::Vec3:
      return "vec3";
    case PrefabVariableType::Color:
      return "color";
  }
  return "float";
}

std::optional<int64_t> readJsonInteger(const Json& value) {
  if (value.is_number_unsigned()) {
    const uint64_t unsigned_value = value.get<uint64_t>();
    if (unsigned_value >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int64_t>(unsigned_value);
  }
  if (value.is_number_integer()) {
    return value.get<int64_t>();
  }
  return std::nullopt;
}

bool normalizeNumberArray(const Json& value, size_t size, Json& out_value) {
  if (!value.is_array() || value.size() != size) {
    return false;
  }
  Json normalized = Json::array();
  for (const Json& element : value) {
    if (!element.is_number()) {
      return false;
    }
    const double scalar = element.get<double>();
    if (!std::isfinite(scalar)) {
      return false;
    }
    normalized.push_back(scalar);
  }
  out_value = std::move(normalized);
  return true;
}

bool normalizeVariableValue(PrefabVariableType type,
                            const Json& value,
                            Json& out_value) {
  switch (type) {
    case PrefabVariableType::Float: {
      if (!value.is_number()) {
        return false;
      }
      const double scalar = value.get<double>();
      if (!std::isfinite(scalar)) {
        return false;
      }
      out_value = scalar;
      return true;
    }
    case PrefabVariableType::Int: {
      const std::optional<int64_t> integer = readJsonInteger(value);
      if (!integer.has_value()) {
        return false;
      }
      out_value = *integer;
      return true;
    }
    case PrefabVariableType::Bool:
      if (!value.is_boolean()) {
        return false;
      }
      out_value = value;
      return true;
    case PrefabVariableType::String:
      if (!value.is_string()) {
        return false;
      }
      out_value = value;
      return true;
    case PrefabVariableType::Vec3:
      return normalizeNumberArray(value, 3u, out_value);
    case PrefabVariableType::Color:
      return normalizeNumberArray(value, 4u, out_value);
  }
  return false;
}

bool isIdentifierStart(char value) {
  const unsigned char c = static_cast<unsigned char>(value);
  return std::isalpha(c) || value == '_';
}

bool isIdentifierChar(char value) {
  const unsigned char c = static_cast<unsigned char>(value);
  return std::isalnum(c) || value == '_';
}

class ExpressionParser {
 public:
  ExpressionParser(std::string_view input, const PrefabVariableMap& variables)
      : input_(input), variables_(variables) {}

  bool parse(double& out_value, std::string& out_error) {
    skipWhitespace();
    if (!parseExpression(out_value)) {
      out_error = error_;
      return false;
    }
    skipWhitespace();
    if (position_ != input_.size()) {
      setError("unexpected token");
      out_error = error_;
      return false;
    }
    if (!std::isfinite(out_value)) {
      setError("non-finite result");
      out_error = error_;
      return false;
    }
    return true;
  }

 private:
  void skipWhitespace() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_]))) {
      ++position_;
    }
  }

  bool consume(char expected) {
    skipWhitespace();
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void setError(std::string error) {
    if (error_.empty()) {
      error_ = std::move(error);
    }
  }

  bool parseExpression(double& out_value) {
    if (!parseTerm(out_value)) {
      return false;
    }
    while (true) {
      if (consume('+')) {
        double rhs = 0.0;
        if (!parseTerm(rhs)) {
          return false;
        }
        out_value += rhs;
      } else if (consume('-')) {
        double rhs = 0.0;
        if (!parseTerm(rhs)) {
          return false;
        }
        out_value -= rhs;
      } else {
        break;
      }
      if (!std::isfinite(out_value)) {
        setError("non-finite result");
        return false;
      }
    }
    return true;
  }

  bool parseTerm(double& out_value) {
    if (!parseUnary(out_value)) {
      return false;
    }
    while (true) {
      if (consume('*')) {
        double rhs = 0.0;
        if (!parseUnary(rhs)) {
          return false;
        }
        out_value *= rhs;
      } else if (consume('/')) {
        double rhs = 0.0;
        if (!parseUnary(rhs)) {
          return false;
        }
        if (rhs == 0.0) {
          setError("division by zero");
          return false;
        }
        out_value /= rhs;
      } else {
        break;
      }
      if (!std::isfinite(out_value)) {
        setError("non-finite result");
        return false;
      }
    }
    return true;
  }

  bool parseUnary(double& out_value) {
    if (consume('+')) {
      return parseUnary(out_value);
    }
    if (consume('-')) {
      if (!parseUnary(out_value)) {
        return false;
      }
      out_value = -out_value;
      return std::isfinite(out_value);
    }
    return parsePrimary(out_value);
  }

  bool parsePrimary(double& out_value) {
    skipWhitespace();
    if (position_ >= input_.size()) {
      setError("expected expression value");
      return false;
    }
    const char current = input_[position_];
    if (current == '(') {
      ++position_;
      if (!parseExpression(out_value)) {
        return false;
      }
      if (!consume(')')) {
        setError("missing ')'");
        return false;
      }
      return true;
    }
    if (std::isdigit(static_cast<unsigned char>(current)) ||
        (current == '.' && position_ + 1u < input_.size() &&
         std::isdigit(static_cast<unsigned char>(input_[position_ + 1u])))) {
      return parseNumber(out_value);
    }
    if (isIdentifierStart(current)) {
      return parseIdentifier(out_value);
    }
    setError("expected expression value");
    return false;
  }

  bool parseNumber(double& out_value) {
    const size_t start = position_;
    bool has_digits = false;
    while (position_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[position_]))) {
      has_digits = true;
      ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        has_digits = true;
        ++position_;
      }
    }
    if (!has_digits) {
      setError("invalid numeric literal");
      return false;
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      const size_t exponent = position_;
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      bool has_exponent_digits = false;
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) {
        has_exponent_digits = true;
        ++position_;
      }
      if (!has_exponent_digits) {
        position_ = exponent;
        setError("invalid numeric exponent");
        return false;
      }
    }

    try {
      out_value =
          std::stod(std::string(input_.substr(start, position_ - start)));
    } catch (const std::exception&) {
      setError("invalid numeric literal");
      return false;
    }
    if (!std::isfinite(out_value)) {
      setError("non-finite numeric literal");
      return false;
    }
    return true;
  }

  bool parseIdentifier(double& out_value) {
    const size_t start = position_;
    ++position_;
    while (position_ < input_.size() && isIdentifierChar(input_[position_])) {
      ++position_;
    }
    const std::string name(input_.substr(start, position_ - start));
    const auto it = variables_.find(name);
    if (it == variables_.end()) {
      setError("unknown variable '" + name + "'");
      return false;
    }
    if (it->second.type == PrefabVariableType::Float) {
      out_value = it->second.value.get<double>();
      return true;
    }
    if (it->second.type == PrefabVariableType::Int) {
      out_value = static_cast<double>(it->second.value.get<int64_t>());
      return true;
    }
    setError("variable '" + name + "' is not numeric");
    return false;
  }

  std::string_view input_;
  const PrefabVariableMap& variables_;
  size_t position_ = 0u;
  std::string error_;
};

std::optional<PrefabVariableMap> buildResolvedVariables(
    const PrefabDocument& document,
    const PrefabInstantiateDesc& desc,
    const std::filesystem::path& path) {
  PrefabVariableMap variables;
  if (!document.variables.is_object()) {
    spdlog::error("Prefab '{}' has non-object 'variables' field", path.string());
    return std::nullopt;
  }

  variables.reserve(document.variables.size());
  for (auto it = document.variables.begin(); it != document.variables.end(); ++it) {
    const std::string name = it.key();
    const Json& declaration = it.value();
    if (!declaration.is_object()) {
      spdlog::error("Prefab '{}' variable '{}' must be an object",
                    path.string(),
                    name);
      return std::nullopt;
    }

    const auto type_it = declaration.find("type");
    if (type_it == declaration.end() || !type_it->is_string()) {
      spdlog::error("Prefab '{}' variable '{}' is missing string 'type'",
                    path.string(),
                    name);
      return std::nullopt;
    }
    const std::string type_name = type_it->get<std::string>();
    const std::optional<PrefabVariableType> type = parseVariableType(type_name);
    if (!type.has_value()) {
      spdlog::error("Prefab '{}' variable '{}' has unsupported type '{}'",
                    path.string(),
                    name,
                    type_name);
      return std::nullopt;
    }

    const auto default_it = declaration.find("default");
    if (default_it == declaration.end()) {
      spdlog::error("Prefab '{}' variable '{}' is missing 'default'",
                    path.string(),
                    name);
      return std::nullopt;
    }
    Json normalized_default;
    if (!normalizeVariableValue(*type, *default_it, normalized_default)) {
      spdlog::error("Prefab '{}' variable '{}' default does not match type '{}'",
                    path.string(),
                    name,
                    variableTypeName(*type));
      return std::nullopt;
    }

    variables.emplace(name, PrefabVariable{*type, std::move(normalized_default)});
  }

  for (const auto& [name, override_value] : desc.variables) {
    const auto variable_it = variables.find(name);
    if (variable_it == variables.end()) {
      spdlog::error("Prefab '{}' received unknown variable override '{}'",
                    path.string(),
                    name);
      return std::nullopt;
    }
    Json normalized_override;
    if (!normalizeVariableValue(variable_it->second.type,
                                override_value,
                                normalized_override)) {
      spdlog::error("Prefab '{}' variable override '{}' does not match type '{}'",
                    path.string(),
                    name,
                    variableTypeName(variable_it->second.type));
      return std::nullopt;
    }
    variable_it->second.value = std::move(normalized_override);
  }

  return variables;
}

bool resolveJsonMarkers(Json& value,
                        const PrefabVariableMap& variables,
                        const std::filesystem::path& path,
                        std::string_view context) {
  if (value.is_array()) {
    for (Json& element : value) {
      if (!resolveJsonMarkers(element, variables, path, context)) {
        return false;
      }
    }
    return true;
  }

  if (!value.is_object()) {
    return true;
  }

  const bool has_var = value.contains("$var");
  const bool has_expr = value.contains("$expr");
  if (has_var || has_expr) {
    if (value.size() != 1u || (has_var && has_expr)) {
      spdlog::error("Prefab '{}' {} has invalid variable marker",
                    path.string(),
                    context);
      return false;
    }
    if (has_var) {
      const Json& marker = value["$var"];
      if (!marker.is_string()) {
        spdlog::error("Prefab '{}' {} has non-string $var marker",
                      path.string(),
                      context);
        return false;
      }
      const std::string name = marker.get<std::string>();
      const auto variable_it = variables.find(name);
      if (variable_it == variables.end()) {
        spdlog::error("Prefab '{}' {} references unknown variable '{}'",
                      path.string(),
                      context,
                      name);
        return false;
      }
      value = variable_it->second.value;
      return true;
    }

    const Json& marker = value["$expr"];
    if (!marker.is_string()) {
      spdlog::error("Prefab '{}' {} has non-string $expr marker",
                    path.string(),
                    context);
      return false;
    }
    const std::string expression = marker.get<std::string>();
    ExpressionParser parser(expression, variables);
    double result = 0.0;
    std::string error;
    if (!parser.parse(result, error)) {
      spdlog::error("Prefab '{}' {} has invalid expression '{}': {}",
                    path.string(),
                    context,
                    expression,
                    error);
      return false;
    }
    value = result;
    return true;
  }

  for (auto it = value.begin(); it != value.end(); ++it) {
    if (!resolveJsonMarkers(it.value(), variables, path, context)) {
      return false;
    }
  }
  return true;
}

std::optional<PrefabDocument> resolvePrefabVariables(
    const PrefabDocument& document,
    const PrefabInstantiateDesc& desc,
    const std::filesystem::path& path) {
  std::optional<PrefabVariableMap> variables =
      buildResolvedVariables(document, desc, path);
  if (!variables.has_value()) {
    return std::nullopt;
  }

  PrefabDocument resolved = document;
  for (PrefabNode& node : resolved.nodes) {
    const std::string context =
        node.name.empty() ? "node '<unnamed>'" : "node '" + node.name + "'";
    if (!resolveJsonMarkers(node.components, *variables, path, context)) {
      return std::nullopt;
    }
  }
  return resolved;
}

bool validateParents(const PrefabDocument& document,
                     const std::filesystem::path& path) {
  if (document.nodes.empty()) {
    spdlog::error("Prefab '{}' contains no nodes", path.string());
    return false;
  }
  if (document.root >= document.nodes.size()) {
    spdlog::error("Prefab '{}' root index is out of range", path.string());
    return false;
  }
  if (document.nodes[document.root].parent.has_value()) {
    spdlog::error("Prefab '{}' root node must not have a parent", path.string());
    return false;
  }

  std::vector<uint8_t> visit_state(document.nodes.size(), 0u);
  visit_state[document.root] = 2u;
  for (size_t index = 0; index < document.nodes.size(); ++index) {
    const std::optional<size_t> parent = document.nodes[index].parent;
    if (parent.has_value() && *parent >= document.nodes.size()) {
      spdlog::error("Prefab '{}' node {} parent index is out of range",
                    path.string(),
                    index);
      return false;
    }

    std::vector<size_t> path_nodes;
    size_t cursor = index;
    while (cursor != document.root) {
      if (visit_state[cursor] == 2u) {
        break;
      }
      if (visit_state[cursor] == 1u) {
        spdlog::error("Prefab '{}' contains a parent cycle at node {}",
                      path.string(),
                      index);
        return false;
      }
      visit_state[cursor] = 1u;
      path_nodes.push_back(cursor);
      if (!document.nodes[cursor].parent.has_value()) {
        spdlog::error("Prefab '{}' node {} is outside the declared root subtree",
                      path.string(),
                      index);
        return false;
      }
      cursor = *document.nodes[cursor].parent;
    }
    for (const size_t node : path_nodes) {
      visit_state[node] = 2u;
    }
  }

  return true;
}

std::optional<PrefabDocument> parseDocument(const Json& json,
                                            const std::filesystem::path& path) {
  if (!json.is_object()) {
    spdlog::error("Prefab '{}' root JSON value must be an object", path.string());
    return std::nullopt;
  }

  PrefabDocument document{};
  if (!readRequiredUint32(json, "version", document.version, path)) {
    return std::nullopt;
  }
  if (document.version != 2u) {
    spdlog::error("Prefab '{}' has unsupported version {}", path.string(), document.version);
    return std::nullopt;
  }
  if (!readRequiredSize(json, "root", document.root, path)) {
    return std::nullopt;
  }
  const auto variables_it = json.find("variables");
  if (variables_it != json.end()) {
    if (!variables_it->is_object()) {
      spdlog::error("Prefab '{}' has non-object 'variables' field", path.string());
      return std::nullopt;
    }
    document.variables = *variables_it;
  }

  const auto nodes_it = json.find("nodes");
  if (nodes_it == json.end() || !nodes_it->is_array()) {
    spdlog::error("Prefab '{}' is missing array 'nodes' field", path.string());
    return std::nullopt;
  }

  std::unordered_set<uint32_t> ids;
  document.nodes.reserve(nodes_it->size());
  for (size_t index = 0; index < nodes_it->size(); ++index) {
    const Json& node_json = (*nodes_it)[index];
    if (!node_json.is_object()) {
      spdlog::error("Prefab '{}' node {} must be an object", path.string(), index);
      return std::nullopt;
    }

    PrefabNode node{};
    if (!readRequiredUint32(node_json, "id", node.id, path) ||
        !readRequiredString(node_json, "name", node.name, path)) {
      return std::nullopt;
    }
    if (!ids.insert(node.id).second) {
      spdlog::error("Prefab '{}' contains duplicate node id {}", path.string(), node.id);
      return std::nullopt;
    }

    const auto parent_it = node_json.find("parent");
    if (parent_it == node_json.end()) {
      spdlog::error("Prefab '{}' node {} is missing 'parent' field", path.string(), index);
      return std::nullopt;
    }
    if (parent_it->is_null()) {
      node.parent.reset();
    } else if (parent_it->is_number_unsigned() || parent_it->is_number_integer()) {
      uint64_t parent = 0;
      if (parent_it->is_number_unsigned()) {
        parent = parent_it->get<uint64_t>();
      } else {
        const int64_t signed_parent = parent_it->get<int64_t>();
        if (signed_parent < 0) {
          spdlog::error("Prefab '{}' node {} has negative parent index",
                        path.string(),
                        index);
          return std::nullopt;
        }
        parent = static_cast<uint64_t>(signed_parent);
      }
      if (parent > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        spdlog::error("Prefab '{}' node {} has out-of-range parent index",
                      path.string(),
                      index);
        return std::nullopt;
      }
      node.parent = static_cast<size_t>(parent);
    } else {
      spdlog::error("Prefab '{}' node {} parent must be null or numeric",
                    path.string(),
                    index);
      return std::nullopt;
    }

    const auto components_it = node_json.find("components");
    if (components_it == node_json.end() || !components_it->is_object()) {
      spdlog::error("Prefab '{}' node {} is missing object 'components' field",
                    path.string(),
                    index);
      return std::nullopt;
    }
    node.components = *components_it;
    document.nodes.push_back(std::move(node));
  }

  if (!validateParents(document, path)) {
    return std::nullopt;
  }
  return document;
}

std::optional<PrefabDocument> loadDocument(const std::filesystem::path& input_path) {
  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::ifstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open prefab '{}'", path.string());
    return std::nullopt;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse prefab '{}': {}", path.string(), e.what());
    return std::nullopt;
  }

  return parseDocument(json, path);
}

Json toJson(const PrefabDocument& document) {
  Json nodes = Json::array();
  for (const PrefabNode& node : document.nodes) {
    nodes.push_back(Json{
        {"id", node.id},
        {"name", node.name},
        {"parent", node.parent.has_value() ? Json(*node.parent) : Json(nullptr)},
        {"components", node.components},
    });
  }
  Json json{
      {"version", document.version},
      {"root", document.root},
      {"nodes", std::move(nodes)},
  };
  if (document.variables.is_object() && !document.variables.empty()) {
    json["variables"] = document.variables;
  }
  return json;
}

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

struct CachedPrefabPackage {
  assets::AssetRegistry* assets = nullptr;
  assets::AssetPackageHandle handle;
  uint32_t ref_count = 0u;
};

struct PrefabRootKey {
  uint64_t world = 0u;
  uint64_t entity = 0u;

  bool operator==(const PrefabRootKey&) const = default;
};

struct PrefabRootKeyHash {
  size_t operator()(const PrefabRootKey& key) const {
    const size_t world_hash = std::hash<uint64_t>{}(key.world);
    const size_t entity_hash = std::hash<uint64_t>{}(key.entity);
    return world_hash ^ (entity_hash + 0x9e3779b9u + (world_hash << 6u) +
                         (world_hash >> 2u));
  }
};

struct TrackedPrefabInstance {
  std::string package_key;
  std::vector<world::Entity> entities;
};

std::unordered_map<std::string, CachedPrefabPackage> g_cached_prefab_packages;
std::unordered_map<PrefabRootKey, TrackedPrefabInstance, PrefabRootKeyHash>
    g_instances_by_root;
assets::AssetRegistry* g_default_prefab_assets = nullptr;
std::mutex g_prefab_state_mutex;

std::string packageCacheKey(assets::AssetRegistry* assets,
                            const std::filesystem::path& manifest_path) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(manifest_path, ec);
  if (ec) {
    absolute = manifest_path;
  }
  return std::to_string(reinterpret_cast<std::uintptr_t>(assets)) + "|" +
         absolute.lexically_normal().string();
}

struct PackageAcquireResult {
  bool success = true;
  assets::AssetRegistry* assets = nullptr;
  std::string cache_key;
  std::optional<assets::AssetPackageHandle> handle;
};

PackageAcquireResult acquirePrefabPackage(assets::AssetRegistry* assets,
                                          const std::filesystem::path& prefab_path) {
  PackageAcquireResult result{};
  if (assets == nullptr) {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    assets = g_default_prefab_assets;
  }
  result.assets = assets;
  const std::filesystem::path manifest_path =
      assets::resolveAssetPackagePath(prefab_path.parent_path());
  std::error_code ec;
  if (!std::filesystem::exists(manifest_path, ec)) {
    return result;
  }
  if (ec) {
    spdlog::error("Failed to inspect asset package '{}': {}",
                  manifest_path.string(),
                  ec.message());
    result.success = false;
    return result;
  }
  if (assets == nullptr) {
    spdlog::error("Prefab '{}' has an asset package but no AssetRegistry was supplied",
                  prefab_path.string());
    result.success = false;
    return result;
  }

  result.cache_key = packageCacheKey(assets, manifest_path);
  {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    auto cached_it = g_cached_prefab_packages.find(result.cache_key);
    if (cached_it != g_cached_prefab_packages.end()) {
      cached_it->second.ref_count += 1u;
      result.handle = cached_it->second.handle;
      return result;
    }
  }

  std::string diagnostic;
  std::optional<assets::AssetPackageHandle> package =
      assets::importAssetPackage(*assets, manifest_path, &diagnostic);
  if (!package.has_value()) {
    spdlog::error("Failed to import prefab asset package '{}': {}",
                  manifest_path.string(),
                  diagnostic);
    result.success = false;
    return result;
  }

  CachedPrefabPackage cached{};
  cached.assets = assets;
  cached.handle = *package;
  cached.ref_count = 1u;
  {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    g_cached_prefab_packages[result.cache_key] = cached;
  }
  result.handle = std::move(package);
  return result;
}

void releasePrefabPackageByKey(const std::string& cache_key) {
  if (cache_key.empty()) {
    return;
  }
  std::optional<CachedPrefabPackage> released;
  {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    auto cached_it = g_cached_prefab_packages.find(cache_key);
    if (cached_it == g_cached_prefab_packages.end()) {
      return;
    }
    if (cached_it->second.ref_count > 0u) {
      cached_it->second.ref_count -= 1u;
    }
    if (cached_it->second.ref_count == 0u) {
      released = std::move(cached_it->second);
      g_cached_prefab_packages.erase(cached_it);
    }
  }
  if (released.has_value() && released->assets != nullptr) {
    assets::unloadAssetPackage(*released->assets, released->handle);
  }
}

std::string entityName(const world::World& world, world::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::TagComponent>(entity)) {
    return {};
  }
  return world.get<components::TagComponent>(entity).name;
}

void collectSubtree(const world::World& world,
                    const world::Scene& scene,
                    world::NodeId node_id,
                    std::vector<world::NodeId>& out_nodes) {
  if (!scene.isAlive(node_id)) {
    return;
  }
  const world::Node& node = scene.get(node_id);
  if (!node.entity.isValid() || !world.isAlive(node.entity)) {
    return;
  }
  out_nodes.push_back(node_id);
  for (const world::NodeId child : node.children) {
    collectSubtree(world, scene, child, out_nodes);
  }
}

PrefabDocument buildDocument(const world::World& world,
                             const world::Scene& scene,
                             world::Entity root,
                             const PrefabSaveOptions& options) {
  ensureBuiltinComponentSerializers();
  const ComponentSerializerRegistry& registry = componentSerializerRegistry();

  std::vector<world::NodeId> scene_nodes;
  const world::NodeId root_node = scene.findNode(root);
  if (options.include_children && scene.isAlive(root_node)) {
    collectSubtree(world, scene, root_node, scene_nodes);
  } else if (scene.isAlive(root_node)) {
    scene_nodes.push_back(root_node);
  }

  PrefabDocument document{};
  document.root = 0;

  std::unordered_map<world::NodeId, size_t> index_by_node;
  if (!scene_nodes.empty()) {
    document.nodes.reserve(scene_nodes.size());
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      index_by_node[scene_nodes[index]] = index;
    }
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      const world::Node& scene_node = scene.get(scene_nodes[index]);
      PrefabNode prefab_node{};
      prefab_node.id = static_cast<uint32_t>(index);
      prefab_node.name = entityName(world, scene_node.entity);
      if (scene.isAlive(scene_node.parent)) {
        const auto parent_it = index_by_node.find(scene_node.parent);
        if (parent_it != index_by_node.end()) {
          prefab_node.parent = parent_it->second;
        }
      }

      prefab_node.components = Json::object();
      for (const ComponentSerializer& serializer : registry.serializers()) {
        if (!serializer.has(world, scene_node.entity)) {
          continue;
        }
        prefab_node.components[serializer.type_name] =
            serializer.serialize(world, scene_node.entity);
      }
      document.nodes.push_back(std::move(prefab_node));
    }
    return document;
  }

  PrefabNode prefab_node{};
  prefab_node.id = 0u;
  prefab_node.name = entityName(world, root);
  prefab_node.components = Json::object();
  for (const ComponentSerializer& serializer : registry.serializers()) {
    if (!serializer.has(world, root)) {
      continue;
    }
    prefab_node.components[serializer.type_name] = serializer.serialize(world, root);
  }
  document.nodes.push_back(std::move(prefab_node));
  return document;
}

void destroyCreated(world::World& world,
                    world::Scene& scene,
                    const std::vector<world::Entity>& entities,
                    const std::vector<world::NodeId>& nodes) {
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (scene.isAlive(*it)) {
      scene.destroyNode(*it);
    }
  }
  for (const world::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
}

class PrefabInstantiationRollback {
 public:
  PrefabInstantiationRollback(world::World& world,
                              world::Scene& scene,
                              const std::vector<world::Entity>& entities,
                              const std::vector<world::NodeId>& nodes,
                              std::string package_key)
      : world_(world),
        scene_(scene),
        entities_(entities),
        nodes_(nodes),
        package_key_(std::move(package_key)) {}

  ~PrefabInstantiationRollback() {
    if (!active_) {
      return;
    }
    try {
      destroyCreated(world_, scene_, entities_, nodes_);
      releasePrefabPackageByKey(package_key_);
    } catch (...) {
      // Rollback is best effort during exception unwinding.
    }
  }

  void dismiss() { active_ = false; }

 private:
  world::World& world_;
  world::Scene& scene_;
  const std::vector<world::Entity>& entities_;
  const std::vector<world::NodeId>& nodes_;
  std::string package_key_;
  bool active_ = true;
};

bool deserializeComponents(world::World& world,
                           world::Entity entity,
                           const PrefabNode& node,
                           const std::filesystem::path& path) {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  std::unordered_set<std::string> consumed;
  consumed.reserve(node.components.size());

  for (const ComponentSerializer& serializer : registry.serializers()) {
    const auto component_it = node.components.find(serializer.type_name);
    if (component_it == node.components.end()) {
      continue;
    }
    try {
      if (!serializer.deserialize(world, entity, *component_it)) {
        spdlog::error("Prefab '{}' node '{}' has invalid '{}' component payload",
                      path.string(),
                      node.name,
                      serializer.type_name);
        return false;
      }
    } catch (const std::exception& e) {
      spdlog::error("Prefab '{}' node '{}' failed to add '{}' component: {}",
                    path.string(),
                    node.name,
                    serializer.type_name,
                    e.what());
      return false;
    }
    consumed.insert(serializer.type_name);
  }

  for (auto it = node.components.begin(); it != node.components.end(); ++it) {
    const std::string type_name = it.key();
    if (consumed.find(type_name) == consumed.end()) {
      spdlog::error("Prefab '{}' node '{}' has unknown component '{}'",
                   path.string(),
                   node.name,
                   type_name);
      return false;
    }
  }
  return true;
}

void applyRootTransform(world::World& world,
                        world::Entity root,
                        const components::TransformComponent& root_transform) {
  components::TransformComponent saved{};
  if (world.has<components::TransformComponent>(root)) {
    saved = world.get<components::TransformComponent>(root);
  }

  const components::TransformComponent final_transform =
      composeTransform(root_transform, saved);
  world.add(root, final_transform);
}

void ensureTransformsForHierarchy(world::World& world, world::Entity entity) {
  if (!world.has<components::TransformComponent>(entity)) {
    world.add(entity, components::TransformComponent{});
  }
}

bool validateVolumetricMaterials(const world::World& world,
                                 const std::vector<world::Entity>& entities,
                                 const assets::AssetRegistry* assets,
                                 const std::filesystem::path& path) {
  auto validate_key = [&](world::Entity entity,
                          std::string_view slot,
                          const std::string& key) {
    if (key.empty()) {
      return true;
    }
    if (assets == nullptr) {
      spdlog::error("Prefab '{}' entity {} VolumetricComponent {} material '{}' "
                    "requires an AssetRegistry",
                    path.string(),
                    entity.index,
                    slot,
                    key);
      return false;
    }
    if (!assets->resolveMaterial(key).has_value()) {
      spdlog::error("Prefab '{}' entity {} VolumetricComponent {} material '{}' "
                    "does not resolve",
                    path.string(),
                    entity.index,
                    slot,
                    key);
      return false;
    }
    return true;
  };

  for (world::Entity entity : entities) {
    if (!world.has<components::VolumetricComponent>(entity)) {
      continue;
    }
    const auto& volume = world.get<components::VolumetricComponent>(entity);
    if (!validate_key(entity, "interior", volume.interior_material_key) ||
        !validate_key(entity, "surface", volume.surface_material_key)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool savePrefab(const world::World& world,
                const world::Scene& scene,
                world::Entity root,
                const std::filesystem::path& input_path,
                const PrefabSaveOptions& options) {
  if (!world.isAlive(root)) {
    spdlog::error("Cannot save prefab: root entity is not alive");
    return false;
  }

  const PrefabDocument document = buildDocument(world, scene, root, options);
  if (document.nodes.empty()) {
    spdlog::error("Cannot save prefab: no serializable nodes found");
    return false;
  }

  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      spdlog::error("Failed to create prefab directory '{}': {}",
                    path.parent_path().string(),
                    ec.message());
      return false;
    }
  }

  std::ofstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open prefab '{}' for writing", path.string());
    return false;
  }
  stream << toJson(document).dump(2) << '\n';
  return static_cast<bool>(stream);
}

std::optional<PrefabInstance> instantiatePrefab(
    world::World& world,
    world::Scene& scene,
    const std::filesystem::path& input_path,
    const PrefabInstantiateDesc& desc) {
  ensureBuiltinComponentSerializers();
  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::optional<PrefabDocument> document = loadDocument(path);
  if (!document.has_value()) {
    return std::nullopt;
  }
  document = resolvePrefabVariables(*document, desc, path);
  if (!document.has_value()) {
    return std::nullopt;
  }
  if (!math::isFinite(desc.root_transform.localPosition()) ||
      !math::isFinite(desc.root_transform.localRotation()) ||
      !math::isFinite(desc.root_transform.localScale()) ||
      math::lengthSquared(desc.root_transform.localRotation()) <= 1.0e-12f) {
    spdlog::error("Prefab '{}' received an invalid root transform", path.string());
    return std::nullopt;
  }
  PackageAcquireResult package = acquirePrefabPackage(desc.assets, path);
  if (!package.success) {
    return std::nullopt;
  }
  assets::AssetRegistry* active_assets = package.assets;

  PrefabInstance instance{};
  std::vector<world::Entity> created_entities;
  std::vector<world::NodeId> created_nodes;
  PrefabInstantiationRollback rollback(
      world, scene, created_entities, created_nodes, package.cache_key);

  try {
    created_entities.reserve(document->nodes.size());
    created_nodes.reserve(document->nodes.size());
    instance.entities.reserve(document->nodes.size());
    instance.entities_by_id.reserve(document->nodes.size());
    instance.named_entities.reserve(document->nodes.size());

    for (size_t index = 0; index < document->nodes.size(); ++index) {
      world::Entity entity = world.createEntity();
      created_entities.push_back(entity);
      world::NodeId scene_node = scene.createNode(entity);
      created_nodes.push_back(scene_node);
      instance.entities.push_back(entity);
      instance.entities_by_id[document->nodes[index].id] = entity;
      if (!document->nodes[index].name.empty()) {
        instance.named_entities[document->nodes[index].name] = entity;
      }
    }

    for (size_t index = 0; index < document->nodes.size(); ++index) {
      if (!deserializeComponents(world, created_entities[index], document->nodes[index], path)) {
        return std::nullopt;
      }
      ensureTransformsForHierarchy(world, created_entities[index]);
    }

    if (!validateVolumetricMaterials(world, created_entities, active_assets, path)) {
      return std::nullopt;
    }

    for (size_t index = 0; index < document->nodes.size(); ++index) {
      const PrefabNode& node = document->nodes[index];
      const bool is_root = index == document->root;
      const std::string final_name =
          is_root && !desc.name_override.empty() ? desc.name_override : node.name;
      if (!final_name.empty()) {
        world.setName(created_entities[index], final_name);
        if (is_root && final_name != node.name) {
          instance.named_entities[final_name] = created_entities[index];
        }
      }
    }

    for (size_t index = 0; index < document->nodes.size(); ++index) {
      const auto parent = document->nodes[index].parent;
      if (!parent.has_value()) {
        continue;
      }
      if (!scene.reparent(created_nodes[index], created_nodes[*parent])) {
        spdlog::error("Prefab '{}' failed to apply node {} hierarchy",
                      path.string(),
                      index);
        return std::nullopt;
      }
    }

    instance.root = created_entities[document->root];
    instance.root_scene_node = created_nodes[document->root];
    instance.asset_registry = active_assets;
    instance.asset_package = package.handle;
    applyRootTransform(world, instance.root, desc.root_transform);
    world::updateWorldTransforms(world, scene);
    const PrefabRootKey root_key{
        .world = world.instanceId(),
        .entity = entityKey(instance.root),
    };
    TrackedPrefabInstance tracked_instance{
        .package_key = package.cache_key,
        .entities = instance.entities,
    };
    std::string stale_package_key;
    {
      std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
      const auto [tracked_it, inserted] =
          g_instances_by_root.try_emplace(root_key, std::move(tracked_instance));
      if (!inserted) {
        stale_package_key = tracked_it->second.package_key;
        tracked_it->second = std::move(tracked_instance);
      }
    }
    releasePrefabPackageByKey(stale_package_key);
  } catch (const std::exception& error) {
    spdlog::error("Prefab '{}' instantiation failed: {}", path.string(), error.what());
    return std::nullopt;
  } catch (...) {
    spdlog::error("Prefab '{}' instantiation failed with an unknown exception",
                  path.string());
    return std::nullopt;
  }
  rollback.dismiss();
  return std::optional<PrefabInstance>(std::move(instance));
}

bool destroyPrefab(world::World& world, world::Scene& scene, world::Entity root) {
  const PrefabRootKey root_key{
      .world = world.instanceId(),
      .entity = entityKey(root),
  };
  std::optional<TrackedPrefabInstance> tracked;
  {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    if (const auto tracked_it = g_instances_by_root.find(root_key);
        tracked_it != g_instances_by_root.end()) {
      tracked = std::move(tracked_it->second);
      g_instances_by_root.erase(tracked_it);
    }
  }
  if (tracked.has_value()) {
    for (auto it = tracked->entities.rbegin(); it != tracked->entities.rend(); ++it) {
      const world::NodeId node = scene.findNode(*it);
      if (scene.isAlive(node)) {
        scene.destroyNode(node);
      }
      if (world.isAlive(*it)) {
        world.destroyEntity(*it);
      }
    }
    releasePrefabPackageByKey(tracked->package_key);
    return true;
  }

  return false;
}

void clearPrefabAssetPackages() {
  std::vector<CachedPrefabPackage> released;
  {
    std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
    released.reserve(g_cached_prefab_packages.size());
    for (auto& [key, cached] : g_cached_prefab_packages) {
      (void)key;
      released.push_back(std::move(cached));
    }
    g_cached_prefab_packages.clear();
    g_instances_by_root.clear();
    g_default_prefab_assets = nullptr;
  }
  for (CachedPrefabPackage& cached : released) {
    if (cached.assets != nullptr) {
      assets::unloadAssetPackage(*cached.assets, cached.handle);
    }
  }
}

void bindPrefabAssetRegistry(assets::AssetRegistry* assets) {
  std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
  g_default_prefab_assets = assets;
}

}  // namespace karma::prefabs
