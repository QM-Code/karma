#include "karma/renderer/backends/diligent/backend.hpp"

#include "../backend_internal.h"
#include "pass_shared.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

namespace karma::renderer_backend {

namespace {

using ParticleDrawClock = std::chrono::steady_clock;

float elapsedMilliseconds(const ParticleDrawClock::time_point& start,
                          const ParticleDrawClock::time_point& end) {
  return std::chrono::duration<float, std::milli>(end - start).count();
}

std::uint32_t floatBits(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

template <typename T>
void hashCombine(std::size_t& seed, const T& value) {
  const std::size_t value_hash = std::hash<T>{}(value);
  seed ^= value_hash + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
}

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

struct ParticleBatchGroupKey {
  renderer::TextureId texture = renderer::kInvalidTexture;
  renderer::ParticleAlignment alignment = renderer::ParticleAlignment::Billboard;
  renderer::ParticleShadingMode shading_mode = renderer::ParticleShadingMode::Standard;
  renderer::ParticlePresentationMode presentation_mode =
      renderer::ParticlePresentationMode::Baked;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  std::uint32_t atlas_columns = 1u;
  std::uint32_t atlas_rows = 1u;
  std::uint32_t atlas_frame_count = 1u;
  bool animate_over_lifetime = false;
  std::uint32_t atlas_frame_width = 0u;
  std::uint32_t atlas_frame_height = 0u;
  std::uint32_t atlas_border_x = 0u;
  std::uint32_t atlas_border_y = 0u;
  std::uint32_t atlas_spacing_x = 0u;
  std::uint32_t atlas_spacing_y = 0u;
  float animation_fps = 0.0f;

  bool operator==(const ParticleBatchGroupKey& other) const {
    return texture == other.texture &&
           alignment == other.alignment &&
           shading_mode == other.shading_mode &&
           presentation_mode == other.presentation_mode &&
           use_soft_mask == other.use_soft_mask &&
           floatBits(soft_particle_distance) == floatBits(other.soft_particle_distance) &&
           floatBits(distortion_strength) == floatBits(other.distortion_strength) &&
           floatBits(fresnel_power) == floatBits(other.fresnel_power) &&
           floatBits(fresnel_strength) == floatBits(other.fresnel_strength) &&
           floatBits(refraction_strength) == floatBits(other.refraction_strength) &&
           floatBits(interior_glow) == floatBits(other.interior_glow) &&
           floatBits(size_curve_exponent) == floatBits(other.size_curve_exponent) &&
           floatBits(alpha_curve_exponent) == floatBits(other.alpha_curve_exponent) &&
           atlas_columns == other.atlas_columns &&
           atlas_rows == other.atlas_rows &&
           atlas_frame_count == other.atlas_frame_count &&
           animate_over_lifetime == other.animate_over_lifetime &&
           atlas_frame_width == other.atlas_frame_width &&
           atlas_frame_height == other.atlas_frame_height &&
           atlas_border_x == other.atlas_border_x &&
           atlas_border_y == other.atlas_border_y &&
           atlas_spacing_x == other.atlas_spacing_x &&
           atlas_spacing_y == other.atlas_spacing_y &&
           floatBits(animation_fps) == floatBits(other.animation_fps);
  }
};

struct ParticleBatchGroupKeyHash {
  std::size_t operator()(const ParticleBatchGroupKey& key) const {
    std::size_t seed = 0;
    hashCombine(seed, key.texture);
    hashCombine(seed, static_cast<std::uint32_t>(key.alignment));
    hashCombine(seed, static_cast<std::uint32_t>(key.shading_mode));
    hashCombine(seed, static_cast<std::uint32_t>(key.presentation_mode));
    hashCombine(seed, key.use_soft_mask);
    hashCombine(seed, floatBits(key.soft_particle_distance));
    hashCombine(seed, floatBits(key.distortion_strength));
    hashCombine(seed, floatBits(key.fresnel_power));
    hashCombine(seed, floatBits(key.fresnel_strength));
    hashCombine(seed, floatBits(key.refraction_strength));
    hashCombine(seed, floatBits(key.interior_glow));
    hashCombine(seed, floatBits(key.size_curve_exponent));
    hashCombine(seed, floatBits(key.alpha_curve_exponent));
    hashCombine(seed, key.atlas_columns);
    hashCombine(seed, key.atlas_rows);
    hashCombine(seed, key.atlas_frame_count);
    hashCombine(seed, key.animate_over_lifetime);
    hashCombine(seed, key.atlas_frame_width);
    hashCombine(seed, key.atlas_frame_height);
    hashCombine(seed, key.atlas_border_x);
    hashCombine(seed, key.atlas_border_y);
    hashCombine(seed, key.atlas_spacing_x);
    hashCombine(seed, key.atlas_spacing_y);
    hashCombine(seed, floatBits(key.animation_fps));
    return seed;
  }
};

struct AdditiveParticleGroup {
  ParticleBatchGroupKey key{};
  std::vector<ParticleInstanceGpu> gpu_particles;
};

struct PreparedParticleSpan {
  ParticleBatchGroupKey key{};
  std::size_t particle_offset = 0u;
  std::size_t particle_count = 0u;
};

struct PreparedParticleStream {
  std::vector<ParticleInstanceGpu> particles;
  std::vector<PreparedParticleSpan> spans;
};

struct SortedParticle {
  const renderer::ParticlePackedInstance* particle = nullptr;
  std::size_t batch_index = 0u;
  std::size_t draw_state_key = 0u;
  float depth = 0.0f;
};

bool sortedParticleLess(const SortedParticle& a, const SortedParticle& b) {
  if (a.depth != b.depth) {
    return a.depth > b.depth;
  }
  if (a.draw_state_key != b.draw_state_key) {
    return a.draw_state_key < b.draw_state_key;
  }
  if (a.batch_index != b.batch_index) {
    return a.batch_index < b.batch_index;
  }
  return reinterpret_cast<std::uintptr_t>(a.particle) <
         reinterpret_cast<std::uintptr_t>(b.particle);
}

Diligent::Viewport buildViewport(int render_width, int render_height) {
  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  return viewport;
}

#if defined(NDEBUG)
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_NONE;
#else
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_VERIFY_ALL;
#endif

}  // namespace

void DiligentBackend::renderParticlePasses(renderer::LayerId layer,
                                           const ParticlePassContext& context) {
  auto make_batch_group_key = [](const auto& batch) {
    return ParticleBatchGroupKey{
        .texture = batch.texture,
        .alignment = batch.alignment,
        .shading_mode = batch.shading_mode,
        .presentation_mode = batch.presentation_mode,
        .use_soft_mask = batch.use_soft_mask,
        .soft_particle_distance = batch.soft_particle_distance,
        .distortion_strength = batch.distortion_strength,
        .fresnel_power = batch.fresnel_power,
        .fresnel_strength = batch.fresnel_strength,
        .refraction_strength = batch.refraction_strength,
        .interior_glow = batch.interior_glow,
        .size_curve_exponent = batch.size_curve_exponent,
        .alpha_curve_exponent = batch.alpha_curve_exponent,
        .atlas_columns = batch.atlas_columns,
        .atlas_rows = batch.atlas_rows,
        .atlas_frame_count = batch.atlas_frame_count,
        .animate_over_lifetime = batch.animate_over_lifetime,
        .atlas_frame_width = batch.atlas_frame_width,
        .atlas_frame_height = batch.atlas_frame_height,
        .atlas_border_x = batch.atlas_border_x,
        .atlas_border_y = batch.atlas_border_y,
        .atlas_spacing_x = batch.atlas_spacing_x,
        .atlas_spacing_y = batch.atlas_spacing_y,
        .animation_fps = batch.animation_fps,
    };
  };

  auto resolve_particle_texture = [&](renderer::TextureId texture_id) -> Diligent::ITextureView* {
    if (texture_id != renderer::kInvalidTexture) {
      auto it = textures_.find(texture_id);
      if (it != textures_.end() && it->second.srv) {
        return it->second.srv;
      }
    }
    return default_base_color_;
  };

  auto ensure_particle_instance_buffer = [&](std::size_t particle_count) {
    if (!particle_instance_vb_ || particle_instance_capacity_ < particle_count) {
      const std::size_t new_capacity =
          std::max(particle_count,
                   particle_instance_capacity_ > 0
                       ? particle_instance_capacity_ * 2
                       : static_cast<std::size_t>(256));
      Diligent::BufferDesc vb_desc{};
      vb_desc.Name = "Karma Particle Instance VB";
      vb_desc.Usage = Diligent::USAGE_DYNAMIC;
      vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
      vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
      vb_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(ParticleInstanceGpu));
      device_->CreateBuffer(vb_desc, nullptr, &particle_instance_vb_);
      if (!particle_instance_vb_) {
        particle_instance_capacity_ = 0;
        return false;
      }
      particle_instance_capacity_ = new_capacity;
    }
    return true;
  };

  struct ParticleDrawTarget {
    Diligent::ITextureView* rtv = nullptr;
    Diligent::ITextureView* dsv = nullptr;
    int width = 0;
    int height = 0;
    bool force_scene_depth_clip = false;
  };

  const ParticleDrawTarget default_target{
      .rtv = context.active_rtv,
      .dsv = context.particle_dsv ? context.particle_dsv : context.active_dsv,
      .width = context.render_width,
      .height = context.render_height,
      .force_scene_depth_clip = false,
  };

  thread_local std::vector<ParticleBatchGroupKey> batch_group_keys;
  batch_group_keys.resize(particle_batches_.size());
  for (std::size_t batch_index = 0; batch_index < particle_batches_.size(); ++batch_index) {
    batch_group_keys[batch_index] = make_batch_group_key(particle_batches_[batch_index]);
  }

  bool has_alpha_particles = false;
  for (const auto& batch : particle_batches_) {
    if (batch.layer == layer &&
        batch.blend_mode == renderer::ParticleBlendMode::Alpha &&
        !batch.particles.empty()) {
      has_alpha_particles = true;
      break;
    }
  }

  bool use_half_res_alpha = false;
  ParticleDrawTarget half_res_alpha_target{};
  if (has_alpha_particles && context.scene_color_format != Diligent::TEX_FORMAT_UNKNOWN) {
    const int half_res_width = std::max(1, (context.render_width + 1) / 2);
    const int half_res_height = std::max(1, (context.render_height + 1) / 2);
    ensureParticleHalfResAlphaResources(
        half_res_width,
        half_res_height,
        context.scene_color_format);
    if (particle_half_res_alpha_rtv_ &&
        particle_half_res_alpha_srv_ &&
        particle_pipeline_state_alpha_half_res_ &&
        particle_srb_alpha_half_res_ &&
        particle_half_res_composite_pipeline_state_ &&
        particle_half_res_composite_srb_ &&
        particle_half_res_alpha_var_) {
      constexpr float kTransparentBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      context_->ClearRenderTarget(particle_half_res_alpha_rtv_,
                                  kTransparentBlack,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      use_half_res_alpha = true;
      half_res_alpha_target.rtv = particle_half_res_alpha_rtv_;
      half_res_alpha_target.width = half_res_width;
      half_res_alpha_target.height = half_res_height;
    }
  }
  particle_pass_stats_.alpha_half_res = use_half_res_alpha;

  thread_local PreparedParticleStream prepared_stream;
  thread_local std::vector<AdditiveParticleGroup> additive_groups;
  thread_local std::unordered_map<ParticleBatchGroupKey,
                                  std::size_t,
                                  ParticleBatchGroupKeyHash>
      additive_group_lookup;
  thread_local std::vector<SortedParticle> sorted_particles;

  auto upload_prepared_particles = [&](const PreparedParticleStream& stream) {
    if (stream.particles.empty()) {
      return false;
    }
    if (!ensure_particle_instance_buffer(stream.particles.size())) {
      return false;
    }
    Diligent::MapHelper<ParticleInstanceGpu> instance_map(
        context_, particle_instance_vb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    auto* mapped_particles = getMappedData(instance_map);
    if (mapped_particles == nullptr) {
      return false;
    }
    std::memcpy(mapped_particles,
                stream.particles.data(),
                stream.particles.size() * sizeof(ParticleInstanceGpu));
    return true;
  };

  auto draw_prepared_particles =
      [&](renderer::ParticleBlendMode blend_mode,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
          Diligent::IShaderResourceVariable* texture_var,
          Diligent::IShaderResourceVariable* scene_color_var,
          Diligent::IShaderResourceVariable* scene_depth_var,
          const ParticleDrawTarget& draw_target,
          const PreparedParticleStream& stream) {
    if (!pso || !particle_cb_ || !particle_vb_ || !draw_target.rtv || draw_target.width <= 0 ||
        draw_target.height <= 0 || stream.particles.empty() || stream.spans.empty()) {
      return;
    }

    const auto submission_start = ParticleDrawClock::now();
    if (!upload_prepared_particles(stream)) {
      return;
    }

    Diligent::IShaderResourceBinding* current_srb = nullptr;
    Diligent::ITextureView* current_texture = nullptr;
    Diligent::ITextureView* current_scene_color = nullptr;
    Diligent::ITextureView* current_scene_depth = nullptr;
    bool pipeline_bound = false;

    for (const auto& span : stream.spans) {
      if (span.particle_count == 0u) {
        continue;
      }

      const auto& key = span.key;
      ParticleConstants constants{};
      copyMat4(constants.view_proj, context.view_proj);
      glm::vec3 particle_right = context.camera_right;
      glm::vec3 particle_up = context.camera_up;
      if (key.alignment == renderer::ParticleAlignment::Ground) {
        particle_right = glm::vec3(1.0f, 0.0f, 0.0f);
        particle_up = glm::vec3(0.0f, 0.0f, 1.0f);
      }
      constants.camera_right[0] = particle_right.x;
      constants.camera_right[1] = particle_right.y;
      constants.camera_right[2] = particle_right.z;
      constants.camera_right[3] = 0.0f;
      constants.camera_up[0] = particle_up.x;
      constants.camera_up[1] = particle_up.y;
      constants.camera_up[2] = particle_up.z;
      constants.camera_up[3] = 0.0f;
      constants.camera_forward[0] = context.camera_forward.x;
      constants.camera_forward[1] = context.camera_forward.y;
      constants.camera_forward[2] = context.camera_forward.z;
      constants.camera_forward[3] = 0.0f;
      constants.params[0] = key.use_soft_mask ? 1.0f : 0.0f;
      constants.params[1] = static_cast<float>(static_cast<std::uint32_t>(blend_mode));
      constants.params[2] = std::max(key.soft_particle_distance, 0.0f);
      constants.params[3] = std::max(key.distortion_strength, 0.0f);
      constants.screen_params[0] = static_cast<float>(draw_target.width);
      constants.screen_params[1] = static_cast<float>(draw_target.height);
      constants.screen_params[2] =
          draw_target.width > 0 ? 1.0f / static_cast<float>(draw_target.width) : 0.0f;
      constants.screen_params[3] =
          draw_target.height > 0 ? 1.0f / static_cast<float>(draw_target.height) : 0.0f;
      constants.camera_params[0] = std::max(camera_.near_clip, 0.001f);
      constants.camera_params[1] =
          std::max(camera_.far_clip, constants.camera_params[0] + 0.001f);
      constants.camera_params[2] = camera_.perspective ? 1.0f : 0.0f;
      constants.camera_params[3] =
          static_cast<float>(static_cast<std::uint32_t>(key.shading_mode));
      constants.camera_position[0] = camera_.position.x;
      constants.camera_position[1] = camera_.position.y;
      constants.camera_position[2] = camera_.position.z;
      constants.camera_position[3] = 1.0f;
      constants.shading_params[0] = std::max(key.fresnel_power, 0.001f);
      constants.shading_params[1] = std::max(key.fresnel_strength, 0.0f);
      constants.shading_params[2] = std::max(key.refraction_strength, 0.0f);
      constants.shading_params[3] = std::max(key.interior_glow, 0.0f);
      constants.presentation_params[0] = std::max(key.size_curve_exponent, 0.001f);
      constants.presentation_params[1] = std::max(key.alpha_curve_exponent, 0.001f);
      constants.presentation_params[2] =
          static_cast<float>(static_cast<std::uint32_t>(key.presentation_mode));
      constants.presentation_params[3] = draw_target.force_scene_depth_clip ? 1.0f : 0.0f;
      constants.atlas_params0[0] = static_cast<float>(std::max(key.atlas_columns, 1u));
      constants.atlas_params0[1] = static_cast<float>(std::max(key.atlas_rows, 1u));
      constants.atlas_params0[2] = static_cast<float>(std::max(key.atlas_frame_count, 1u));
      constants.atlas_params0[3] = key.animate_over_lifetime ? 1.0f : 0.0f;
      constants.atlas_params1[0] = static_cast<float>(key.atlas_frame_width);
      constants.atlas_params1[1] = static_cast<float>(key.atlas_frame_height);
      constants.atlas_params1[2] = static_cast<float>(key.atlas_border_x);
      constants.atlas_params1[3] = static_cast<float>(key.atlas_border_y);
      constants.atlas_params2[0] = static_cast<float>(key.atlas_spacing_x);
      constants.atlas_params2[1] = static_cast<float>(key.atlas_spacing_y);
      constants.atlas_params2[2] = std::max(key.animation_fps, 0.0f);
      constants.atlas_params2[3] = 0.0f;
      {
        Diligent::MapHelper<ParticleConstants> cb_map(context_,
                                                      particle_cb_,
                                                      Diligent::MAP_WRITE,
                                                      Diligent::MAP_FLAG_DISCARD);
        auto* mapped_constants = getMappedData(cb_map);
        if (mapped_constants == nullptr) {
          return;
        }
        *mapped_constants = constants;
      }

      if (!pipeline_bound) {
        auto* rtv = draw_target.rtv;
        auto* dsv = draw_target.dsv;
        const Diligent::Viewport viewport = buildViewport(draw_target.width, draw_target.height);
        context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context_->SetViewports(1,
                               &viewport,
                               static_cast<Diligent::Uint32>(draw_target.width),
                               static_cast<Diligent::Uint32>(draw_target.height));
        context_->SetPipelineState(pso);
        pipeline_bound = true;
      }

      Diligent::ITextureView* desired_texture = resolve_particle_texture(key.texture);
      if (texture_var && desired_texture &&
          (desired_texture != current_texture || current_srb != srb)) {
        texture_var->Set(desired_texture);
        current_texture = desired_texture;
        current_srb = nullptr;
      }
      Diligent::ITextureView* desired_scene_color =
          context.particle_scene_color_sample_srv ? context.particle_scene_color_sample_srv
                                                  : default_base_color_;
      if (scene_color_var && desired_scene_color &&
          (desired_scene_color != current_scene_color || current_srb != srb)) {
        scene_color_var->Set(desired_scene_color);
        current_scene_color = desired_scene_color;
        current_srb = nullptr;
      }
      Diligent::ITextureView* desired_scene_depth =
          context.particle_scene_depth_srv ? context.particle_scene_depth_srv
                                           : particle_fallback_depth_srv_;
      if (scene_depth_var && desired_scene_depth &&
          (desired_scene_depth != current_scene_depth || current_srb != srb)) {
        scene_depth_var->Set(desired_scene_depth);
        current_scene_depth = desired_scene_depth;
        current_srb = nullptr;
      }
      if (srb && srb != current_srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        current_srb = srb;
      }

      Diligent::IBuffer* vbs[] = {particle_vb_, particle_instance_vb_};
      Diligent::Uint64 offsets[] = {
          0,
          static_cast<Diligent::Uint64>(span.particle_offset * sizeof(ParticleInstanceGpu))};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

      Diligent::DrawAttribs draw{};
      draw.NumVertices = static_cast<Diligent::Uint32>(kParticleQuadVertexCount);
      draw.NumInstances = static_cast<Diligent::Uint32>(span.particle_count);
      draw.Flags = kHotPathDrawFlags;
      switch (blend_mode) {
        case renderer::ParticleBlendMode::Additive:
          particle_pass_stats_.additive_draw_calls += 1u;
          break;
        case renderer::ParticleBlendMode::Alpha:
          particle_pass_stats_.alpha_draw_calls += 1u;
          break;
        case renderer::ParticleBlendMode::Distortion:
          particle_pass_stats_.distortion_draw_calls += 1u;
          break;
      }
      context_->Draw(draw);
    }

    particle_pass_stats_.draw_submission_ms +=
        elapsedMilliseconds(submission_start, ParticleDrawClock::now());
  };

  auto build_additive_stream = [&](bool depth_test, PreparedParticleStream& stream) {
    const auto grouping_start = ParticleDrawClock::now();
    stream.particles.clear();
    stream.spans.clear();
    additive_groups.clear();
    additive_group_lookup.clear();
    additive_groups.reserve(particle_batches_.size());
    additive_group_lookup.reserve(particle_batches_.size());

    std::size_t total_particles = 0u;
    for (std::size_t batch_index = 0; batch_index < particle_batches_.size(); ++batch_index) {
      const auto& batch = particle_batches_[batch_index];
      if (batch.layer != layer ||
          batch.depth_test != depth_test ||
          batch.blend_mode != renderer::ParticleBlendMode::Additive ||
          batch.particles.empty()) {
        continue;
      }

      total_particles += batch.particles.size();
      const ParticleBatchGroupKey& key = batch_group_keys[batch_index];
      auto [it, inserted] = additive_group_lookup.emplace(key, additive_groups.size());
      if (inserted) {
        additive_groups.push_back(AdditiveParticleGroup{.key = key});
      }
      auto& group = additive_groups[it->second];
      group.gpu_particles.reserve(group.gpu_particles.size() + batch.particles.size());
      group.gpu_particles.insert(group.gpu_particles.end(),
                                 batch.particles.begin(),
                                 batch.particles.end());
    }

    stream.particles.reserve(total_particles);
    stream.spans.reserve(additive_groups.size());
    for (const auto& group : additive_groups) {
      if (group.gpu_particles.empty()) {
        continue;
      }
      const std::size_t particle_offset = stream.particles.size();
      stream.particles.insert(stream.particles.end(),
                              group.gpu_particles.begin(),
                              group.gpu_particles.end());
      stream.spans.push_back(PreparedParticleSpan{
          .key = group.key,
          .particle_offset = particle_offset,
          .particle_count = group.gpu_particles.size(),
      });
    }

    particle_pass_stats_.additive_grouping_ms +=
        elapsedMilliseconds(grouping_start, ParticleDrawClock::now());
  };

  struct SortedStreamBuildMetrics {
    float collect_ms = 0.0f;
    float sort_only_ms = 0.0f;
    float span_ms = 0.0f;
  };

  auto build_sorted_stream = [&](renderer::ParticleBlendMode blend_mode,
                                 bool depth_test,
                                 PreparedParticleStream& stream) {
    SortedStreamBuildMetrics metrics{};
    stream.particles.clear();
    stream.spans.clear();

    const auto collect_start = ParticleDrawClock::now();
    sorted_particles.clear();
    for (std::size_t batch_index = 0; batch_index < particle_batches_.size(); ++batch_index) {
      const auto& batch = particle_batches_[batch_index];
      if (batch.layer != layer ||
          batch.depth_test != depth_test ||
          batch.blend_mode != blend_mode ||
          batch.particles.empty()) {
        continue;
      }

      const std::size_t draw_state_key = ParticleBatchGroupKeyHash{}(batch_group_keys[batch_index]);
      sorted_particles.reserve(sorted_particles.size() + batch.particles.size());
      for (const auto& particle : batch.particles) {
        const glm::vec3 position(
            particle.position_age[0],
            particle.position_age[1],
            particle.position_age[2]);
        float depth = glm::dot(position - camera_.position, context.camera_forward);
        if (!std::isfinite(depth)) {
          if (blend_mode == renderer::ParticleBlendMode::Alpha) {
            particle_pass_stats_.alpha_invalid_depth_particles += 1u;
          } else if (blend_mode == renderer::ParticleBlendMode::Distortion) {
            particle_pass_stats_.distortion_invalid_depth_particles += 1u;
          }
          depth = -std::numeric_limits<float>::infinity();
        }
        sorted_particles.push_back(SortedParticle{
            .particle = &particle,
            .batch_index = batch_index,
            .draw_state_key = draw_state_key,
            .depth = depth,
        });
      }
    }
    metrics.collect_ms = elapsedMilliseconds(collect_start, ParticleDrawClock::now());

    if (sorted_particles.empty()) {
      return metrics;
    }

    const uint32_t sorted_particle_count = static_cast<uint32_t>(std::min<std::size_t>(
        sorted_particles.size(),
        static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
    if (blend_mode == renderer::ParticleBlendMode::Alpha) {
      particle_pass_stats_.alpha_sorted_particles += sorted_particle_count;
    } else if (blend_mode == renderer::ParticleBlendMode::Distortion) {
      particle_pass_stats_.distortion_sorted_particles += sorted_particle_count;
    }

    const auto sort_only_start = ParticleDrawClock::now();
    if (sorted_particles.size() > 1u) {
      std::sort(sorted_particles.begin(), sorted_particles.end(), sortedParticleLess);
    }
    metrics.sort_only_ms = elapsedMilliseconds(sort_only_start, ParticleDrawClock::now());

    const auto span_start = ParticleDrawClock::now();
    stream.particles.reserve(sorted_particles.size());
    stream.spans.reserve(sorted_particles.size());
    std::size_t start = 0u;
    while (start < sorted_particles.size()) {
      const ParticleBatchGroupKey span_key = batch_group_keys[sorted_particles[start].batch_index];
      const std::size_t particle_offset = stream.particles.size();
      std::size_t end = start;
      while (end < sorted_particles.size() &&
             batch_group_keys[sorted_particles[end].batch_index] == span_key) {
        stream.particles.push_back(*sorted_particles[end].particle);
        ++end;
      }
      stream.spans.push_back(PreparedParticleSpan{
          .key = span_key,
          .particle_offset = particle_offset,
          .particle_count = stream.particles.size() - particle_offset,
      });
      start = end;
    }
    metrics.span_ms = elapsedMilliseconds(span_start, ParticleDrawClock::now());
    return metrics;
  };

  auto record_sorted_stream_metrics = [&](renderer::ParticleBlendMode blend_mode,
                                          const SortedStreamBuildMetrics& metrics) {
    if (blend_mode == renderer::ParticleBlendMode::Alpha) {
      particle_pass_stats_.alpha_collect_ms += metrics.collect_ms;
      particle_pass_stats_.alpha_sort_only_ms += metrics.sort_only_ms;
      particle_pass_stats_.alpha_span_ms += metrics.span_ms;
      particle_pass_stats_.alpha_sort_ms +=
          metrics.collect_ms + metrics.sort_only_ms + metrics.span_ms;
    } else if (blend_mode == renderer::ParticleBlendMode::Distortion) {
      particle_pass_stats_.distortion_collect_ms += metrics.collect_ms;
      particle_pass_stats_.distortion_sort_only_ms += metrics.sort_only_ms;
      particle_pass_stats_.distortion_span_ms += metrics.span_ms;
      particle_pass_stats_.distortion_sort_ms +=
          metrics.collect_ms + metrics.sort_only_ms + metrics.span_ms;
    }
  };

  auto composite_half_res_alpha = [&] {
    if (!use_half_res_alpha ||
        !particle_half_res_alpha_srv_ ||
        !particle_half_res_alpha_var_ ||
        !particle_half_res_composite_pipeline_state_ ||
        !particle_half_res_composite_srb_ ||
        !context.active_rtv) {
      return;
    }

    const auto composite_start = ParticleDrawClock::now();
    particle_half_res_alpha_var_->Set(particle_half_res_alpha_srv_);
    auto* rtv = context.active_rtv;
    context_->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const Diligent::Viewport viewport = buildViewport(context.render_width, context.render_height);
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(context.render_width),
                           static_cast<Diligent::Uint32>(context.render_height));
    context_->SetPipelineState(particle_half_res_composite_pipeline_state_);
    context_->CommitShaderResources(particle_half_res_composite_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs draw{};
    draw.NumVertices = 3;
    draw.Flags = kHotPathDrawFlags;
    context_->Draw(draw);
    particle_pass_stats_.draw_submission_ms +=
        elapsedMilliseconds(composite_start, ParticleDrawClock::now());
  };

  if (context.allow_distortion_particles) {
    const auto distortion_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Distortion, true, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Distortion,
                            particle_pipeline_state_distortion_depth_,
                            particle_srb_distortion_depth_,
                            particle_texture_var_distortion_depth_,
                            particle_scene_color_var_distortion_depth_,
                            particle_scene_depth_var_distortion_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Distortion, distortion_depth_metrics);

    const auto distortion_no_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Distortion, false, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Distortion,
                            particle_pipeline_state_distortion_no_depth_,
                            particle_srb_distortion_no_depth_,
                            particle_texture_var_distortion_no_depth_,
                            particle_scene_color_var_distortion_no_depth_,
                            particle_scene_depth_var_distortion_no_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Distortion,
                                 distortion_no_depth_metrics);
  }

  build_additive_stream(true, prepared_stream);
  draw_prepared_particles(renderer::ParticleBlendMode::Additive,
                          particle_pipeline_state_additive_depth_,
                          particle_srb_additive_depth_,
                          particle_texture_var_additive_depth_,
                          particle_scene_color_var_additive_depth_,
                          particle_scene_depth_var_additive_depth_,
                          default_target,
                          prepared_stream);
  build_additive_stream(false, prepared_stream);
  draw_prepared_particles(renderer::ParticleBlendMode::Additive,
                          particle_pipeline_state_additive_no_depth_,
                          particle_srb_additive_no_depth_,
                          particle_texture_var_additive_no_depth_,
                          particle_scene_color_var_additive_no_depth_,
                          particle_scene_depth_var_additive_no_depth_,
                          default_target,
                          prepared_stream);

  if (use_half_res_alpha) {
    ParticleDrawTarget depth_alpha_target = half_res_alpha_target;
    depth_alpha_target.force_scene_depth_clip = true;
    const auto alpha_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Alpha, true, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_half_res_,
                            particle_srb_alpha_half_res_,
                            particle_texture_var_alpha_half_res_,
                            particle_scene_color_var_alpha_half_res_,
                            particle_scene_depth_var_alpha_half_res_,
                            depth_alpha_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Alpha, alpha_depth_metrics);

    const auto alpha_no_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Alpha, false, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_half_res_,
                            particle_srb_alpha_half_res_,
                            particle_texture_var_alpha_half_res_,
                            particle_scene_color_var_alpha_half_res_,
                            particle_scene_depth_var_alpha_half_res_,
                            half_res_alpha_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Alpha, alpha_no_depth_metrics);
    composite_half_res_alpha();
  } else {
    const auto alpha_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Alpha, true, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_depth_,
                            particle_srb_alpha_depth_,
                            particle_texture_var_alpha_depth_,
                            particle_scene_color_var_alpha_depth_,
                            particle_scene_depth_var_alpha_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Alpha, alpha_depth_metrics);

    const auto alpha_no_depth_metrics =
        build_sorted_stream(renderer::ParticleBlendMode::Alpha, false, prepared_stream);
    draw_prepared_particles(renderer::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_no_depth_,
                            particle_srb_alpha_no_depth_,
                            particle_texture_var_alpha_no_depth_,
                            particle_scene_color_var_alpha_no_depth_,
                            particle_scene_depth_var_alpha_no_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(renderer::ParticleBlendMode::Alpha, alpha_no_depth_metrics);
  }
}

}  // namespace karma::renderer_backend
