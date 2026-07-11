#pragma once

#include "karma/ui.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

/// Appends one diagnostic unless an equivalent severity/code/message/source
/// entry already exists at the same line. Columns are attached by callers
/// after a successful append so source-mapped parsing retains its exact
/// first-diagnostic behavior.
void addDiagnostic(
    std::vector<Diagnostic>& diagnostics,
    std::string_view asset_key,
    std::string code,
    std::string message,
    std::size_t line = 0u,
    DiagnosticSeverity severity = DiagnosticSeverity::Error);

}  // namespace karma::ui::native
