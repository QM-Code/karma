#include "karma/content/prefabs/prefab.h"

#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "prefab_parse_support.h"

namespace karma::prefabs {

namespace {

enum class ActiveSection : uint8_t {
  None = 0,
  Prefab = 1,
  Param = 2,
  Entry = 3,
};

bool activateSection(Prefab& prefab,
                     const detail::SectionHeader& header,
                     ActiveSection& active_section,
                     size_t& active_param_index,
                     size_t& active_entry_index,
                     std::string& out_error) {
  switch (header.kind) {
    case detail::SectionKind::Prefab:
      active_section = ActiveSection::Prefab;
      return true;

    case detail::SectionKind::Param:
      if (header.name.empty()) {
        out_error = "param section missing name";
        return false;
      }
      active_param_index = prefab.parameters.size();
      prefab.parameters.push_back(PrefabParameter{.name = header.name});
      active_section = ActiveSection::Param;
      return true;

    case detail::SectionKind::Mesh:
    case detail::SectionKind::Particle:
    case detail::SectionKind::Light:
    case detail::SectionKind::Beam:
    case detail::SectionKind::VolumeSphere: {
      if (header.name.empty()) {
        out_error =
            std::string(detail::sectionKindLabel(header.kind)) + " section missing name";
        return false;
      }

      const auto entry_type = detail::entryTypeForSection(header.kind);
      if (!entry_type.has_value()) {
        out_error = "unknown prefab entry section";
        return false;
      }

      PrefabEntry entry{};
      entry.type = *entry_type;
      entry.name = header.name;
      active_entry_index = prefab.entries.size();
      prefab.entries.push_back(std::move(entry));
      active_section = ActiveSection::Entry;
      return true;
    }

    case detail::SectionKind::Invalid:
      out_error = "invalid section header";
      return false;
  }

  out_error = "invalid section header";
  return false;
}

}  // namespace

bool loadPrefab(const std::filesystem::path& path, Prefab& out_prefab) {
  const auto resolved_path = detail::resolvePrefabSourcePath(path);
  if (!resolved_path.has_value()) {
    spdlog::error("Prefab load failed: could not resolve {}", path.string());
    return false;
  }

  std::ifstream file(*resolved_path);
  if (!file.is_open()) {
    spdlog::error("Prefab load failed: could not open {}", resolved_path->string());
    return false;
  }

  Prefab prefab{};
  prefab.source_path = *resolved_path;

  ActiveSection active_section = ActiveSection::None;
  size_t active_param_index = 0u;
  size_t active_entry_index = 0u;

  std::string line;
  size_t line_number = 0u;
  while (std::getline(file, line)) {
    line_number += 1u;

    const size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line.erase(comment_pos);
    }

    const std::string trimmed = detail::trim(line);
    if (trimmed.empty()) {
      continue;
    }

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      detail::SectionHeader header{};
      if (!detail::parseSectionHeader(
              std::string_view(trimmed).substr(1u, trimmed.size() - 2u), header)) {
        spdlog::error("Prefab parse failed: {}:{} invalid section header",
                      resolved_path->string(),
                      line_number);
        return false;
      }

      std::string section_error;
      if (!activateSection(prefab,
                           header,
                           active_section,
                           active_param_index,
                           active_entry_index,
                           section_error)) {
        spdlog::error("Prefab parse failed: {}:{} {}",
                      resolved_path->string(),
                      line_number,
                      section_error);
        return false;
      }
      continue;
    }

    const size_t equals_pos = trimmed.find('=');
    if (equals_pos == std::string::npos) {
      spdlog::error("Prefab parse failed: {}:{} missing '='",
                    resolved_path->string(),
                    line_number);
      return false;
    }

    const std::string key =
        detail::lowercase(detail::trim(std::string_view(trimmed).substr(0u, equals_pos)));
    const std::string value = detail::trim(std::string_view(trimmed).substr(equals_pos + 1u));

    std::string parse_error;
    bool ok = false;
    switch (active_section) {
      case ActiveSection::Prefab:
        ok = detail::applyPrefabField(prefab, key, value, parse_error);
        break;

      case ActiveSection::Param:
        ok = detail::applyParameterField(
            prefab.parameters[active_param_index], key, value, parse_error);
        break;

      case ActiveSection::Entry:
        ok = detail::applyEntryField(
            prefab, prefab.entries[active_entry_index], key, value, parse_error);
        break;

      case ActiveSection::None:
        parse_error = "field specified outside a section";
        break;
    }

    if (!ok) {
      spdlog::error("Prefab parse failed: {}:{} {}",
                    resolved_path->string(),
                    line_number,
                    parse_error.empty() ? "invalid value" : parse_error);
      return false;
    }
  }

  if (prefab.name.empty()) {
    prefab.name = resolved_path->stem().string();
  }

  out_prefab = std::move(prefab);
  return true;
}

std::optional<Prefab> loadPrefab(const std::filesystem::path& path) {
  Prefab prefab{};
  if (!loadPrefab(path, prefab)) {
    return std::nullopt;
  }
  return prefab;
}

}  // namespace karma::prefabs
