#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/foliage.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace karma::foliage::detail {

struct FoliageRenderPrototypePart {
  components::MeshComponent mesh;
  std::optional<components::LodComponent> lod;
  math::Vec3 local_position{};
  math::Quat local_rotation{};
  math::Vec3 local_scale{1.0f, 1.0f, 1.0f};
  std::vector<std::string> render_tags;
};

struct FoliageRenderPrototypeDiagnostic {
  std::filesystem::path path;
  std::string message;
};

/// Transactional output of direct-mesh or prefab prototype compilation.
/// A successful prefab result owns one shared package-store reference until
/// the caller passes it to `releaseFoliageRenderPrototypePackage`.
struct FoliageRenderPrototypeBuild {
  bool success = false;
  std::filesystem::path resolved_prefab_path;
  std::filesystem::file_time_type prefab_modified =
      std::filesystem::file_time_type::min();
  std::optional<assets::AssetPackageHandle> prefab_package;
  std::vector<FoliageRenderPrototypePart> parts;
  std::vector<FoliageRenderPrototypeDiagnostic> diagnostics;
};

std::filesystem::path resolveFoliagePrefabPath(
    std::filesystem::path path);

std::filesystem::file_time_type foliagePrefabModifiedTime(
    const std::filesystem::path& path);

FoliageRenderPrototypeBuild buildFoliageRenderPrototype(
    world::World& world,
    world::Entity source,
    const components::FoliageComponent& component,
    const std::filesystem::path& reference_root,
    assets::AssetRegistry* assets);

void releaseFoliageRenderPrototypePackage(
    assets::AssetRegistry* assets,
    std::optional<assets::AssetPackageHandle>& package);

}  // namespace karma::foliage::detail
