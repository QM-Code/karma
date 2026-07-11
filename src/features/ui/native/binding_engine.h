#pragma once

#include "karma/ui.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

struct BindingEvaluationContext {
  const Value* model = nullptr;
  const Value::Object* locals = nullptr;
};

/// Main-thread expression and property-path evaluator used by retained UI
/// bindings. Parsed expressions and paths are cached per System instance.
class BindingEngine {
 public:
  BindingEngine();
  ~BindingEngine();
  BindingEngine(const BindingEngine&) = delete;
  BindingEngine& operator=(const BindingEngine&) = delete;
  BindingEngine(BindingEngine&&) noexcept;
  BindingEngine& operator=(BindingEngine&&) noexcept;

  [[nodiscard]] std::optional<Value> evaluate(
      std::string_view expression,
      const BindingEvaluationContext& context);
  [[nodiscard]] std::string interpolate(
      std::string_view text,
      const BindingEvaluationContext& context);
  /// Model/local lookup paths referenced by a compiled expression.
  [[nodiscard]] std::vector<std::string> dependencies(
      std::string_view expression);

  [[nodiscard]] bool validPath(std::string_view property_path);
  bool set(Value& model, std::string_view property_path, Value value);
  [[nodiscard]] std::optional<Value> get(
      const Value& model,
      std::string_view property_path);

  void clear();
  [[nodiscard]] std::size_t cachedExpressionCount() const;
  [[nodiscard]] std::size_t cachedPathCount() const;
  [[nodiscard]] std::size_t expressionCompileCount() const;
  [[nodiscard]] std::size_t pathParseCount() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
