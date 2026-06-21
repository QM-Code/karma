#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "karma/content/assets/asset_registry.h"

namespace karma::content::detail {

std::vector<uint8_t> serializeTexture(const TextureAsset& texture);
std::optional<TextureAsset> deserializeTexture(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic);

std::vector<uint8_t> serializeMesh(const geometry::MeshData& mesh);
std::optional<geometry::MeshData> deserializeMesh(const std::vector<uint8_t>& bytes,
                                                  std::string* diagnostic);

std::vector<uint8_t> serializeMaterialAsset(const renderer::MaterialAssetDesc& material);
std::optional<renderer::MaterialAssetDesc> deserializeMaterialAsset(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeMaterialVariant(const renderer::MaterialVariantDesc& material);
std::optional<renderer::MaterialVariantDesc> deserializeMaterialVariant(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeParticleEffect(const particles::ParticleEffectAsset& effect);
std::optional<particles::ParticleEffectAsset> deserializeParticleEffect(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeGltfScene(const GltfSceneAsset& scene);
std::optional<GltfSceneAsset> deserializeGltfScene(const std::vector<uint8_t>& bytes,
                                                   std::string* diagnostic);
std::vector<uint8_t> serializeAnimationClip(const animation::AnimationClip& clip);
std::optional<animation::AnimationClip> deserializeAnimationClip(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic);
std::vector<uint8_t> serializeSkeleton(const animation::Skeleton& skeleton);
std::optional<animation::Skeleton> deserializeSkeleton(const std::vector<uint8_t>& bytes,
                                                       std::string* diagnostic);
std::vector<uint8_t> serializeSkin(const animation::Skin& skin);
std::optional<animation::Skin> deserializeSkin(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic);

}  // namespace karma::content::detail
