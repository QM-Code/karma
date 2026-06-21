#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "karma/content/assets/asset_registry.h"
#include "karma/content/image/image.h"

namespace karma::content::detail {

std::string textureContentHash(const TextureAsset& texture);
std::string_view textureImporterVersion();
std::string_view textureDependencyVersion();
void bleedTransparentRgb(Rgba8Image& image);
TextureAsset makeTextureAssetFromImage(Rgba8Image image,
                                       bool srgb,
                                       bool generate_mips,
                                       TextureAsset::Semantic semantic,
                                       bool prefer_compressed,
                                       bool alpha_aware_mips = false,
                                       float alpha_coverage_cutoff = 0.5f);
std::string sanitizeTextureKeySegment(std::string_view value);
std::string importedTextureAlias(renderer::ImportedMaterialTextureSemantic semantic,
                                 std::string_view fallback);
TextureAsset::Semantic importedTextureSemantic(renderer::ImportedMaterialTextureSemantic semantic,
                                               bool srgb);
std::optional<Rgba8Image> decodeImportedTexture(
    const renderer::ImportedMaterialTexture& texture);
std::string importedTextureSourceHash(const renderer::ImportedMaterialTexture& texture);

}  // namespace karma::content::detail
