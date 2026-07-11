#include "karma/prefabs.h"

#include <algorithm>
#include <atomic>
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
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/foliage.h"
#include "karma/math.h"
#include "karma/world.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;
using PrefabDiagnostics = std::vector<std::string>;

bool prefabError(PrefabDiagnostics* diagnostics, std::string message) {
  spdlog::error("{}", message);
  if (diagnostics != nullptr) {
    diagnostics->push_back(std::move(message));
  }
  return false;
}

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
                        const std::filesystem::path& path,
                        PrefabDiagnostics* diagnostics = nullptr) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' is missing numeric '" +
                           std::string(key) + "' field");
  }
  uint64_t value = 0;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' has out-of-range '" +
                             std::string(key) + "' field");
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > static_cast<uint64_t>(UINT32_MAX)) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' has out-of-range '" +
                           std::string(key) + "' field");
  }
  out_value = static_cast<uint32_t>(value);
  return true;
}

bool readRequiredSize(const Json& object,
                      std::string_view key,
                      size_t& out_value,
                      const std::filesystem::path& path,
                      PrefabDiagnostics* diagnostics = nullptr) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' is missing numeric '" +
                           std::string(key) + "' field");
  }
  uint64_t value = 0;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' has negative '" +
                             std::string(key) + "' field");
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' has out-of-range '" +
                           std::string(key) + "' field");
  }
  out_value = static_cast<size_t>(value);
  return true;
}

bool readRequiredString(const Json& object,
                        std::string_view key,
                        std::string& out_value,
                        const std::filesystem::path& path,
                        PrefabDiagnostics* diagnostics = nullptr) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' is missing string '" +
                           std::string(key) + "' field");
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
    const std::filesystem::path& path,
    PrefabDiagnostics* diagnostics = nullptr) {
  PrefabVariableMap variables;
  if (!document.variables.is_object()) {
    prefabError(diagnostics,
                "Prefab '" + path.string() +
                    "' has non-object 'variables' field");
    return std::nullopt;
  }

  variables.reserve(document.variables.size());
  for (auto it = document.variables.begin(); it != document.variables.end(); ++it) {
    const std::string name = it.key();
    const Json& declaration = it.value();
    if (!declaration.is_object()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable '" + name +
                      "' must be an object");
      return std::nullopt;
    }

    const auto type_it = declaration.find("type");
    if (type_it == declaration.end() || !type_it->is_string()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable '" + name +
                      "' is missing string 'type'");
      return std::nullopt;
    }
    const std::string type_name = type_it->get<std::string>();
    const std::optional<PrefabVariableType> type = parseVariableType(type_name);
    if (!type.has_value()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable '" + name +
                      "' has unsupported type '" + type_name + "'");
      return std::nullopt;
    }

    const auto default_it = declaration.find("default");
    if (default_it == declaration.end()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable '" + name +
                      "' is missing 'default'");
      return std::nullopt;
    }
    Json normalized_default;
    if (!normalizeVariableValue(*type, *default_it, normalized_default)) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable '" + name +
                      "' default does not match type '" +
                      variableTypeName(*type) + "'");
      return std::nullopt;
    }

    variables.emplace(name, PrefabVariable{*type, std::move(normalized_default)});
  }

  for (const auto& [name, override_value] : desc.variables) {
    const auto variable_it = variables.find(name);
    if (variable_it == variables.end()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() +
                      "' received unknown variable override '" + name + "'");
      return std::nullopt;
    }
    Json normalized_override;
    if (!normalizeVariableValue(variable_it->second.type,
                                override_value,
                                normalized_override)) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' variable override '" + name +
                      "' does not match type '" +
                      variableTypeName(variable_it->second.type) + "'");
      return std::nullopt;
    }
    variable_it->second.value = std::move(normalized_override);
  }

  return variables;
}

bool resolveJsonMarkers(Json& value,
                        const PrefabVariableMap& variables,
                        const std::filesystem::path& path,
                        std::string_view context,
                        PrefabDiagnostics* diagnostics = nullptr) {
  if (value.is_array()) {
    for (Json& element : value) {
      if (!resolveJsonMarkers(element, variables, path, context, diagnostics)) {
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
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' " +
                             std::string(context) +
                             " has invalid variable marker");
    }
    if (has_var) {
      const Json& marker = value["$var"];
      if (!marker.is_string()) {
        return prefabError(diagnostics,
                           "Prefab '" + path.string() + "' " +
                               std::string(context) +
                               " has non-string $var marker");
      }
      const std::string name = marker.get<std::string>();
      const auto variable_it = variables.find(name);
      if (variable_it == variables.end()) {
        return prefabError(diagnostics,
                           "Prefab '" + path.string() + "' " +
                               std::string(context) +
                               " references unknown variable '" + name + "'");
      }
      value = variable_it->second.value;
      return true;
    }

    const Json& marker = value["$expr"];
    if (!marker.is_string()) {
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' " +
                             std::string(context) +
                             " has non-string $expr marker");
    }
    const std::string expression = marker.get<std::string>();
    ExpressionParser parser(expression, variables);
    double result = 0.0;
    std::string error;
    if (!parser.parse(result, error)) {
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' " +
                             std::string(context) + " has invalid expression '" +
                             expression + "': " + error);
    }
    value = result;
    return true;
  }

  for (auto it = value.begin(); it != value.end(); ++it) {
    if (!resolveJsonMarkers(it.value(), variables, path, context, diagnostics)) {
      return false;
    }
  }
  return true;
}

std::optional<PrefabDocument> resolvePrefabVariables(
    const PrefabDocument& document,
    const PrefabInstantiateDesc& desc,
    const std::filesystem::path& path,
    PrefabDiagnostics* diagnostics = nullptr) {
  std::optional<PrefabVariableMap> variables =
      buildResolvedVariables(document, desc, path, diagnostics);
  if (!variables.has_value()) {
    return std::nullopt;
  }

  PrefabDocument resolved = document;
  for (PrefabNode& node : resolved.nodes) {
    const std::string context =
        node.name.empty() ? "node '<unnamed>'" : "node '" + node.name + "'";
    if (!resolveJsonMarkers(node.components,
                            *variables,
                            path,
                            context,
                            diagnostics)) {
      return std::nullopt;
    }
  }
  return resolved;
}

bool validateParents(const PrefabDocument& document,
                     const std::filesystem::path& path,
                     PrefabDiagnostics* diagnostics = nullptr) {
  if (document.nodes.empty()) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() + "' contains no nodes");
  }
  if (document.root >= document.nodes.size()) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() +
                           "' root index is out of range");
  }
  if (document.nodes[document.root].parent.has_value()) {
    return prefabError(diagnostics,
                       "Prefab '" + path.string() +
                           "' root node must not have a parent");
  }

  std::vector<uint8_t> visit_state(document.nodes.size(), 0u);
  visit_state[document.root] = 2u;
  for (size_t index = 0; index < document.nodes.size(); ++index) {
    const std::optional<size_t> parent = document.nodes[index].parent;
    if (parent.has_value() && *parent >= document.nodes.size()) {
      return prefabError(diagnostics,
                         "Prefab '" + path.string() + "' node " +
                             std::to_string(index) +
                             " parent index is out of range");
    }

    std::vector<size_t> path_nodes;
    size_t cursor = index;
    while (cursor != document.root) {
      if (visit_state[cursor] == 2u) {
        break;
      }
      if (visit_state[cursor] == 1u) {
        return prefabError(diagnostics,
                           "Prefab '" + path.string() +
                               "' contains a parent cycle at node " +
                               std::to_string(index));
      }
      visit_state[cursor] = 1u;
      path_nodes.push_back(cursor);
      if (!document.nodes[cursor].parent.has_value()) {
        return prefabError(diagnostics,
                           "Prefab '" + path.string() + "' node " +
                               std::to_string(index) +
                               " is outside the declared root subtree");
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
                                            const std::filesystem::path& path,
                                            PrefabDiagnostics* diagnostics = nullptr) {
  if (!json.is_object()) {
    prefabError(diagnostics,
                "Prefab '" + path.string() +
                    "' root JSON value must be an object");
    return std::nullopt;
  }

  PrefabDocument document{};
  if (!readRequiredUint32(json,
                          "version",
                          document.version,
                          path,
                          diagnostics)) {
    return std::nullopt;
  }
  if (document.version != 2u) {
    prefabError(diagnostics,
                "Prefab '" + path.string() + "' has unsupported version " +
                    std::to_string(document.version));
    return std::nullopt;
  }
  if (!readRequiredSize(json, "root", document.root, path, diagnostics)) {
    return std::nullopt;
  }
  const auto variables_it = json.find("variables");
  if (variables_it != json.end()) {
    if (!variables_it->is_object()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() +
                      "' has non-object 'variables' field");
      return std::nullopt;
    }
    document.variables = *variables_it;
  }

  const auto nodes_it = json.find("nodes");
  if (nodes_it == json.end() || !nodes_it->is_array()) {
    prefabError(diagnostics,
                "Prefab '" + path.string() +
                    "' is missing array 'nodes' field");
    return std::nullopt;
  }

  std::unordered_set<uint32_t> ids;
  document.nodes.reserve(nodes_it->size());
  for (size_t index = 0; index < nodes_it->size(); ++index) {
    const Json& node_json = (*nodes_it)[index];
    if (!node_json.is_object()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' node " +
                      std::to_string(index) + " must be an object");
      return std::nullopt;
    }

    PrefabNode node{};
    if (!readRequiredUint32(node_json,
                            "id",
                            node.id,
                            path,
                            diagnostics) ||
        !readRequiredString(node_json,
                            "name",
                            node.name,
                            path,
                            diagnostics)) {
      return std::nullopt;
    }
    if (!ids.insert(node.id).second) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' contains duplicate node id " +
                      std::to_string(node.id));
      return std::nullopt;
    }

    const auto parent_it = node_json.find("parent");
    if (parent_it == node_json.end()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' node " +
                      std::to_string(index) + " is missing 'parent' field");
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
          prefabError(diagnostics,
                      "Prefab '" + path.string() + "' node " +
                          std::to_string(index) +
                          " has negative parent index");
          return std::nullopt;
        }
        parent = static_cast<uint64_t>(signed_parent);
      }
      if (parent > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        prefabError(diagnostics,
                    "Prefab '" + path.string() + "' node " +
                        std::to_string(index) +
                        " has out-of-range parent index");
        return std::nullopt;
      }
      node.parent = static_cast<size_t>(parent);
    } else {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' node " +
                      std::to_string(index) +
                      " parent must be null or numeric");
      return std::nullopt;
    }

    const auto components_it = node_json.find("components");
    if (components_it == node_json.end() || !components_it->is_object()) {
      prefabError(diagnostics,
                  "Prefab '" + path.string() + "' node " +
                      std::to_string(index) +
                      " is missing object 'components' field");
      return std::nullopt;
    }
    node.components = *components_it;
    document.nodes.push_back(std::move(node));
  }

  if (!validateParents(document, path, diagnostics)) {
    return std::nullopt;
  }
  return document;
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
  std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
  auto cached_it = g_cached_prefab_packages.find(result.cache_key);
  if (cached_it != g_cached_prefab_packages.end()) {
    if (cached_it->second.ref_count == std::numeric_limits<uint32_t>::max()) {
      spdlog::error("Prefab asset package reference count overflow: {}",
                    manifest_path.string());
      result.success = false;
      return result;
    }
    cached_it->second.ref_count += 1u;
    result.handle = cached_it->second.handle;
    return result;
  }

  std::string diagnostic;
  std::optional<assets::AssetPackageHandle> package =
      assets->sharedPackageStore().acquirePackage(manifest_path, &diagnostic);
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
  g_cached_prefab_packages[result.cache_key] = cached;
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
    released->assets->sharedPackageStore().releasePackage(released->handle);
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
  std::unordered_map<uint64_t, uint32_t> node_id_by_entity;
  if (!scene_nodes.empty()) {
    document.nodes.reserve(scene_nodes.size());
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      index_by_node[scene_nodes[index]] = index;
      node_id_by_entity[entityKey(scene.get(scene_nodes[index]).entity)] =
          static_cast<uint32_t>(index);
    }
    const ComponentSerializationContext context{
        .serialize_entity_reference =
            [&](world::Entity entity) -> std::optional<Json> {
          const auto it = node_id_by_entity.find(entityKey(entity));
          if (it == node_id_by_entity.end()) {
            throw std::runtime_error(
                "component references an entity outside the saved prefab subtree");
          }
          return Json{{"scope", "prefab"}, {"node", it->second}};
        },
    };
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
            serializeComponentPayload(
                serializer, world, scene_node.entity, context);
      }
      document.nodes.push_back(std::move(prefab_node));
    }
    return document;
  }

  PrefabNode prefab_node{};
  prefab_node.id = 0u;
  prefab_node.name = entityName(world, root);
  prefab_node.components = Json::object();
  node_id_by_entity[entityKey(root)] = 0u;
  const ComponentSerializationContext context{
      .serialize_entity_reference =
          [&](world::Entity entity) -> std::optional<Json> {
        const auto it = node_id_by_entity.find(entityKey(entity));
        if (it == node_id_by_entity.end()) {
          throw std::runtime_error(
              "component references an entity outside the saved prefab");
        }
        return Json{{"scope", "prefab"}, {"node", it->second}};
      },
  };
  for (const ComponentSerializer& serializer : registry.serializers()) {
    if (!serializer.has(world, root)) {
      continue;
    }
    prefab_node.components[serializer.type_name] =
        serializeComponentPayload(serializer, world, root, context);
  }
  document.nodes.push_back(std::move(prefab_node));
  return document;
}

void makeAbsoluteJsonPathRelative(Json& object,
                                  std::string_view key,
                                  const std::filesystem::path& prefab_directory) {
  const auto it = object.find(std::string(key));
  if (it == object.end() || !it->is_string()) {
    return;
  }

  const std::filesystem::path serialized_path = it->get<std::string>();
  if (serialized_path.empty() || serialized_path.is_relative()) {
    return;
  }

  const std::filesystem::path relative_path =
      serialized_path.lexically_normal().lexically_relative(prefab_directory);
  if (!relative_path.empty() && relative_path.is_relative()) {
    *it = relative_path.generic_string();
  }
}

void makeFileBackedComponentPathsRelative(
    PrefabDocument& document,
    const std::filesystem::path& prefab_path) {
  std::error_code ec;
  std::filesystem::path prefab_directory =
      std::filesystem::absolute(prefab_path.parent_path(), ec);
  if (ec) {
    prefab_directory = prefab_path.parent_path();
  }
  prefab_directory = prefab_directory.lexically_normal();

  for (PrefabNode& node : document.nodes) {
    auto terrain_it = node.components.find("TerrainComponent");
    if (terrain_it != node.components.end() && terrain_it->is_object()) {
      Json& terrain = *terrain_it;
      makeAbsoluteJsonPathRelative(terrain, "tile_directory", prefab_directory);
      makeAbsoluteJsonPathRelative(terrain, "height_image", prefab_directory);
      makeAbsoluteJsonPathRelative(terrain, "heatmap_image", prefab_directory);
      makeAbsoluteJsonPathRelative(terrain, "color_image", prefab_directory);
      makeAbsoluteJsonPathRelative(terrain, "control_image", prefab_directory);

      const auto layers_it = terrain.find("material_layers");
      if (layers_it != terrain.end() && layers_it->is_array()) {
        for (Json& layer : *layers_it) {
          if (!layer.is_object()) {
            continue;
          }
          makeAbsoluteJsonPathRelative(layer, "albedo_image", prefab_directory);
          makeAbsoluteJsonPathRelative(layer, "normal_image", prefab_directory);
          makeAbsoluteJsonPathRelative(layer, "roughness_image", prefab_directory);
        }
      }

      const auto maps_it = terrain.find("data_maps");
      if (maps_it != terrain.end() && maps_it->is_array()) {
        for (Json& map : *maps_it) {
          if (map.is_object()) {
            makeAbsoluteJsonPathRelative(map, "image", prefab_directory);
          }
        }
      }
    }

    auto foliage_it = node.components.find("FoliageComponent");
    if (foliage_it != node.components.end() && foliage_it->is_object()) {
      makeAbsoluteJsonPathRelative(
          *foliage_it, "sidecar_path", prefab_directory);
      makeAbsoluteJsonPathRelative(
          *foliage_it, "prefab_path", prefab_directory);
    }

    auto camera_it = node.components.find("CameraComponent");
    if (camera_it != node.components.end() && camera_it->is_object()) {
      makeAbsoluteJsonPathRelative(
          *camera_it, "shader_override_vertex_path", prefab_directory);
      makeAbsoluteJsonPathRelative(
          *camera_it, "shader_override_fragment_path", prefab_directory);
    }
  }
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
                           const std::filesystem::path& path,
                           const ComponentSerializationContext& context = {}) {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  std::unordered_set<std::string> consumed;
  consumed.reserve(node.components.size());

  for (const ComponentSerializer& serializer : registry.serializers()) {
    const auto component_it = node.components.find(serializer.type_name);
    if (component_it == node.components.end()) {
      continue;
    }
    try {
      if (!deserializeComponentPayload(
              serializer, world, entity, *component_it, context)) {
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

std::optional<uint32_t> prefabNodeReference(const Json& reference) {
  const Json* node = nullptr;
  if (reference.is_number_integer() || reference.is_number_unsigned()) {
    node = &reference;
  } else if (reference.is_object()) {
    const auto scope_it = reference.find("scope");
    if (scope_it != reference.end() &&
        (!scope_it->is_string() || scope_it->get<std::string>() != "prefab")) {
      return std::nullopt;
    }
    auto node_it = reference.find("node");
    if (node_it == reference.end()) {
      node_it = reference.find("node_id");
    }
    if (node_it != reference.end()) node = &*node_it;
  }
  if (node == nullptr ||
      (!node->is_number_integer() && !node->is_number_unsigned())) {
    return std::nullopt;
  }
  uint64_t value = 0u;
  if (node->is_number_unsigned()) {
    value = node->get<uint64_t>();
  } else {
    const int64_t signed_value = node->get<int64_t>();
    if (signed_value < 0) return std::nullopt;
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > UINT32_MAX) return std::nullopt;
  return static_cast<uint32_t>(value);
}

bool validateRenderComponentDependencies(
    const world::World& world,
    const std::vector<world::Entity>& entities,
    const std::filesystem::path& path) {
  for (const world::Entity entity : entities) {
    if (world.has<components::InstancedMeshComponent>(entity)) {
      const auto& batch =
          world.get<components::InstancedMeshComponent>(entity);
      const world::Entity source =
          batch.instance_source.isValid() ? batch.instance_source : entity;
      if (!world.isAlive(source) ||
          !world.has<components::InstanceSetComponent>(source)) {
        spdlog::error(
            "Prefab '{}' InstancedMeshComponent has no resolvable "
            "InstanceSetComponent source",
            path.string());
        return false;
      }
    }
    if (world.has<components::LodComponent>(entity)) {
      const bool has_mesh = world.has<components::MeshComponent>(entity);
      const bool has_instanced_mesh =
          world.has<components::InstancedMeshComponent>(entity);
      bool has_direct_foliage = false;
      if (world.has<components::FoliageComponent>(entity)) {
        const auto& foliage =
            world.get<components::FoliageComponent>(entity);
        has_direct_foliage = foliage.prefab_path.empty() &&
                             !foliage.mesh_asset_key.empty();
      }
      if (!has_mesh && !has_instanced_mesh && !has_direct_foliage) {
        spdlog::error(
            "Prefab '{}' LODComponent has no sibling mesh, instanced mesh, "
            "or direct-mesh foliage render source",
            path.string());
        return false;
      }
    }
  }
  return true;
}

bool validateDocumentComponents(const PrefabDocument& document,
                                const std::filesystem::path& path) {
  world::World validation_world;
  std::vector<world::Entity> entities;
  entities.reserve(document.nodes.size());
  std::unordered_map<uint32_t, world::Entity> entities_by_id;
  entities_by_id.reserve(document.nodes.size());
  for (const PrefabNode& node : document.nodes) {
    const world::Entity entity = validation_world.createEntity();
    entities.push_back(entity);
    entities_by_id.emplace(node.id, entity);
  }
  const ComponentSerializationContext context{
      .resolve_entity_reference =
          [&](const Json& reference) -> std::optional<world::Entity> {
        const std::optional<uint32_t> node_id = prefabNodeReference(reference);
        if (!node_id.has_value()) return std::nullopt;
        const auto it = entities_by_id.find(*node_id);
        return it == entities_by_id.end()
                   ? std::nullopt
                   : std::optional<world::Entity>(it->second);
      },
  };
  for (size_t index = 0; index < document.nodes.size(); ++index) {
    if (!deserializeComponents(validation_world,
                               entities[index],
                               document.nodes[index],
                               path,
                               context)) {
      return false;
    }
  }
  return validateRenderComponentDependencies(validation_world, entities, path);
}

void resolveFileBackedComponentPaths(world::World& world,
                                     world::Entity entity,
                                     const std::filesystem::path& prefab_path) {
  const std::filesystem::path base = prefab_path.parent_path();
  auto resolve = [&](std::filesystem::path& path) {
    if (!path.empty() && path.is_relative()) {
      path = (base / path).lexically_normal();
    }
  };
  if (world.has<components::TerrainComponent>(entity)) {
    auto& terrain = world.get<components::TerrainComponent>(entity);
    resolve(terrain.tile_directory);
    resolve(terrain.height_image);
    resolve(terrain.heatmap_image);
    resolve(terrain.color_image);
    resolve(terrain.control_image);
    for (auto& layer : terrain.material_layers) {
      resolve(layer.albedo_image);
      resolve(layer.normal_image);
      resolve(layer.roughness_image);
    }
    for (auto& map : terrain.data_maps) resolve(map.image);
  }
  if (world.has<components::FoliageComponent>(entity)) {
    auto& foliage = world.get<components::FoliageComponent>(entity);
    resolve(foliage.sidecar_path);
    resolve(foliage.prefab_path);
  }
  if (world.has<components::CameraComponent>(entity)) {
    auto& camera = world.get<components::CameraComponent>(entity);
    resolve(camera.shader_override_vertex_path);
    resolve(camera.shader_override_fragment_path);
  }
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

PrefabDocumentSaveResult savePrefabDocument(
    const PrefabDocument& input_document,
    const std::filesystem::path& input_path) {
  PrefabDocumentSaveResult result{};
  result.path = resolvePrefabPath(input_path);

  PrefabDocument document = input_document;
  makeFileBackedComponentPathsRelative(document, result.path);
  const Json document_json = toJson(document);
  std::optional<PrefabDocument> parsed =
      parseDocument(document_json, result.path, &result.diagnostics);
  if (!parsed.has_value()) {
    return result;
  }
  const PrefabInstantiateDesc defaults{};
  std::optional<PrefabDocument> resolved = resolvePrefabVariables(
      *parsed, defaults, result.path, &result.diagnostics);
  if (!resolved.has_value()) {
    return result;
  }
  try {
    ensureBuiltinComponentSerializers();
    if (!validateDocumentComponents(*resolved, result.path)) {
      prefabError(&result.diagnostics,
                  "Cannot save prefab '" + result.path.string() +
                      "': it has non-portable, invalid, or unresolved "
                      "components");
      return result;
    }
  } catch (const std::exception& error) {
    prefabError(&result.diagnostics,
                "Cannot validate prefab '" + result.path.string() +
                    "': " + error.what());
    return result;
  }

  std::error_code ec;
  if (!result.path.parent_path().empty()) {
    std::filesystem::create_directories(result.path.parent_path(), ec);
    if (ec) {
      prefabError(&result.diagnostics,
                  "Failed to create prefab directory '" +
                      result.path.parent_path().string() + "': " +
                      ec.message());
      return result;
    }
  }

  static std::atomic<uint64_t> next_temporary_id{0u};
  std::filesystem::path temporary_path;
  do {
    temporary_path = result.path;
    temporary_path += ".tmp-" +
                      std::to_string(next_temporary_id.fetch_add(
                          1u, std::memory_order_relaxed));
  } while (std::filesystem::exists(temporary_path, ec) && !ec);
  ec.clear();

  {
    std::ofstream stream(
        temporary_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
      prefabError(&result.diagnostics,
                  "Failed to open prefab temporary file '" +
                      temporary_path.string() + "' for writing");
      return result;
    }
    stream << document_json.dump(2) << '\n';
    stream.flush();
    if (!stream) {
      stream.close();
      std::filesystem::remove(temporary_path, ec);
      prefabError(&result.diagnostics,
                  "Failed to write prefab temporary file '" +
                      temporary_path.string() + "'");
      return result;
    }
    stream.close();
    if (!stream) {
      std::filesystem::remove(temporary_path, ec);
      prefabError(&result.diagnostics,
                  "Failed to close prefab temporary file '" +
                      temporary_path.string() + "'");
      return result;
    }
  }

#if defined(_WIN32)
  if (!MoveFileExW(temporary_path.c_str(),
                   result.path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()),
                         std::system_category());
  }
#else
  std::filesystem::rename(temporary_path, result.path, ec);
#endif
  if (ec) {
    std::error_code cleanup_ec;
    std::filesystem::remove(temporary_path, cleanup_ec);
    prefabError(&result.diagnostics,
                "Failed to atomically replace prefab '" +
                    result.path.string() + "': " + ec.message());
  }
  return result;
}

PrefabLoadResult loadPrefabDocument(const std::filesystem::path& input_path) {
  PrefabLoadResult result{};
  result.source_path = resolvePrefabPath(input_path);

  std::ifstream stream(result.source_path);
  if (!stream) {
    prefabError(&result.diagnostics,
                "Failed to open prefab '" + result.source_path.string() + "'");
    return result;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception& error) {
    prefabError(&result.diagnostics,
                "Failed to parse prefab '" + result.source_path.string() +
                    "': " + error.what());
    return result;
  }

  std::optional<PrefabDocument> document =
      parseDocument(json, result.source_path, &result.diagnostics);
  if (!document.has_value()) {
    return result;
  }

  const PrefabInstantiateDesc default_variables{};
  std::optional<PrefabDocument> resolved = resolvePrefabVariables(
      *document,
      default_variables,
      result.source_path,
      &result.diagnostics);
  if (!resolved.has_value()) {
    return result;
  }

  try {
    ensureBuiltinComponentSerializers();
    if (!validateDocumentComponents(*resolved, result.source_path)) {
      prefabError(&result.diagnostics,
                  "Prefab '" + result.source_path.string() +
                      "' has unknown, invalid, or unresolved components");
      return result;
    }
  } catch (const std::exception& error) {
    prefabError(&result.diagnostics,
                "Prefab '" + result.source_path.string() +
                    "' component validation failed: " + error.what());
    return result;
  }

  result.document = std::move(document);
  return result;
}

bool savePrefab(const world::World& world,
                const world::Scene& scene,
                world::Entity root,
                const std::filesystem::path& input_path,
                const PrefabSaveOptions& options) {
  if (!world.isAlive(root)) {
    spdlog::error("Cannot save prefab: root entity is not alive");
    return false;
  }

  PrefabDocument document{};
  try {
    document = buildDocument(world, scene, root, options);
  } catch (const std::exception& error) {
    spdlog::error("Cannot serialize prefab components: {}", error.what());
    return false;
  }
  if (document.nodes.empty()) {
    spdlog::error("Cannot save prefab: no serializable nodes found");
    return false;
  }

  return savePrefabDocument(document, input_path).success();
}

std::optional<PrefabInstance> instantiatePrefab(
    world::World& world,
    world::Scene& scene,
    const std::filesystem::path& input_path,
    const PrefabInstantiateDesc& desc) {
  ensureBuiltinComponentSerializers();
  const std::filesystem::path path = resolvePrefabPath(input_path);
  PrefabLoadResult load = loadPrefabDocument(path);
  if (!load.success()) {
    return std::nullopt;
  }
  std::optional<PrefabDocument> document = std::move(load.document);
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
  PackageAcquireResult package{};
  package.assets = desc.assets;
  if (desc.auto_load_package) {
    package = acquirePrefabPackage(desc.assets, path);
    if (!package.success) {
      return std::nullopt;
    }
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

    const ComponentSerializationContext component_context{
        .resolve_entity_reference =
            [&](const Json& reference) -> std::optional<world::Entity> {
          const std::optional<uint32_t> node_id =
              prefabNodeReference(reference);
          if (!node_id.has_value()) return std::nullopt;
          const auto it = instance.entities_by_id.find(*node_id);
          return it == instance.entities_by_id.end()
                     ? std::nullopt
                     : std::optional<world::Entity>(it->second);
        },
    };

    for (size_t index = 0; index < document->nodes.size(); ++index) {
      if (!deserializeComponents(world,
                                 created_entities[index],
                                 document->nodes[index],
                                 path,
                                 component_context)) {
        return std::nullopt;
      }
      resolveFileBackedComponentPaths(world, created_entities[index], path);
      ensureTransformsForHierarchy(world, created_entities[index]);
    }

    if (!validateRenderComponentDependencies(
            world, created_entities, path)) {
      return std::nullopt;
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
      cached.assets->sharedPackageStore().releasePackage(cached.handle);
    }
  }
}

void bindPrefabAssetRegistry(assets::AssetRegistry* assets) {
  std::lock_guard<std::mutex> lock(g_prefab_state_mutex);
  g_default_prefab_assets = assets;
}

}  // namespace karma::prefabs
