#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace karma::scene {

using Json = nlohmann::json;

struct GltfDocument {
  Json json;
  std::vector<std::uint8_t> bin;

  bool valid() const {
    return json.is_object();
  }
};

GltfDocument loadGltfDocument(const std::filesystem::path& path);

bool readFloatAccessor(const GltfDocument& doc,
                       uint32_t accessor_index,
                       size_t expected_components,
                       std::vector<float>& out,
                       size_t* out_count = nullptr);

bool readVec4JointAccessor(const GltfDocument& doc,
                           uint32_t accessor_index,
                           std::vector<glm::uvec4>& out);

bool readVec4WeightAccessor(const GltfDocument& doc,
                            uint32_t accessor_index,
                            std::vector<glm::vec4>& out);

bool readIndexAccessor(const GltfDocument& doc,
                       uint32_t accessor_index,
                       std::vector<uint32_t>& out);

std::string gltfNodeName(const GltfDocument& doc, uint32_t node_index);

std::unordered_map<uint32_t, uint32_t> buildGltfNodeToPrefabIndex(
    const GltfDocument& doc,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name);

}  // namespace karma::scene
