#include "features/ui/native/binding_engine.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace karma::ui::native {
namespace {

constexpr std::size_t kMaximumCachedExpressions = 4096u;
constexpr std::size_t kMaximumCachedPaths = 8192u;

std::string_view trimView(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1u);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1u);
  }
  return value;
}

struct PathSegment {
  std::string name;
  std::optional<std::size_t> index;
};

std::vector<PathSegment> parsePropertyPath(std::string_view path) {
  std::vector<PathSegment> output;
  std::size_t cursor = 0u;
  while (cursor < path.size()) {
    if (std::isalpha(static_cast<unsigned char>(path[cursor])) == 0 &&
        path[cursor] != '_') {
      return {};
    }
    const std::size_t begin = cursor++;
    while (cursor < path.size() &&
           (std::isalnum(static_cast<unsigned char>(path[cursor])) != 0 ||
            path[cursor] == '_' || path[cursor] == '-')) {
      ++cursor;
    }
    PathSegment segment{.name = std::string(path.substr(begin, cursor - begin))};
    if (cursor < path.size() && path[cursor] == '[') {
      ++cursor;
      const std::size_t number_begin = cursor;
      while (cursor < path.size() &&
             std::isdigit(static_cast<unsigned char>(path[cursor])) != 0) {
        ++cursor;
      }
      if (number_begin == cursor || cursor >= path.size() || path[cursor] != ']') {
        return {};
      }
      std::size_t index = 0u;
      const auto parsed = std::from_chars(path.data() + number_begin,
                                          path.data() + cursor, index);
      if (parsed.ec != std::errc{}) return {};
      segment.index = index;
      ++cursor;
    }
    output.push_back(std::move(segment));
    if (cursor == path.size()) break;
    if (path[cursor] != '.') return {};
    ++cursor;
    if (cursor == path.size()) return {};
  }
  return output;
}

enum class TokenKind : std::uint8_t {
  End,
  Invalid,
  Identifier,
  String,
  Number,
  True,
  False,
  Null,
  Not,
  And,
  Or,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Question,
  Colon,
  LeftParen,
  RightParen,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  double number = 0.0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view source) : source_(source) {}

  Token next() {
    while (cursor_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[cursor_])) != 0) {
      ++cursor_;
    }
    if (cursor_ >= source_.size()) return {};
    const char character = source_[cursor_];
    if (std::isalpha(static_cast<unsigned char>(character)) != 0 ||
        character == '_') {
      const std::size_t begin = cursor_++;
      while (cursor_ < source_.size()) {
        const char item = source_[cursor_];
        if (std::isalnum(static_cast<unsigned char>(item)) != 0 || item == '_' ||
            item == '-' || item == '.' || item == '[' || item == ']') {
          ++cursor_;
        } else {
          break;
        }
      }
      std::string text(source_.substr(begin, cursor_ - begin));
      if (text == "true") return {.kind = TokenKind::True};
      if (text == "false") return {.kind = TokenKind::False};
      if (text == "null") return {.kind = TokenKind::Null};
      return {.kind = TokenKind::Identifier, .text = std::move(text)};
    }
    if (std::isdigit(static_cast<unsigned char>(character)) != 0 ||
        character == '-' || character == '+') {
      const std::size_t begin = cursor_++;
      while (cursor_ < source_.size()) {
        const char item = source_[cursor_];
        if (std::isdigit(static_cast<unsigned char>(item)) != 0 || item == '.' ||
            item == 'e' || item == 'E' || item == '+' || item == '-') {
          ++cursor_;
        } else {
          break;
        }
      }
      double number = 0.0;
      const std::string_view token = source_.substr(begin, cursor_ - begin);
      const auto parsed = std::from_chars(token.data(), token.data() + token.size(),
                                          number, std::chars_format::general);
      if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
          !std::isfinite(number)) {
        return {.kind = TokenKind::Invalid};
      }
      return {.kind = TokenKind::Number, .number = number};
    }
    if (character == '\'' || character == '"') {
      const char quote = character;
      ++cursor_;
      std::string text;
      while (cursor_ < source_.size() && source_[cursor_] != quote) {
        char item = source_[cursor_++];
        if (item == '\\' && cursor_ < source_.size()) {
          const char escaped = source_[cursor_++];
          if (escaped == 'n') item = '\n';
          else if (escaped == 't') item = '\t';
          else item = escaped;
        }
        text.push_back(item);
      }
      if (cursor_ >= source_.size()) return {.kind = TokenKind::Invalid};
      ++cursor_;
      return {.kind = TokenKind::String, .text = std::move(text)};
    }
    ++cursor_;
    switch (character) {
      case '!':
        if (consume('=')) return {.kind = TokenKind::NotEqual};
        return {.kind = TokenKind::Not};
      case '&':
        if (consume('&')) return {.kind = TokenKind::And};
        break;
      case '|':
        if (consume('|')) return {.kind = TokenKind::Or};
        break;
      case '=':
        if (consume('=')) return {.kind = TokenKind::Equal};
        break;
      case '<':
        if (consume('=')) return {.kind = TokenKind::LessEqual};
        return {.kind = TokenKind::Less};
      case '>':
        if (consume('=')) return {.kind = TokenKind::GreaterEqual};
        return {.kind = TokenKind::Greater};
      case '?': return {.kind = TokenKind::Question};
      case ':': return {.kind = TokenKind::Colon};
      case '(': return {.kind = TokenKind::LeftParen};
      case ')': return {.kind = TokenKind::RightParen};
      default: break;
    }
    return {.kind = TokenKind::Invalid};
  }

 private:
  bool consume(char expected) {
    if (cursor_ < source_.size() && source_[cursor_] == expected) {
      ++cursor_;
      return true;
    }
    return false;
  }

  std::string_view source_;
  std::size_t cursor_ = 0u;
};

enum class Operation : std::uint8_t {
  Literal,
  Lookup,
  Not,
  And,
  Or,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Ternary,
};

struct ExpressionNode {
  Operation operation = Operation::Literal;
  Value literal;
  std::string path;
  std::unique_ptr<ExpressionNode> first;
  std::unique_ptr<ExpressionNode> second;
  std::unique_ptr<ExpressionNode> third;
};

std::unique_ptr<ExpressionNode> literal(Value value) {
  auto node = std::make_unique<ExpressionNode>();
  node->literal = std::move(value);
  return node;
}

std::unique_ptr<ExpressionNode> operation(
    Operation kind,
    std::unique_ptr<ExpressionNode> first,
    std::unique_ptr<ExpressionNode> second = {},
    std::unique_ptr<ExpressionNode> third = {}) {
  auto node = std::make_unique<ExpressionNode>();
  node->operation = kind;
  node->first = std::move(first);
  node->second = std::move(second);
  node->third = std::move(third);
  return node;
}

class Parser {
 public:
  explicit Parser(std::string_view source) : lexer_(source), current_(lexer_.next()) {}

  std::unique_ptr<ExpressionNode> parse() {
    auto result = parseTernary();
    return result && current_.kind == TokenKind::End ? std::move(result) : nullptr;
  }

 private:
  void advance() { current_ = lexer_.next(); }

  std::unique_ptr<ExpressionNode> parseTernary() {
    auto condition = parseOr();
    if (!condition || current_.kind != TokenKind::Question) return condition;
    advance();
    auto when_true = parseTernary();
    if (!when_true || current_.kind != TokenKind::Colon) return nullptr;
    advance();
    auto when_false = parseTernary();
    if (!when_false) return nullptr;
    return operation(Operation::Ternary, std::move(condition),
                     std::move(when_true), std::move(when_false));
  }

  std::unique_ptr<ExpressionNode> parseOr() {
    auto left = parseAnd();
    while (left && current_.kind == TokenKind::Or) {
      advance();
      auto right = parseAnd();
      if (!right) return nullptr;
      left = operation(Operation::Or, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExpressionNode> parseAnd() {
    auto left = parseEquality();
    while (left && current_.kind == TokenKind::And) {
      advance();
      auto right = parseEquality();
      if (!right) return nullptr;
      left = operation(Operation::And, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExpressionNode> parseEquality() {
    auto left = parseComparison();
    while (left && (current_.kind == TokenKind::Equal ||
                    current_.kind == TokenKind::NotEqual)) {
      const Operation kind = current_.kind == TokenKind::Equal
                                 ? Operation::Equal
                                 : Operation::NotEqual;
      advance();
      auto right = parseComparison();
      if (!right) return nullptr;
      left = operation(kind, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExpressionNode> parseComparison() {
    auto left = parseUnary();
    while (left && current_.kind >= TokenKind::Less &&
           current_.kind <= TokenKind::GreaterEqual) {
      Operation kind = Operation::Less;
      if (current_.kind == TokenKind::LessEqual) kind = Operation::LessEqual;
      if (current_.kind == TokenKind::Greater) kind = Operation::Greater;
      if (current_.kind == TokenKind::GreaterEqual) kind = Operation::GreaterEqual;
      advance();
      auto right = parseUnary();
      if (!right) return nullptr;
      left = operation(kind, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExpressionNode> parseUnary() {
    if (current_.kind == TokenKind::Not) {
      advance();
      auto value = parseUnary();
      return value ? operation(Operation::Not, std::move(value)) : nullptr;
    }
    return parsePrimary();
  }

  std::unique_ptr<ExpressionNode> parsePrimary() {
    const Token token = current_;
    switch (token.kind) {
      case TokenKind::Identifier: {
        advance();
        auto node = std::make_unique<ExpressionNode>();
        node->operation = Operation::Lookup;
        node->path = token.text;
        return node;
      }
      case TokenKind::String:
        advance();
        return literal(token.text);
      case TokenKind::Number:
        advance();
        return literal(token.number);
      case TokenKind::True:
        advance();
        return literal(true);
      case TokenKind::False:
        advance();
        return literal(false);
      case TokenKind::Null:
        advance();
        return literal({});
      case TokenKind::LeftParen: {
        advance();
        auto value = parseTernary();
        if (!value || current_.kind != TokenKind::RightParen) return nullptr;
        advance();
        return value;
      }
      default: return nullptr;
    }
  }

  Lexer lexer_;
  Token current_;
};

struct CachedExpression {
  bool valid = false;
  std::unique_ptr<ExpressionNode> root;
};

}  // namespace

struct BindingEngine::Impl {
  std::unordered_map<std::string, CachedExpression> expressions;
  std::unordered_map<std::string, std::vector<PathSegment>> paths;
  std::size_t expression_compile_count = 0u;
  std::size_t path_parse_count = 0u;

  const CachedExpression* compiled(std::string_view source) {
    const std::string key(trimView(source));
    if (key.empty()) return nullptr;
    auto found = expressions.find(key);
    if (found == expressions.end()) {
      if (expressions.size() >= kMaximumCachedExpressions) {
        expressions.clear();
      }
      ++expression_compile_count;
      CachedExpression expression;
      expression.root = Parser(key).parse();
      expression.valid = expression.root != nullptr;
      found = expressions.emplace(key, std::move(expression)).first;
    }
    return &found->second;
  }

  const std::vector<PathSegment>* path(std::string_view source) {
    const std::string key(source);
    if (const auto found = paths.find(key); found != paths.end()) {
      return found->second.empty() ? nullptr : &found->second;
    }
    if (paths.size() >= kMaximumCachedPaths) paths.clear();
    ++path_parse_count;
    auto [inserted, unused] = paths.emplace(key, parsePropertyPath(source));
    (void)unused;
    return inserted->second.empty() ? nullptr : &inserted->second;
  }

  std::optional<Value> descend(const Value& root,
                               const std::vector<PathSegment>& segments,
                               std::size_t begin = 0u) {
    Value current = root;
    for (std::size_t index = begin; index < segments.size(); ++index) {
      const Value::Object* object = current.asObject();
      if (object == nullptr) return std::nullopt;
      const auto found = object->find(segments[index].name);
      if (found == object->end()) return std::nullopt;
      current = found->second;
      if (segments[index].index.has_value()) {
        const Value::Array* array = current.asArray();
        if (array == nullptr || *segments[index].index >= array->size()) {
          return std::nullopt;
        }
        current = (*array)[*segments[index].index];
      }
    }
    return current;
  }

  std::optional<Value> lookup(std::string_view source,
                              const BindingEvaluationContext& context) {
    const std::vector<PathSegment>* segments = path(source);
    if (segments == nullptr) return std::nullopt;
    if (context.locals != nullptr) {
      const auto found = context.locals->find(segments->front().name);
      if (found != context.locals->end()) {
        Value local = found->second;
        if (segments->front().index.has_value()) {
          const Value::Array* array = local.asArray();
          if (array == nullptr ||
              *segments->front().index >= array->size()) {
            return std::nullopt;
          }
          local = (*array)[*segments->front().index];
        }
        return segments->size() == 1u
                   ? std::optional<Value>{std::move(local)}
                   : descend(local, *segments, 1u);
      }
    }
    return context.model == nullptr ? std::nullopt
                                    : descend(*context.model, *segments);
  }

  std::optional<Value> evaluateNode(
      const ExpressionNode& node,
      const BindingEvaluationContext& context) {
    if (node.operation == Operation::Literal) return node.literal;
    if (node.operation == Operation::Lookup) {
      return lookup(node.path, context).value_or(Value{});
    }
    const auto first = node.first ? evaluateNode(*node.first, context) : std::nullopt;
    if (!first) return std::nullopt;
    if (node.operation == Operation::Not) return Value(!first->truthy());
    if (node.operation == Operation::Ternary) {
      const ExpressionNode* selected = first->truthy() ? node.second.get()
                                                       : node.third.get();
      return selected ? evaluateNode(*selected, context) : std::nullopt;
    }
    if (node.operation == Operation::And && !first->truthy()) return Value(false);
    if (node.operation == Operation::Or && first->truthy()) return Value(true);
    const auto second = node.second ? evaluateNode(*node.second, context) : std::nullopt;
    if (!second) return std::nullopt;
    if (node.operation == Operation::And) {
      return Value(first->truthy() && second->truthy());
    }
    if (node.operation == Operation::Or) {
      return Value(first->truthy() || second->truthy());
    }
    if (node.operation == Operation::Equal ||
        node.operation == Operation::NotEqual) {
      const bool equal = *first == *second;
      return Value(node.operation == Operation::Equal ? equal : !equal);
    }
    bool result = false;
    const auto first_number = first->asNumber();
    const auto second_number = second->asNumber();
    if (first_number && second_number) {
      if (node.operation == Operation::Less) result = *first_number < *second_number;
      if (node.operation == Operation::LessEqual) result = *first_number <= *second_number;
      if (node.operation == Operation::Greater) result = *first_number > *second_number;
      if (node.operation == Operation::GreaterEqual) result = *first_number >= *second_number;
    } else if (first->asString() != nullptr && second->asString() != nullptr) {
      if (node.operation == Operation::Less) result = *first->asString() < *second->asString();
      if (node.operation == Operation::LessEqual) result = *first->asString() <= *second->asString();
      if (node.operation == Operation::Greater) result = *first->asString() > *second->asString();
      if (node.operation == Operation::GreaterEqual) result = *first->asString() >= *second->asString();
    }
    return Value(result);
  }
};

BindingEngine::BindingEngine() : impl_(std::make_unique<Impl>()) {}
BindingEngine::~BindingEngine() = default;
BindingEngine::BindingEngine(BindingEngine&&) noexcept = default;
BindingEngine& BindingEngine::operator=(BindingEngine&&) noexcept = default;

std::optional<Value> BindingEngine::evaluate(
    std::string_view expression,
    const BindingEvaluationContext& context) {
  const CachedExpression* compiled = impl_->compiled(expression);
  return compiled != nullptr && compiled->valid
             ? impl_->evaluateNode(*compiled->root, context)
             : std::nullopt;
}

std::vector<std::string> BindingEngine::dependencies(
    std::string_view expression) {
  const CachedExpression* compiled = impl_->compiled(expression);
  if (compiled == nullptr || !compiled->valid) return {};
  std::vector<std::string> output;
  auto collect = [&](auto&& self, const ExpressionNode* node) -> void {
    if (node == nullptr) return;
    if (node->operation == Operation::Lookup &&
        std::find(output.begin(), output.end(), node->path) == output.end()) {
      output.push_back(node->path);
    }
    self(self, node->first.get());
    self(self, node->second.get());
    self(self, node->third.get());
  };
  collect(collect, compiled->root.get());
  return output;
}

std::string BindingEngine::interpolate(
    std::string_view text,
    const BindingEvaluationContext& context) {
  std::string output;
  std::size_t cursor = 0u;
  while (cursor < text.size()) {
    const std::size_t open = text.find("{{", cursor);
    if (open == text.npos) {
      output.append(text.substr(cursor));
      break;
    }
    output.append(text.substr(cursor, open - cursor));
    const std::size_t close = text.find("}}", open + 2u);
    if (close == text.npos) {
      output.append(text.substr(open));
      break;
    }
    if (auto value = evaluate(text.substr(open + 2u, close - open - 2u),
                              context)) {
      output += value->toString();
    }
    cursor = close + 2u;
  }
  return output;
}

bool BindingEngine::validPath(std::string_view property_path) {
  return impl_->path(property_path) != nullptr;
}

bool BindingEngine::set(Value& model,
                        std::string_view property_path,
                        Value value) {
  const std::vector<PathSegment>* segments = impl_->path(property_path);
  if (segments == nullptr) return false;
  if (model.asObject() == nullptr) model = Value::Object{};
  Value* current = &model;
  for (std::size_t index = 0u; index < segments->size(); ++index) {
    Value::Object* object = current->asObject();
    if (object == nullptr) return false;
    const PathSegment& segment = (*segments)[index];
    const bool last = index + 1u == segments->size();
    if (!segment.index.has_value()) {
      if (last) {
        (*object)[segment.name] = std::move(value);
        return true;
      }
      Value& next = (*object)[segment.name];
      if (next.asObject() == nullptr) next = Value::Object{};
      current = &next;
      continue;
    }
    Value& array_value = (*object)[segment.name];
    if (array_value.asArray() == nullptr) array_value = Value::Array{};
    Value::Array* array = array_value.asArray();
    if (*segment.index >= array->size()) array->resize(*segment.index + 1u);
    if (last) {
      (*array)[*segment.index] = std::move(value);
      return true;
    }
    if ((*array)[*segment.index].asObject() == nullptr) {
      (*array)[*segment.index] = Value::Object{};
    }
    current = &(*array)[*segment.index];
  }
  return false;
}

std::optional<Value> BindingEngine::get(
    const Value& model,
    std::string_view property_path) {
  const std::vector<PathSegment>* segments = impl_->path(property_path);
  return segments == nullptr ? std::nullopt : impl_->descend(model, *segments);
}

void BindingEngine::clear() {
  impl_->expressions.clear();
  impl_->paths.clear();
  impl_->expression_compile_count = 0u;
  impl_->path_parse_count = 0u;
}

std::size_t BindingEngine::cachedExpressionCount() const {
  return impl_->expressions.size();
}

std::size_t BindingEngine::cachedPathCount() const {
  return impl_->paths.size();
}

std::size_t BindingEngine::expressionCompileCount() const {
  return impl_->expression_compile_count;
}

std::size_t BindingEngine::pathParseCount() const {
  return impl_->path_parse_count;
}

}  // namespace karma::ui::native
