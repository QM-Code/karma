#pragma once

#include <cstdint>
#include <limits>

namespace karma::renderer {

using InstanceId = uint64_t;
using MeshId = uint32_t;
using MaterialId = uint32_t;
using MaterialSetId = uint32_t;
using TextureId = uint32_t;
using RenderTargetId = uint32_t;
using LayerId = uint32_t;

constexpr RenderTargetId kDefaultRenderTarget = 0;
constexpr MaterialId kInvalidMaterial = 0;
constexpr MaterialSetId kInvalidMaterialSet = 0;
constexpr MeshId kInvalidMesh = 0;
constexpr TextureId kInvalidTexture = 0;
constexpr InstanceId kInvalidInstance = std::numeric_limits<InstanceId>::max();

}  // namespace karma::renderer
