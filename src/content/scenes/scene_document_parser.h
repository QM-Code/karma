#pragma once

#include "karma/scenes.h"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace karma::scenes::detail {

bool parseSceneDocument(const nlohmann::json& root,
                        const std::filesystem::path& source_path,
                        SceneLoadResult& result);

}  // namespace karma::scenes::detail
