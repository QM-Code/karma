#pragma once

#include <cstdint>
#include <limits>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Stable renderer instance handle used for submitted draw records.
using InstanceId = uint64_t;
/// \ingroup karma_rendering
/// Opaque mesh resource handle.
using MeshId = uint32_t;
/// \ingroup karma_rendering
/// Opaque material resource handle.
using MaterialId = uint32_t;
/// \ingroup karma_rendering
/// Opaque material-set resource handle.
using MaterialSetId = uint32_t;
/// \ingroup karma_rendering
/// Opaque texture resource handle.
using TextureId = uint32_t;
/// \ingroup karma_rendering
/// Opaque render-target resource handle.
using RenderTargetId = uint32_t;
/// \ingroup karma_rendering
/// Opaque streamed terrain resource handle.
using TerrainId = uint32_t;
/// \ingroup karma_rendering
/// Render layer id used to submit and draw batches independently.
using LayerId = uint32_t;

constexpr RenderTargetId kDefaultRenderTarget = 0;
constexpr MaterialId kInvalidMaterial = 0;
constexpr MaterialSetId kInvalidMaterialSet = 0;
constexpr MeshId kInvalidMesh = 0;
constexpr TextureId kInvalidTexture = 0;
constexpr TerrainId kInvalidTerrain = 0;
constexpr InstanceId kInvalidInstance = std::numeric_limits<InstanceId>::max();

}  // namespace karma::renderer
