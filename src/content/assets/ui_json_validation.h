#pragma once

#include "content/assets/ui_json_profile.h"

#include <cstddef>
#include <string>
#include <vector>

namespace karma::assets::detail {

enum class UiJsonKind {
  Document,
  Theme,
};

struct UiJsonValidationIssue {
  std::string code;
  std::string message;
  std::string json_pointer;
  std::size_t line = 0;
  std::size_t column = 0;
};

/// Validates the shared JSON2 authoring contract. All issues point back to the
/// original JSON5 source through JsonProfileDocument::source_map.
std::vector<UiJsonValidationIssue> validateUiJsonProfile(
    const JsonProfileDocument& profile,
    UiJsonKind kind);

}  // namespace karma::assets::detail
