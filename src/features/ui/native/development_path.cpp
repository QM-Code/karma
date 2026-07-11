#include "features/ui/native/development_path.h"

namespace karma::ui::native {
namespace {

bool isPathInside(const std::filesystem::path& candidate,
                  const std::filesystem::path& root) {
  std::error_code error;
  const std::filesystem::path relative =
      std::filesystem::relative(candidate, root, error);
  if (error || relative.empty()) return !error && candidate == root;
  const auto first = relative.begin();
  return first == relative.end() || *first != "..";
}

}  // namespace

bool isPortableDevelopmentPath(const std::filesystem::path& requested) {
  if (requested.empty() || requested.is_absolute() ||
      requested.has_root_name() || requested.has_root_directory()) {
    return false;
  }

  // Inspect native spelling as well as parsed root components. On POSIX a
  // backslash or Windows drive is otherwise just a legal filename; accepting
  // either would make the same authoring graph resolve differently by host.
  const auto& lexical = requested.native();
  using PathCharacter = std::filesystem::path::value_type;
  return lexical.find(static_cast<PathCharacter>('\\')) == lexical.npos &&
         lexical.find(static_cast<PathCharacter>(':')) == lexical.npos;
}

std::optional<std::filesystem::path> resolveDevelopmentPath(
    const std::filesystem::path& requested,
    const std::filesystem::path& referring_directory,
    const std::filesystem::path& root) {
  if (!isPortableDevelopmentPath(requested)) return std::nullopt;
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(referring_directory / requested, error);
  if (error || !isPathInside(canonical, root) ||
      !std::filesystem::is_regular_file(canonical, error) || error) {
    return std::nullopt;
  }
  return canonical;
}

}  // namespace karma::ui::native
