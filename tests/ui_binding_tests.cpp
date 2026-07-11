#include "features/ui/native/binding_engine.h"

#include <cassert>
#include <iostream>

int main() {
  using karma::ui::Value;
  using karma::ui::native::BindingEngine;
  using karma::ui::native::BindingEvaluationContext;

  BindingEngine bindings;
  Value model = Value::Object{
      {"enabled", true},
      {"count", 3},
      {"name", "Karma"},
      {"rows", Value::Array{
                   Value::Object{{"id", 10}, {"title", "First"}},
                   Value::Object{{"id", 20}, {"title", "Second"}},
               }},
  };
  Value::Object locals{{"row", Value::Object{{"id", 20}}}, {"index", 1}};
  const BindingEvaluationContext context{.model = &model, .locals = &locals};

  assert(bindings.evaluate("enabled && count >= 3", context) == Value(true));
  assert(bindings.evaluate("!enabled || count < 2", context) == Value(false));
  assert(bindings.evaluate("row.id == rows[1].id", context) == Value(true));
  assert(bindings.evaluate("enabled ? name : 'disabled'", context) ==
         Value("Karma"));
  assert(bindings.interpolate("{{ name }} #{{ index }}", context) ==
         "Karma #1");
  assert(!bindings.evaluate("enabled &&", context).has_value());

  const std::size_t compiled = bindings.expressionCompileCount();
  for (int iteration = 0; iteration < 100; ++iteration) {
    assert(bindings.evaluate("enabled && count >= 3", context) == Value(true));
    assert(bindings.get(model, "rows[1].title") == Value("Second"));
  }
  assert(bindings.expressionCompileCount() == compiled);
  assert(bindings.cachedExpressionCount() >= 5u);
  assert(bindings.pathParseCount() <= 8u);

  assert(bindings.set(model, "settings.audio.volume", 0.75));
  assert(bindings.get(model, "settings.audio.volume") == Value(0.75));
  assert(bindings.set(model, "rows[2].title", "Third"));
  assert(bindings.get(model, "rows[2].title") == Value("Third"));
  assert(bindings.validPath("settings.audio.volume"));
  assert(!bindings.validPath(""));
  assert(!bindings.validPath("trailing."));
  assert(!bindings.set(model, "../escape", true));
  assert(!bindings.set(model, "trailing.", true));

  std::cout << "native UI binding tests passed\n";
  return 0;
}
