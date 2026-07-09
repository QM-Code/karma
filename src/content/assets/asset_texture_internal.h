#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "karma/assets.h"
#include "karma/assets.h"

namespace karma::assets::detail {

std::string textureContentHash(const TextureAsset& texture);
std::string_view textureImporterVersion();
std::string_view textureDependencyVersion();
std::string textureProfileVersion();
void bleedTransparentRgb(Rgba8Image& image);
TextureAsset makeTextureAssetFromImage(Rgba8Image image,
                                       bool srgb,
                                       bool generate_mips,
                                       TextureAsset::Semantic semantic,
                                       bool prefer_compressed,
                                       bool alpha_aware_mips = false,
                                       float alpha_coverage_cutoff = 0.5f);
std::string sanitizeTextureKeySegment(std::string_view value);
std::string importedTextureAlias(rendering::ImportedMaterialTextureSemantic semantic,
                                 std::string_view fallback);
TextureAsset::Semantic importedTextureSemantic(rendering::ImportedMaterialTextureSemantic semantic,
                                               bool srgb);
std::optional<Rgba8Image> decodeImportedTexture(
    const rendering::ImportedMaterialTexture& texture);
std::string importedTextureSourceHash(const rendering::ImportedMaterialTexture& texture);

}  // namespace karma::assets::detail
