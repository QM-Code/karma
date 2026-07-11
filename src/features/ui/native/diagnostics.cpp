#include "features/ui/native/diagnostics.h"

#include <algorithm>
#include <utility>

namespace karma::ui::native {

void addDiagnostic(std::vector<Diagnostic>& diagnostics,
                   std::string_view asset_key,
                   std::string code,
                   std::string message,
                   std::size_t line,
                   DiagnosticSeverity severity) {
  if (std::any_of(
          diagnostics.begin(), diagnostics.end(),
          [&](const Diagnostic& existing) {
            return existing.severity == severity && existing.code == code &&
                   existing.message == message &&
                   existing.asset_key == asset_key && existing.line == line;
          })) {
    return;
  }
  diagnostics.push_back(Diagnostic{.severity = severity,
                                   .code = std::move(code),
                                   .message = std::move(message),
                                   .asset_key = std::string(asset_key),
                                   .line = line});
}

}  // namespace karma::ui::native
