#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "karma/assets.h"

namespace karma::assets::detail {

std::vector<uint8_t> serializeTexture(const TextureAsset& texture);
std::optional<TextureAsset> deserializeTexture(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic);

std::vector<uint8_t> serializeMesh(const world::MeshData& mesh);
std::optional<world::MeshData> deserializeMesh(const std::vector<uint8_t>& bytes,
                                                  std::string* diagnostic);

std::vector<uint8_t> serializeMaterialAsset(const rendering::MaterialAssetDesc& material);
std::optional<rendering::MaterialAssetDesc> deserializeMaterialAsset(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeMaterialVariant(const rendering::MaterialVariantDesc& material);
std::optional<rendering::MaterialVariantDesc> deserializeMaterialVariant(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeParticleEffect(const visual::particles::ParticleEffectAsset& effect);
std::optional<visual::particles::ParticleEffectAsset> deserializeParticleEffect(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeGltfScene(const GltfSceneAsset& scene);
std::optional<GltfSceneAsset> deserializeGltfScene(const std::vector<uint8_t>& bytes,
                                                   std::string* diagnostic);
std::vector<uint8_t> serializeAnimationClip(const world::AnimationClip& clip);
std::optional<world::AnimationClip> deserializeAnimationClip(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeSkeleton(const world::Skeleton& skeleton);
std::optional<world::Skeleton> deserializeSkeleton(const std::vector<uint8_t>& bytes,
                                                       std::string* diagnostic);
std::vector<uint8_t> serializeSkin(const world::Skin& skin);
std::optional<world::Skin> deserializeSkin(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic);

}  // namespace karma::assets::detail
