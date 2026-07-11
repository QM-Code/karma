#pragma once

#include "karma/scenes.h"
#include "scene_bake_artifacts.h"

namespace karma::scenes::detail {

bool bakeSceneLightmaps(const SceneDocument& document,
                        const SceneBakeDesc& desc,
                        world::World& world,
                        const SceneInstantiateResult& instance,
                        assets::AssetRegistry& assets,
                        std::string_view scene_fingerprint,
                        const SceneBakeExecutionOptions& execution,
                        BakeArtifactTransaction& artifacts,
                        SceneBakeResult& result);

}  // namespace karma::scenes::detail
