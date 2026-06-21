#include "../backend.hpp"

#include "../backend_internal.h"
#include "pass_shared.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceObject.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>

#include "karma/core.h"

namespace karma::rendering::backend {

namespace {

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
  rendering::TextureId texture = rendering::kInvalidTexture;
  rendering::ParticleAlignment alignment = rendering::ParticleAlignment::Billboard;
  rendering::ParticleShadingMode shading_mode = rendering::ParticleShadingMode::Standard;
  rendering::ParticlePresentationMode presentation_mode =
      rendering::ParticlePresentationMode::Baked;
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
  std::vector<std::size_t> batch_indices;
  std::size_t particle_count = 0u;
};

struct PreparedParticleSpan {
  ParticleBatchGroupKey key{};
  std::size_t particle_offset = 0u;
  std::size_t particle_count = 0u;
  uint32_t indirect_draw_index = 0u;
  bool indirect = false;
};

struct PreparedParticleStream {
  std::vector<ParticleInstanceGpu> particles;
  std::vector<PreparedParticleSpan> spans;
};

struct SortedParticle {
  const rendering::ParticlePackedInstance* particle = nullptr;
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

bool isTransparentParticleBlend(rendering::ParticleBlendMode blend_mode) {
  return blend_mode == rendering::ParticleBlendMode::Alpha ||
         blend_mode == rendering::ParticleBlendMode::Distortion;
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

void DiligentBackend::renderParticlePasses(rendering::LayerId layer,
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

  auto resolve_particle_texture = [&](rendering::TextureId texture_id) -> Diligent::ITextureView* {
    if (texture_id != rendering::kInvalidTexture) {
      auto it = textures_.find(texture_id);
      if (it != textures_.end() && it->second.srv) {
        return it->second.srv;
      }
    }
    return default_base_color_;
  };

  auto ensure_particle_instance_buffer = [&](std::size_t particle_count) {
    if (!particle_instance_vb_ || !particle_instance_uav_ ||
        particle_instance_capacity_ < particle_count) {
      const std::size_t new_capacity =
          std::max(particle_count,
                   particle_instance_capacity_ > 0
                       ? particle_instance_capacity_ * 2
                       : static_cast<std::size_t>(256));
      Diligent::BufferDesc vb_desc{};
      vb_desc.Name = "Karma Particle Instance VB";
      vb_desc.Usage = Diligent::USAGE_DEFAULT;
      vb_desc.BindFlags =
          Diligent::BIND_VERTEX_BUFFER | Diligent::BIND_UNORDERED_ACCESS;
      vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
      vb_desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
      vb_desc.ElementByteStride =
          static_cast<Diligent::Uint32>(sizeof(ParticleInstanceGpu));
      vb_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(ParticleInstanceGpu));
      Diligent::RefCntAutoPtr<Diligent::IBuffer> next_particle_instance_vb;
      device_->CreateBuffer(vb_desc, nullptr, &next_particle_instance_vb);
      if (!next_particle_instance_vb) {
        particle_instance_capacity_ = 0;
        return false;
      }
      particle_instance_vb_ = std::move(next_particle_instance_vb);
      particle_instance_uav_ =
          particle_instance_vb_->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
      if (!particle_instance_uav_) {
        particle_instance_vb_.Release();
        particle_instance_capacity_ = 0;
        return false;
      }
      particle_instance_capacity_ = new_capacity;
      particle_pass_stats_.gpu_buffer_resizes += 1u;
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
        batch.blend_mode == rendering::ParticleBlendMode::Alpha &&
        !batch.particles.empty()) {
      has_alpha_particles = true;
      break;
    }
  }
  if (!has_alpha_particles) {
    for (const auto& submission : particle_emitter_submissions_) {
      const auto& emitter = submission.desc;
      if (emitter.layer == layer &&
          emitter.blend_mode == rendering::ParticleBlendMode::Alpha &&
          emitter.visible &&
          emitter.enabled &&
          emitter.max_particles > 0u) {
        has_alpha_particles = true;
        break;
      }
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
    context_->UpdateBuffer(particle_instance_vb_,
                           0,
                           static_cast<Diligent::Uint32>(
                               stream.particles.size() * sizeof(ParticleInstanceGpu)),
                           stream.particles.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return true;
  };

  auto draw_particle_spans =
      [&](rendering::ParticleBlendMode blend_mode,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
          Diligent::IShaderResourceVariable* texture_var,
          Diligent::IShaderResourceVariable* scene_color_var,
          Diligent::IShaderResourceVariable* scene_depth_var,
          const ParticleDrawTarget& draw_target,
          const std::vector<PreparedParticleSpan>& spans,
          std::size_t particle_count,
          const PreparedParticleStream* cpu_stream,
          Diligent::IBuffer* indirect_args_buffer) {
    if (!pso || !particle_cb_ || !particle_vb_ || !draw_target.rtv || draw_target.width <= 0 ||
        draw_target.height <= 0 || particle_count == 0u || spans.empty()) {
      return;
    }

    const auto submission_start = core::SteadyClock::now();
    if (cpu_stream != nullptr) {
      if (!upload_prepared_particles(*cpu_stream)) {
        return;
      }
    } else if (!ensure_particle_instance_buffer(particle_count)) {
      return;
    }

    Diligent::IShaderResourceBinding* current_srb = nullptr;
    Diligent::ITextureView* current_texture = nullptr;
    Diligent::ITextureView* current_scene_color = nullptr;
    Diligent::ITextureView* current_scene_depth = nullptr;
    bool pipeline_bound = false;

    for (const auto& span : spans) {
      if (span.particle_count == 0u) {
        continue;
      }

      const auto& key = span.key;
      ParticleConstants constants{};
      copyMat4(constants.view_proj, context.view_proj);
      glm::vec3 particle_right = context.camera_right;
      glm::vec3 particle_up = context.camera_up;
      if (key.alignment == rendering::ParticleAlignment::Ground) {
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

      switch (blend_mode) {
        case rendering::ParticleBlendMode::Additive:
          particle_pass_stats_.additive_draw_calls += 1u;
          break;
        case rendering::ParticleBlendMode::Alpha:
          particle_pass_stats_.alpha_draw_calls += 1u;
          break;
        case rendering::ParticleBlendMode::Distortion:
          particle_pass_stats_.distortion_draw_calls += 1u;
          break;
      }
      if (indirect_args_buffer != nullptr && span.indirect) {
        Diligent::DrawIndirectAttribs draw{};
        draw.pAttribsBuffer = indirect_args_buffer;
        draw.DrawArgsOffset =
            static_cast<Diligent::Uint64>(span.indirect_draw_index *
                                          sizeof(ParticleGpuIndirectArgs));
        draw.DrawArgsStride = static_cast<Diligent::Uint32>(sizeof(ParticleGpuIndirectArgs));
        draw.DrawCount = 1u;
        draw.Flags = kHotPathDrawFlags;
        draw.AttribsBufferStateTransitionMode =
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context_->DrawIndirect(draw);
      } else {
        Diligent::DrawAttribs draw{};
        draw.NumVertices = static_cast<Diligent::Uint32>(kParticleQuadVertexCount);
        draw.NumInstances = static_cast<Diligent::Uint32>(span.particle_count);
        draw.Flags = kHotPathDrawFlags;
        context_->Draw(draw);
      }
    }

    particle_pass_stats_.draw_submission_ms +=
        core::elapsedMilliseconds(submission_start, core::SteadyClock::now());
  };

  auto draw_prepared_particles =
      [&](rendering::ParticleBlendMode blend_mode,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
          Diligent::IShaderResourceVariable* texture_var,
          Diligent::IShaderResourceVariable* scene_color_var,
          Diligent::IShaderResourceVariable* scene_depth_var,
          const ParticleDrawTarget& draw_target,
          const PreparedParticleStream& stream) {
    draw_particle_spans(blend_mode,
                        pso,
                        srb,
                        texture_var,
                        scene_color_var,
                        scene_depth_var,
                        draw_target,
                        stream.spans,
                        stream.particles.size(),
                        &stream,
                        nullptr);
  };

  struct GpuEmitterDraw {
    const ParticleEmitterSubmission* submission = nullptr;
    ParticleBatchGroupKey key{};
    std::size_t particle_offset = 0u;
    std::size_t particle_count = 0u;
    uint32_t alive_count = 0u;
    uint32_t spawned_this_frame = 0u;
    float depth = 0.0f;
  };

  struct PersistentGpuParticleGroup {
    ParticleBatchGroupKey key{};
    rendering::ParticleBlendMode blend_mode = rendering::ParticleBlendMode::Additive;
    bool depth_test = true;
    uint32_t group_index = 0u;
    uint32_t instance_base = 0u;
    uint32_t max_particles = 0u;
    uint32_t sort_base = 0u;
    uint32_t sort_capacity = 0u;
    bool sortable = false;
  };

  auto next_power_of_two = [](uint32_t value) {
    if (value <= 1u) {
      return 1u;
    }
    --value;
    value |= value >> 1u;
    value |= value >> 2u;
    value |= value >> 4u;
    value |= value >> 8u;
    value |= value >> 16u;
    return value + 1u;
  };

  auto make_emitter_group_key = [](const ParticleEmitterSubmission& submission) {
    const auto& emitter = submission.desc;
    return ParticleBatchGroupKey{
        .texture = emitter.texture,
        .alignment = emitter.alignment,
        .shading_mode = emitter.shading_mode,
        .presentation_mode = rendering::ParticlePresentationMode::Simulated,
        .use_soft_mask = emitter.use_soft_mask,
        .soft_particle_distance = std::max(emitter.soft_particle_distance, 0.0f),
        .distortion_strength = std::max(emitter.distortion_strength, 0.0f),
        .fresnel_power = std::max(emitter.fresnel_power, 0.001f),
        .fresnel_strength = std::max(emitter.fresnel_strength, 0.0f),
        .refraction_strength = std::max(emitter.refraction_strength, 0.0f),
        .interior_glow = std::max(emitter.interior_glow, 0.0f),
        .size_curve_exponent = std::max(emitter.size_curve_exponent, 0.001f),
        .alpha_curve_exponent = std::max(emitter.alpha_curve_exponent, 0.001f),
        .atlas_columns = std::max(emitter.atlas_columns, 1u),
        .atlas_rows = std::max(emitter.atlas_rows, 1u),
        .atlas_frame_count = std::max(emitter.atlas_frame_count, 1u),
        .animate_over_lifetime = emitter.animate_over_lifetime,
        .atlas_frame_width = emitter.atlas_frame_width,
        .atlas_frame_height = emitter.atlas_frame_height,
        .atlas_border_x = emitter.atlas_border_x,
        .atlas_border_y = emitter.atlas_border_y,
        .atlas_spacing_x = emitter.atlas_spacing_x,
        .atlas_spacing_y = emitter.atlas_spacing_y,
        .animation_fps = std::max(emitter.animation_fps, 0.0f),
    };
  };

  auto emitter_active_time = [](const ParticleEmitterSubmission& submission) {
    return std::max(submission.elapsed_seconds -
                        std::max(submission.desc.start_delay, 0.0f),
                    0.0f);
  };

  auto estimate_total_spawned = [&](const ParticleEmitterSubmission& submission,
                                    float elapsed_seconds) {
    const auto& emitter = submission.desc;
    const float active_time =
        std::max(elapsed_seconds - std::max(emitter.start_delay, 0.0f), 0.0f);
    if (!emitter.enabled || !emitter.playing || active_time <= 0.0f) {
      return 0u;
    }

    uint32_t total = emitter.emit_burst_on_start ? emitter.burst_count : 0u;
    if (emitter.spawn_rate > 0.0f) {
      const float emission_duration =
          emitter.loop || emitter.duration <= 0.0f ? active_time
                                                   : std::min(active_time, emitter.duration);
      total += static_cast<uint32_t>(
          std::floor(std::max(emission_duration, 0.0f) * emitter.spawn_rate));
    }
    return total;
  };

  auto estimate_alive_particles = [&](const ParticleEmitterSubmission& submission) {
    const auto& emitter = submission.desc;
    if (!emitter.enabled || !emitter.playing || !emitter.visible) {
      return 0u;
    }

    const uint32_t max_particles = std::max(emitter.max_particles, 1u);
    const float active_time = emitter_active_time(submission);
    if (active_time <= 0.0f && emitter.start_delay > 0.0f) {
      return 0u;
    }

    const float max_lifetime =
        std::max(std::max(emitter.particle_lifetime_min, emitter.particle_lifetime_max), 0.01f);
    uint32_t alive = 0u;
    if (emitter.emit_burst_on_start && active_time < max_lifetime) {
      alive += std::min(emitter.burst_count, max_particles);
    }

    const uint32_t burst_capacity =
        emitter.emit_burst_on_start ? std::min(emitter.burst_count, max_particles) : 0u;
    const uint32_t continuous_capacity = max_particles - burst_capacity;
    if (continuous_capacity > 0u && emitter.spawn_rate > 0.0f) {
      if (emitter.loop) {
        const uint32_t live_continuous =
            static_cast<uint32_t>(std::ceil(emitter.spawn_rate * max_lifetime));
        alive += std::min(continuous_capacity, live_continuous);
      } else {
        const float end_time =
            emitter.duration > 0.0f ? std::min(active_time, emitter.duration) : active_time;
        const float begin_time = std::max(0.0f, active_time - max_lifetime);
        if (end_time > begin_time) {
          const uint32_t live_continuous = static_cast<uint32_t>(
              std::ceil((end_time - begin_time) * emitter.spawn_rate));
          alive += std::min(continuous_capacity, live_continuous);
        }
      }
    }
    return std::min(alive, max_particles);
  };

  auto estimate_emitter_bounds_radius = [](const rendering::ParticleEmitterGpuDesc& emitter) {
    const float max_scale =
        std::max({std::abs(emitter.scale.x), std::abs(emitter.scale.y),
                  std::abs(emitter.scale.z), 1.0e-4f});
    const glm::vec3 box_extents(emitter.source_box_extents.x * max_scale,
                                emitter.source_box_extents.y * max_scale,
                                emitter.source_box_extents.z * max_scale);
    float radius = glm::length(box_extents);
    radius = std::max(radius, std::max(emitter.source_radius_min,
                                       emitter.source_radius_max) * max_scale);
    radius = std::max(radius, std::max(emitter.source_inner_radius,
                                       emitter.source_outer_radius) * max_scale);
    radius = std::max(radius, emitter.source_height * 0.5f * max_scale);
    radius = std::max(radius, emitter.source_mesh_bounds_radius * max_scale);
    for (const math::Vec3& point : emitter.source_path_points) {
      radius = std::max(radius, glm::length(glm::vec3(point.x, point.y, point.z)) * max_scale);
    }

    const glm::vec3 velocity_min(emitter.velocity_min.x,
                                 emitter.velocity_min.y,
                                 emitter.velocity_min.z);
    const glm::vec3 velocity_max(emitter.velocity_max.x,
                                 emitter.velocity_max.y,
                                 emitter.velocity_max.z);
    const glm::vec3 acceleration(emitter.acceleration.x,
                                 emitter.acceleration.y,
                                 emitter.acceleration.z);
    const float lifetime = std::max(std::max(emitter.particle_lifetime_min,
                                             emitter.particle_lifetime_max),
                                    0.01f);
    const float max_velocity =
        std::max(glm::length(velocity_min), glm::length(velocity_max)) +
        std::max(emitter.radial_speed_max, 0.0f);
    radius += max_velocity * lifetime;
    radius += 0.5f * glm::length(acceleration) * lifetime * lifetime;
    radius += std::max(std::max(emitter.start_size_min, emitter.start_size_max),
                       std::max(emitter.end_size_min, emitter.end_size_max)) *
              max_scale;
    return std::max(radius, 0.01f);
  };

  auto is_emitter_visible_to_camera = [&](const rendering::ParticleEmitterGpuDesc& emitter) {
    if (!emitter.visible) {
      return false;
    }
    if (!camera_active_) {
      return true;
    }

    const glm::vec3 center(emitter.position.x, emitter.position.y, emitter.position.z);
    const glm::vec3 to_center = center - camera_.position;
    const float radius = estimate_emitter_bounds_radius(emitter);
    const float depth = glm::dot(to_center, context.camera_forward);
    const float near_clip = std::max(camera_.near_clip, 0.001f);
    const float far_clip = std::max(camera_.far_clip, near_clip + 0.001f);
    if (depth < near_clip - radius || depth > far_clip + radius) {
      return false;
    }

    if (!camera_.perspective) {
      const float horizontal = glm::dot(to_center, context.camera_right);
      const float vertical = glm::dot(to_center, context.camera_up);
      const float left = std::min(camera_.ortho_left, camera_.ortho_right) - radius;
      const float right = std::max(camera_.ortho_left, camera_.ortho_right) + radius;
      const float bottom = std::min(camera_.ortho_bottom, camera_.ortho_top) - radius;
      const float top = std::max(camera_.ortho_bottom, camera_.ortho_top) + radius;
      return horizontal >= left && horizontal <= right &&
             vertical >= bottom && vertical <= top;
    }

    const float tan_half_fov_y =
        std::tan(std::clamp(camera_.fov_y_degrees, 1.0f, 179.0f) * 0.008726646259971648f);
    const float aspect = std::max(camera_.aspect, 1.0e-4f);
    const float horizontal = glm::dot(to_center, context.camera_right);
    const float vertical = glm::dot(to_center, context.camera_up);
    const float abs_depth = std::max(depth, 0.0f);
    const float vertical_limit = abs_depth * tan_half_fov_y + radius;
    const float horizontal_limit = abs_depth * tan_half_fov_y * aspect + radius;
    return std::abs(vertical) <= vertical_limit &&
           std::abs(horizontal) <= horizontal_limit;
  };

  auto fill_source_data = [](auto& constants, const rendering::ParticleEmitterGpuDesc& emitter) {
    constants.spawn_box[0] = emitter.source_box_extents.x;
    constants.spawn_box[1] = emitter.source_box_extents.y;
    constants.spawn_box[2] = emitter.source_box_extents.z;
    constants.spawn_box[3] = static_cast<float>(static_cast<uint32_t>(emitter.source_shape));
    constants.spawn_sphere[0] = emitter.source_radius_min;
    constants.spawn_sphere[1] = emitter.source_radius_max;
    constants.spawn_sphere[2] = emitter.radial_speed_min;
    constants.spawn_sphere[3] = emitter.radial_speed_max;
    constants.source_params0[0] = emitter.source_inner_radius;
    constants.source_params0[1] = emitter.source_outer_radius;
    constants.source_params0[2] = emitter.source_angle;
    constants.source_params0[3] = emitter.source_jitter_radius;
    constants.source_params1[0] = emitter.source_height;
    constants.source_params1[1] = emitter.source_dimensions.x;
    constants.source_params1[2] = emitter.source_dimensions.y;
    constants.source_params1[3] = emitter.source_closed_loop ? 1.0f : 0.0f;
    constants.source_params2[0] = static_cast<float>(
        std::min<std::size_t>(emitter.source_path_points.size(), 8u));
    constants.source_params2[1] =
        static_cast<float>(static_cast<uint32_t>(emitter.source_sampling));
    constants.source_params2[2] =
        static_cast<float>(static_cast<uint32_t>(emitter.source_distribution));
    constants.source_params2[3] = emitter.source_dimensions.z;
    constants.source_mesh[0] = emitter.source_mesh_bounds_center.x;
    constants.source_mesh[1] = emitter.source_mesh_bounds_center.y;
    constants.source_mesh[2] = emitter.source_mesh_bounds_center.z;
    constants.source_mesh[3] = emitter.source_mesh_bounds_radius;

    float* paths[] = {
        constants.source_path0,
        constants.source_path1,
        constants.source_path2,
        constants.source_path3,
        constants.source_path4,
        constants.source_path5,
        constants.source_path6,
        constants.source_path7,
    };
    for (std::size_t i = 0u; i < 8u; ++i) {
      paths[i][0] = 0.0f;
      paths[i][1] = 0.0f;
      paths[i][2] = 0.0f;
      paths[i][3] = 0.0f;
      if (i < emitter.source_path_points.size()) {
        const math::Vec3& point = emitter.source_path_points[i];
        paths[i][0] = point.x;
        paths[i][1] = point.y;
        paths[i][2] = point.z;
        paths[i][3] = 1.0f;
      }
    }
  };

  auto fill_sim_constants = [&](const GpuEmitterDraw& draw,
                                ParticleSimComputeConstants& constants) {
    const auto& emitter = draw.submission->desc;
    constants.position_time[0] = emitter.position.x;
    constants.position_time[1] = emitter.position.y;
    constants.position_time[2] = emitter.position.z;
    constants.position_time[3] = draw.submission->elapsed_seconds;
    constants.rotation[0] = emitter.rotation.x;
    constants.rotation[1] = emitter.rotation.y;
    constants.rotation[2] = emitter.rotation.z;
    constants.rotation[3] = emitter.rotation.w;
    constants.scale_seed[0] = emitter.scale.x;
    constants.scale_seed[1] = emitter.scale.y;
    constants.scale_seed[2] = emitter.scale.z;
    const uint32_t fallback_seed = static_cast<uint32_t>(
        (emitter.instance_id >> 32u) ^ (emitter.instance_id & 0xFFFFFFFFu) ^ 0x9E3779B9u);
    constants.scale_seed[3] =
        static_cast<float>(emitter.seed != 0u ? emitter.seed : std::max(fallback_seed, 1u));
    constants.playback[0] = draw.submission->elapsed_seconds;
    constants.playback[1] = draw.submission->previous_elapsed_seconds;
    constants.playback[2] = std::max(emitter.start_delay, 0.0f);
    constants.playback[3] = std::max(emitter.duration, 0.0f);
    uint32_t flags = 0u;
    flags |= emitter.emit_burst_on_start ? 1u : 0u;
    flags |= emitter.loop ? 2u : 0u;
    flags |= emitter.local_space ? 4u : 0u;
    flags |= (!emitter.local_space && emitter.collide_with_ground) ? 8u : 0u;
    flags |= emitter.enabled ? 16u : 0u;
    flags |= emitter.playing ? 32u : 0u;
    flags |= emitter.visible ? 64u : 0u;
    constants.emission[0] = static_cast<float>(std::max(emitter.max_particles, 1u));
    constants.emission[1] = static_cast<float>(emitter.burst_count);
    constants.emission[2] = std::max(emitter.spawn_rate, 0.0f);
    constants.emission[3] = static_cast<float>(flags);
    constants.lifetime[0] = emitter.particle_lifetime_min;
    constants.lifetime[1] = emitter.particle_lifetime_max;
    constants.lifetime[2] = 0.0f;
    constants.lifetime[3] = 0.0f;
    constants.size[0] = emitter.start_size_min;
    constants.size[1] = emitter.start_size_max;
    constants.size[2] = emitter.end_size_min;
    constants.size[3] = emitter.end_size_max;
    constants.rotation_params[0] = emitter.initial_rotation_min;
    constants.rotation_params[1] = emitter.initial_rotation_max;
    constants.rotation_params[2] = emitter.angular_velocity_min;
    constants.rotation_params[3] = emitter.angular_velocity_max;
    fill_source_data(constants, emitter);
    constants.velocity_min[0] = emitter.velocity_min.x;
    constants.velocity_min[1] = emitter.velocity_min.y;
    constants.velocity_min[2] = emitter.velocity_min.z;
    constants.velocity_min[3] = 0.0f;
    constants.velocity_max[0] = emitter.velocity_max.x;
    constants.velocity_max[1] = emitter.velocity_max.y;
    constants.velocity_max[2] = emitter.velocity_max.z;
    constants.velocity_max[3] = 0.0f;
    constants.acceleration_drag[0] = emitter.acceleration.x;
    constants.acceleration_drag[1] = emitter.acceleration.y;
    constants.acceleration_drag[2] = emitter.acceleration.z;
    constants.acceleration_drag[3] = std::max(emitter.drag, 0.0f);
    constants.collision[0] = emitter.ground_height;
    constants.collision[1] = std::clamp(emitter.bounce_damping, 0.0f, 1.0f);
    constants.collision[2] = std::clamp(emitter.collision_friction, 0.0f, 1.0f);
    constants.collision[3] = std::max(emitter.rest_speed_threshold, 0.0f);
    constants.color_start[0] = emitter.start_color.r;
    constants.color_start[1] = emitter.start_color.g;
    constants.color_start[2] = emitter.start_color.b;
    constants.color_start[3] = emitter.start_color.a;
    constants.color_end[0] = emitter.end_color.r;
    constants.color_end[1] = emitter.end_color.g;
    constants.color_end[2] = emitter.end_color.b;
    constants.color_end[3] = emitter.end_color.a;
    constants.output[0] = static_cast<float>(draw.particle_offset);
    constants.output[1] = static_cast<float>(draw.particle_count);
    constants.output[2] = 0.0f;
    constants.output[3] = 0.0f;
  };

  auto record_gpu_draw_stats = [&](rendering::ParticleBlendMode blend_mode,
                                   uint32_t alive_count,
                                   uint32_t capacity,
                                   uint32_t spawned_this_frame) {
    particle_pass_stats_.submitted_batches += 1u;
    particle_pass_stats_.submitted_particles += alive_count;
    particle_pass_stats_.simulated_particles += alive_count;
    particle_pass_stats_.packed_particles += alive_count;
    particle_pass_stats_.culled_particles += capacity > alive_count ? capacity - alive_count : 0u;
    particle_pass_stats_.gpu_particle_capacity += capacity;
    particle_pass_stats_.gpu_alive_particles += alive_count;
    particle_pass_stats_.gpu_spawned_particles += spawned_this_frame;
    particle_pass_stats_.gpu_killed_particles += capacity > alive_count ? capacity - alive_count : 0u;
    particle_pass_stats_.gpu_compacted_particles += alive_count;
    switch (blend_mode) {
      case rendering::ParticleBlendMode::Additive:
        particle_pass_stats_.additive_batches += 1u;
        particle_pass_stats_.additive_particles += alive_count;
        break;
      case rendering::ParticleBlendMode::Alpha:
        particle_pass_stats_.alpha_batches += 1u;
        particle_pass_stats_.alpha_particles += alive_count;
        particle_pass_stats_.alpha_sorted_particles += alive_count;
        break;
      case rendering::ParticleBlendMode::Distortion:
        particle_pass_stats_.distortion_batches += 1u;
        particle_pass_stats_.distortion_particles += alive_count;
        particle_pass_stats_.distortion_sorted_particles += alive_count;
        particle_pass_stats_.distortion_present = true;
        break;
    }
  };

  thread_local std::vector<PersistentGpuParticleGroup> persistent_gpu_groups;
  thread_local std::vector<ParticleGpuEmitterDesc> persistent_gpu_emitters;
  thread_local std::vector<ParticleGpuMeshSample> persistent_gpu_mesh_samples;
  thread_local std::vector<uint64_t> persistent_gpu_instance_ids;
  thread_local std::vector<ParticleGpuMaterialGroup> persistent_gpu_material_groups;
  thread_local std::vector<ParticleGpuMaterialRecord> persistent_gpu_material_records;
  thread_local std::unordered_map<ParticleBatchGroupKey,
                                  uint32_t,
                                  ParticleBatchGroupKeyHash>
      persistent_gpu_material_lookup;
  thread_local std::unordered_map<rendering::TextureId, uint32_t>
      persistent_gpu_texture_lookup;
  thread_local std::array<Diligent::IDeviceObject*, kParticleGpuTextureTableSize>
      persistent_gpu_texture_table;
  thread_local std::vector<PreparedParticleSpan> persistent_gpu_spans;
  thread_local std::vector<uint64_t> active_gpu_emitter_ids;
  bool persistent_gpu_ready = false;
  std::size_t persistent_gpu_instance_capacity = 0u;
  bool persistent_gpu_global_sort_active = false;

  auto draw_global_particle_spans =
      [&](rendering::ParticleBlendMode blend_mode,
          ParticleGlobalPipeline& pipeline,
          const ParticleDrawTarget& draw_target,
          const std::vector<PreparedParticleSpan>& spans,
          std::size_t particle_count,
          Diligent::IBuffer* indirect_args_buffer) {
    if (!pipeline.pso || !pipeline.srb || !particle_cb_ || !particle_vb_ ||
        !particle_gpu_material_record_srv_ || !draw_target.rtv || draw_target.width <= 0 ||
        draw_target.height <= 0 || particle_count == 0u || spans.empty()) {
      return;
    }
    if (!ensure_particle_instance_buffer(particle_count)) {
      return;
    }

    const auto submission_start = core::SteadyClock::now();
    ParticleConstants constants{};
    copyMat4(constants.view_proj, context.view_proj);
    constants.camera_right[0] = context.camera_right.x;
    constants.camera_right[1] = context.camera_right.y;
    constants.camera_right[2] = context.camera_right.z;
    constants.camera_right[3] = 0.0f;
    constants.camera_up[0] = context.camera_up.x;
    constants.camera_up[1] = context.camera_up.y;
    constants.camera_up[2] = context.camera_up.z;
    constants.camera_up[3] = 0.0f;
    constants.camera_forward[0] = context.camera_forward.x;
    constants.camera_forward[1] = context.camera_forward.y;
    constants.camera_forward[2] = context.camera_forward.z;
    constants.camera_forward[3] = 0.0f;
    constants.params[1] = static_cast<float>(static_cast<std::uint32_t>(blend_mode));
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
    constants.camera_position[0] = camera_.position.x;
    constants.camera_position[1] = camera_.position.y;
    constants.camera_position[2] = camera_.position.z;
    constants.camera_position[3] = 1.0f;
    constants.presentation_params[3] = draw_target.force_scene_depth_clip ? 1.0f : 0.0f;
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

    auto* rtv = draw_target.rtv;
    auto* dsv = draw_target.dsv;
    const Diligent::Viewport viewport = buildViewport(draw_target.width, draw_target.height);
    context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(draw_target.width),
                           static_cast<Diligent::Uint32>(draw_target.height));
    context_->SetPipelineState(pipeline.pso);

    pipeline.materials_vs_var->Set(particle_gpu_material_record_srv_,
                                   Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    pipeline.materials_ps_var->Set(particle_gpu_material_record_srv_,
                                   Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    pipeline.textures_var->SetArray(persistent_gpu_texture_table.data(),
                                    0,
                                    static_cast<Diligent::Uint32>(
                                        persistent_gpu_texture_table.size()),
                                    Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    Diligent::ITextureView* desired_scene_color =
        context.particle_scene_color_sample_srv ? context.particle_scene_color_sample_srv
                                                : default_base_color_;
    if (pipeline.scene_color_var && desired_scene_color) {
      pipeline.scene_color_var->Set(desired_scene_color,
                                    Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    Diligent::ITextureView* desired_scene_depth =
        context.particle_scene_depth_srv ? context.particle_scene_depth_srv
                                         : particle_fallback_depth_srv_;
    if (pipeline.scene_depth_var && desired_scene_depth) {
      pipeline.scene_depth_var->Set(desired_scene_depth,
                                    Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
    context_->CommitShaderResources(pipeline.srb,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    for (const auto& span : spans) {
      if (span.particle_count == 0u) {
        continue;
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

      if (blend_mode == rendering::ParticleBlendMode::Alpha) {
        particle_pass_stats_.alpha_draw_calls += 1u;
      } else if (blend_mode == rendering::ParticleBlendMode::Distortion) {
        particle_pass_stats_.distortion_draw_calls += 1u;
      }

      if (indirect_args_buffer != nullptr && span.indirect) {
        Diligent::DrawIndirectAttribs draw{};
        draw.pAttribsBuffer = indirect_args_buffer;
        draw.DrawArgsOffset =
            static_cast<Diligent::Uint64>(span.indirect_draw_index *
                                          sizeof(ParticleGpuIndirectArgs));
        draw.DrawArgsStride = static_cast<Diligent::Uint32>(sizeof(ParticleGpuIndirectArgs));
        draw.DrawCount = 1u;
        draw.Flags = kHotPathDrawFlags;
        draw.AttribsBufferStateTransitionMode =
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context_->DrawIndirect(draw);
      }
    }

    particle_pass_stats_.draw_submission_ms +=
        core::elapsedMilliseconds(submission_start, core::SteadyClock::now());
  };

  auto apply_particle_gpu_readback = [&] {
    if (particle_gpu_stats_readback_valid_) {
      const uint32_t read_index = particle_gpu_stats_readback_frame_ ^ 1u;
      auto& readback = particle_gpu_stats_readback_buffers_[read_index];
      if (readback) {
        Diligent::MapHelper<ParticleGpuStatsReadback> read_map(
            context_,
            readback,
            Diligent::MAP_READ,
            Diligent::MAP_FLAG_DO_NOT_WAIT);
        if (auto* data = getMappedData(read_map)) {
          particle_gpu_last_stats_ = *data;
          particle_gpu_stats_readback_age_ = 1u;
        } else if (particle_gpu_stats_readback_age_ < std::numeric_limits<uint32_t>::max()) {
          particle_gpu_stats_readback_age_ += 1u;
        }
      }
    }

    if (particle_gpu_stats_readback_age_ == 0u) {
      return;
    }
    particle_pass_stats_.gpu_particle_capacity += particle_gpu_last_stats_.particle_capacity;
    particle_pass_stats_.gpu_alive_particles += particle_gpu_last_stats_.alive_particles;
    particle_pass_stats_.gpu_dead_particles += particle_gpu_last_stats_.dead_particles;
    particle_pass_stats_.gpu_spawned_particles += particle_gpu_last_stats_.spawned_particles;
    particle_pass_stats_.gpu_killed_particles += particle_gpu_last_stats_.killed_particles;
    particle_pass_stats_.gpu_compacted_particles += particle_gpu_last_stats_.compacted_particles;
    particle_pass_stats_.gpu_indirect_draws += particle_gpu_last_stats_.indirect_draws;
    particle_pass_stats_.gpu_indirect_dispatches +=
        particle_gpu_last_stats_.indirect_dispatches;
    particle_pass_stats_.gpu_sort_key_count += particle_gpu_last_stats_.sort_key_count;
    particle_pass_stats_.gpu_culled_particles += particle_gpu_last_stats_.culled_particles;
    particle_pass_stats_.gpu_culling_dispatches +=
        particle_gpu_last_stats_.culling_dispatches;
    particle_pass_stats_.culled_particles += particle_gpu_last_stats_.culled_particles;
    particle_pass_stats_.gpu_stats_readback_age = particle_gpu_stats_readback_age_;
    particle_pass_stats_.gpu_sort_overflow =
        particle_pass_stats_.gpu_sort_overflow ||
        particle_gpu_last_stats_.sort_overflow != 0u;
    particle_pass_stats_.gpu_fallback_active =
        particle_pass_stats_.gpu_fallback_active ||
        particle_gpu_last_stats_.fallback_active != 0u;
    particle_pass_stats_.gpu_global_sort_active =
        particle_pass_stats_.gpu_global_sort_active ||
        particle_gpu_last_stats_.global_sort_active != 0u;
    particle_pass_stats_.gpu_grouped_sort_fallback =
        particle_pass_stats_.gpu_grouped_sort_fallback ||
        particle_gpu_last_stats_.grouped_sort_fallback != 0u;
  };

  auto ensure_particle_gpu_structured_buffer =
      [&](const char* name,
          std::size_t required_count,
          std::size_t stride,
          Diligent::BIND_FLAGS bind_flags,
          Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
          Diligent::RefCntAutoPtr<Diligent::IBufferView>* srv,
          Diligent::RefCntAutoPtr<Diligent::IBufferView>* uav,
          std::size_t& capacity,
          bool preserve_existing) {
    required_count = std::max<std::size_t>(required_count, 1u);
    if (buffer && capacity >= required_count) {
      return true;
    }

    const std::size_t next_capacity =
        std::max(required_count, capacity > 0u ? capacity * 2u : required_count);
    const Diligent::Uint64 byte_size =
        static_cast<Diligent::Uint64>(next_capacity * stride);
    Diligent::BufferDesc desc{};
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = static_cast<Diligent::Uint32>(stride);
    desc.Size = byte_size;

    std::vector<uint8_t> zero_data(static_cast<std::size_t>(byte_size), 0u);
    Diligent::BufferData initial_data{zero_data.data(), byte_size};
    Diligent::RefCntAutoPtr<Diligent::IBuffer> next_buffer;
    device_->CreateBuffer(desc, &initial_data, &next_buffer);
    if (!next_buffer) {
      particle_pass_stats_.gpu_allocator_allocation_failures += 1u;
      return false;
    }

    if (preserve_existing && buffer && capacity > 0u) {
      const Diligent::Uint64 copy_size =
          static_cast<Diligent::Uint64>(std::min(capacity, next_capacity) * stride);
      context_->CopyBuffer(buffer,
                           0,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                           next_buffer,
                           0,
                           copy_size,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    buffer = std::move(next_buffer);
    capacity = next_capacity;
    if (srv != nullptr) {
      if ((bind_flags & Diligent::BIND_SHADER_RESOURCE) != 0) {
        *srv = buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
      } else {
        srv->Release();
      }
    }
    if (uav != nullptr) {
      if ((bind_flags & Diligent::BIND_UNORDERED_ACCESS) != 0) {
        *uav = buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
      } else {
        uav->Release();
      }
    }
    particle_pass_stats_.gpu_buffer_resizes += 1u;
    return true;
  };

  auto ensure_particle_gpu_readback_buffers = [&] {
    for (auto& readback : particle_gpu_stats_readback_buffers_) {
      if (readback) {
        continue;
      }
      Diligent::BufferDesc desc{};
      desc.Name = "Karma Particle GPU Stats Readback";
      desc.Usage = Diligent::USAGE_STAGING;
      desc.BindFlags = Diligent::BIND_NONE;
      desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
      desc.Size = sizeof(ParticleGpuStatsReadback);
      device_->CreateBuffer(desc, nullptr, &readback);
      if (!readback) {
        particle_pass_stats_.gpu_allocator_allocation_failures += 1u;
        return false;
      }
    }
    return true;
  };

  auto coalesce_particle_gpu_free_slots = [&] {
    if (particle_gpu_free_particle_slots_.empty()) {
      return;
    }
    std::sort(particle_gpu_free_particle_slots_.begin(),
              particle_gpu_free_particle_slots_.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.offset < rhs.offset;
              });

    std::size_t write_index = 0u;
    for (const auto& slot : particle_gpu_free_particle_slots_) {
      if (slot.capacity == 0u) {
        continue;
      }
      if (write_index > 0u) {
        auto& previous = particle_gpu_free_particle_slots_[write_index - 1u];
        const uint64_t previous_end =
            static_cast<uint64_t>(previous.offset) + previous.capacity;
        if (previous_end >= slot.offset) {
          const uint64_t slot_end = static_cast<uint64_t>(slot.offset) + slot.capacity;
          previous.capacity = static_cast<uint32_t>(
              std::min<uint64_t>(std::max(previous_end, slot_end) - previous.offset,
                                 std::numeric_limits<uint32_t>::max()));
          continue;
        }
      }
      particle_gpu_free_particle_slots_[write_index++] = slot;
    }
    particle_gpu_free_particle_slots_.resize(write_index);

    while (!particle_gpu_free_particle_slots_.empty()) {
      const auto& trailing = particle_gpu_free_particle_slots_.back();
      const uint64_t trailing_end =
          static_cast<uint64_t>(trailing.offset) + trailing.capacity;
      if (trailing_end != particle_gpu_allocated_capacity_) {
        break;
      }
      particle_gpu_allocated_capacity_ = trailing.offset;
      particle_gpu_free_particle_slots_.pop_back();
    }
  };

  auto release_particle_gpu_slot = [&](uint32_t offset, uint32_t capacity) {
    if (capacity == 0u) {
      return;
    }
    particle_gpu_free_particle_slots_.push_back(ParticleGpuSlotRange{
        .offset = offset,
        .capacity = capacity,
    });
    coalesce_particle_gpu_free_slots();
  };

  auto allocate_particle_gpu_slot = [&](uint32_t required_capacity,
                                        uint32_t& out_offset) {
    required_capacity = std::max(required_capacity, 1u);
    coalesce_particle_gpu_free_slots();
    std::size_t best_index = particle_gpu_free_particle_slots_.size();
    uint32_t best_capacity = std::numeric_limits<uint32_t>::max();
    for (std::size_t i = 0u; i < particle_gpu_free_particle_slots_.size(); ++i) {
      const auto& slot = particle_gpu_free_particle_slots_[i];
      if (slot.capacity >= required_capacity && slot.capacity < best_capacity) {
        best_index = i;
        best_capacity = slot.capacity;
      }
    }

    if (best_index < particle_gpu_free_particle_slots_.size()) {
      auto& slot = particle_gpu_free_particle_slots_[best_index];
      out_offset = slot.offset;
      particle_pass_stats_.gpu_allocator_reused_slots += 1u;
      if (slot.capacity == required_capacity) {
        particle_gpu_free_particle_slots_.erase(
            particle_gpu_free_particle_slots_.begin() +
            static_cast<std::ptrdiff_t>(best_index));
      } else {
        slot.offset += required_capacity;
        slot.capacity -= required_capacity;
      }
      return true;
    }

    if (particle_gpu_allocated_capacity_ >
        static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()) - required_capacity) {
      particle_pass_stats_.gpu_allocator_allocation_failures += 1u;
      return false;
    }
    out_offset = static_cast<uint32_t>(particle_gpu_allocated_capacity_);
    particle_gpu_allocated_capacity_ += required_capacity;
    particle_gpu_high_water_capacity_ =
        std::max(particle_gpu_high_water_capacity_, particle_gpu_allocated_capacity_);
    return true;
  };

  auto allocate_particle_gpu_emitter_state_slot = [&](uint32_t& out_index) {
    if (!particle_gpu_free_emitter_state_slots_.empty()) {
      out_index = particle_gpu_free_emitter_state_slots_.back();
      particle_gpu_free_emitter_state_slots_.pop_back();
      particle_pass_stats_.gpu_allocator_reused_slots += 1u;
      return true;
    }
    if (particle_gpu_emitter_state_allocated_capacity_ >=
        static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
      particle_pass_stats_.gpu_allocator_allocation_failures += 1u;
      return false;
    }
    out_index = static_cast<uint32_t>(particle_gpu_emitter_state_allocated_capacity_++);
    return true;
  };

  auto retire_stale_particle_gpu_emitters = [&] {
    active_gpu_emitter_ids.clear();
    active_gpu_emitter_ids.reserve(particle_emitter_submissions_.size());
    for (const auto& submission : particle_emitter_submissions_) {
      if (submission.desc.instance_id != 0u && submission.desc.max_particles > 0u) {
        active_gpu_emitter_ids.push_back(submission.desc.instance_id);
      }
    }
    std::sort(active_gpu_emitter_ids.begin(), active_gpu_emitter_ids.end());
    active_gpu_emitter_ids.erase(
        std::unique(active_gpu_emitter_ids.begin(), active_gpu_emitter_ids.end()),
        active_gpu_emitter_ids.end());

    for (auto it = particle_emitter_runtime_states_.begin();
         it != particle_emitter_runtime_states_.end();) {
      if (std::binary_search(active_gpu_emitter_ids.begin(),
                             active_gpu_emitter_ids.end(),
                             it->first)) {
        ++it;
        continue;
      }

      const ParticleEmitterRuntimeState state = it->second;
      if (state.gpu_slot_capacity > 0u) {
        release_particle_gpu_slot(state.gpu_slot_offset, state.gpu_slot_capacity);
      }
      if (state.gpu_emitter_state_allocated) {
        particle_gpu_free_emitter_state_slots_.push_back(state.gpu_emitter_state_index);
      }
      particle_pass_stats_.gpu_allocator_retired_emitters += 1u;
      it = particle_emitter_runtime_states_.erase(it);
    }
  };

  auto to_stats_u32 = [](std::size_t value) {
    return static_cast<uint32_t>(
        std::min<std::size_t>(value,
                              static_cast<std::size_t>(
                                  std::numeric_limits<uint32_t>::max())));
  };

  auto record_particle_gpu_allocator_stats = [&] {
    particle_gpu_high_water_capacity_ =
        std::max(particle_gpu_high_water_capacity_, particle_gpu_allocated_capacity_);
    std::size_t active_capacity = 0u;
    for (const auto& entry : particle_emitter_runtime_states_) {
      active_capacity += entry.second.gpu_slot_capacity;
    }
    particle_pass_stats_.gpu_allocator_live_emitters =
        to_stats_u32(particle_emitter_runtime_states_.size());
    particle_pass_stats_.gpu_allocator_free_ranges =
        to_stats_u32(particle_gpu_free_particle_slots_.size());
    particle_pass_stats_.gpu_allocator_active_capacity =
        to_stats_u32(active_capacity);
    particle_pass_stats_.gpu_allocator_high_water_capacity =
        to_stats_u32(particle_gpu_high_water_capacity_);
  };

  auto run_persistent_gpu_particles = [&] {
    apply_particle_gpu_readback();
    retire_stale_particle_gpu_emitters();
    record_particle_gpu_allocator_stats();
    persistent_gpu_groups.clear();
    persistent_gpu_emitters.clear();
    persistent_gpu_mesh_samples.clear();
    persistent_gpu_instance_ids.clear();
    persistent_gpu_material_groups.clear();
    persistent_gpu_material_records.clear();
    persistent_gpu_material_lookup.clear();
    persistent_gpu_texture_lookup.clear();
    persistent_gpu_texture_table.fill(nullptr);
    persistent_gpu_global_sort_active = false;

    if (!device_ || !context_) {
      return false;
    }
    const auto& draw_caps = device_->GetAdapterInfo().DrawCommand;
    if ((draw_caps.CapFlags & Diligent::DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT) == 0) {
      particle_pass_stats_.gpu_fallback_active = true;
      return false;
    }
    if (!particle_gpu_clear_compute_pso_ ||
        !particle_gpu_update_emitters_pso_ ||
        !particle_gpu_simulate_pso_ ||
        !particle_gpu_prepare_unsorted_pso_ ||
        !particle_gpu_generate_sort_pso_ ||
        !particle_gpu_sort_pso_ ||
        !particle_gpu_prepare_sorted_pso_ ||
        !particle_gpu_indirect_args_pso_ ||
        !particle_gpu_frame_cb_ ||
        !particle_gpu_sort_cb_) {
      particle_pass_stats_.gpu_fallback_active = true;
      return false;
    }

    auto global_pipeline_ready = [](const ParticleGlobalPipeline& pipeline) {
      return pipeline.pso && pipeline.srb &&
             pipeline.materials_vs_var != nullptr &&
             pipeline.materials_ps_var != nullptr &&
             pipeline.textures_var != nullptr &&
             pipeline.scene_color_var != nullptr &&
             pipeline.scene_depth_var != nullptr;
    };
    bool has_transparent_gpu_emitters = false;
    bool global_sort_possible =
        device_->GetDeviceInfo().IsVulkanDevice() &&
        device_->GetDeviceInfo().Features.ShaderResourceRuntimeArray !=
            Diligent::DEVICE_FEATURE_STATE_DISABLED &&
        default_base_color_ != nullptr &&
        global_pipeline_ready(particle_global_alpha_depth_) &&
        global_pipeline_ready(particle_global_alpha_no_depth_) &&
        global_pipeline_ready(particle_global_alpha_half_res_) &&
        global_pipeline_ready(particle_global_distortion_depth_) &&
        global_pipeline_ready(particle_global_distortion_no_depth_);

    auto add_texture_slot = [&](rendering::TextureId texture_id, uint32_t& out_index) {
      Diligent::ITextureView* texture_view = resolve_particle_texture(texture_id);
      if (!texture_view) {
        return false;
      }
      if (texture_id == rendering::kInvalidTexture) {
        out_index = 0u;
        return true;
      }
      if (auto it = persistent_gpu_texture_lookup.find(texture_id);
          it != persistent_gpu_texture_lookup.end()) {
        out_index = it->second;
        return true;
      }
      if (persistent_gpu_texture_lookup.size() >= kParticleGpuTextureTableSize) {
        return false;
      }
      out_index = static_cast<uint32_t>(persistent_gpu_texture_lookup.size());
      persistent_gpu_texture_lookup.emplace(texture_id, out_index);
      persistent_gpu_texture_table[out_index] =
          static_cast<Diligent::IDeviceObject*>(texture_view);
      return true;
    };

    auto add_material_record = [&](const ParticleBatchGroupKey& key,
                                   uint32_t& out_material_id) {
      if (auto it = persistent_gpu_material_lookup.find(key);
          it != persistent_gpu_material_lookup.end()) {
        out_material_id = it->second;
        return true;
      }
      if (persistent_gpu_material_records.size() >=
          static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
      }
      uint32_t texture_index = 0u;
      if (!add_texture_slot(key.texture, texture_index)) {
        return false;
      }
      ParticleGpuMaterialRecord record{};
      record.texture_index = texture_index;
      record.alignment = static_cast<uint32_t>(key.alignment);
      record.shading_mode = static_cast<uint32_t>(key.shading_mode);
      record.presentation_mode = static_cast<uint32_t>(key.presentation_mode);
      record.soft_particle_distance = std::max(key.soft_particle_distance, 0.0f);
      record.distortion_strength = std::max(key.distortion_strength, 0.0f);
      record.fresnel_power = std::max(key.fresnel_power, 0.001f);
      record.fresnel_strength = std::max(key.fresnel_strength, 0.0f);
      record.refraction_strength = std::max(key.refraction_strength, 0.0f);
      record.interior_glow = std::max(key.interior_glow, 0.0f);
      record.size_curve_exponent = std::max(key.size_curve_exponent, 0.001f);
      record.alpha_curve_exponent = std::max(key.alpha_curve_exponent, 0.001f);
      record.atlas_columns = std::max(key.atlas_columns, 1u);
      record.atlas_rows = std::max(key.atlas_rows, 1u);
      record.atlas_frame_count = std::max(key.atlas_frame_count, 1u);
      record.animate_over_lifetime = key.animate_over_lifetime ? 1u : 0u;
      record.atlas_frame_width = key.atlas_frame_width;
      record.atlas_frame_height = key.atlas_frame_height;
      record.atlas_border_x = key.atlas_border_x;
      record.atlas_border_y = key.atlas_border_y;
      record.atlas_spacing_x = static_cast<float>(key.atlas_spacing_x);
      record.atlas_spacing_y = static_cast<float>(key.atlas_spacing_y);
      record.animation_fps = std::max(key.animation_fps, 0.0f);
      record.use_soft_mask = key.use_soft_mask ? 1.0f : 0.0f;
      out_material_id = static_cast<uint32_t>(persistent_gpu_material_records.size());
      persistent_gpu_material_lookup.emplace(key, out_material_id);
      persistent_gpu_material_records.push_back(record);
      return true;
    };

    if (global_sort_possible) {
      persistent_gpu_texture_lookup.emplace(rendering::kInvalidTexture, 0u);
      persistent_gpu_texture_table[0] =
          static_cast<Diligent::IDeviceObject*>(default_base_color_.RawPtr());
      for (const auto& submission : particle_emitter_submissions_) {
        const auto& emitter = submission.desc;
        if (emitter.layer != layer ||
            emitter.max_particles == 0u ||
            !isTransparentParticleBlend(emitter.blend_mode)) {
          continue;
        }
        has_transparent_gpu_emitters = true;
        const ParticleBatchGroupKey key = make_emitter_group_key(submission);
        uint32_t material_id = 0u;
        if (!add_material_record(key, material_id)) {
          global_sort_possible = false;
          persistent_gpu_material_records.clear();
          persistent_gpu_material_lookup.clear();
          persistent_gpu_texture_lookup.clear();
          persistent_gpu_texture_table.fill(nullptr);
          break;
        }
      }
    } else {
      for (const auto& submission : particle_emitter_submissions_) {
        const auto& emitter = submission.desc;
        if (emitter.layer == layer &&
            emitter.max_particles > 0u &&
            isTransparentParticleBlend(emitter.blend_mode)) {
          has_transparent_gpu_emitters = true;
          break;
        }
      }
    }
    persistent_gpu_global_sort_active =
        global_sort_possible && has_transparent_gpu_emitters &&
        !persistent_gpu_material_records.empty();
    if (persistent_gpu_global_sort_active) {
      for (auto& texture : persistent_gpu_texture_table) {
        if (texture == nullptr) {
          texture = persistent_gpu_texture_table[0];
        }
      }
    }
    if (has_transparent_gpu_emitters) {
      if (persistent_gpu_global_sort_active) {
        particle_pass_stats_.gpu_global_sort_active = true;
      } else {
        particle_pass_stats_.gpu_grouped_sort_fallback = true;
      }
    }

    auto find_or_add_group = [&](const ParticleBatchGroupKey& key,
                                 rendering::ParticleBlendMode blend_mode,
                                 bool depth_test) {
      const bool global_sort_group =
          persistent_gpu_global_sort_active && isTransparentParticleBlend(blend_mode);
      for (std::size_t i = 0; i < persistent_gpu_groups.size(); ++i) {
        const auto& group = persistent_gpu_groups[i];
        if (group.blend_mode == blend_mode &&
            group.depth_test == depth_test &&
            (global_sort_group || group.key == key)) {
          return static_cast<uint32_t>(i);
        }
      }
      PersistentGpuParticleGroup group{};
      group.key = key;
      group.blend_mode = blend_mode;
      group.depth_test = depth_test;
      group.group_index = static_cast<uint32_t>(persistent_gpu_groups.size());
      group.sortable = blend_mode == rendering::ParticleBlendMode::Alpha ||
                       blend_mode == rendering::ParticleBlendMode::Distortion;
      if (group.sortable && !persistent_gpu_global_sort_active) {
        particle_pass_stats_.gpu_grouped_sort_fallback = true;
      }
      persistent_gpu_groups.push_back(group);
      return static_cast<uint32_t>(persistent_gpu_groups.size() - 1u);
    };

    auto append_mesh_source_samples =
        [&](ParticleGpuEmitterDesc& gpu_desc,
            const rendering::ParticleEmitterGpuDesc& emitter) {
          if (emitter.source_shape != rendering::ParticleSourceShape::MeshSurface ||
              emitter.source_mesh == rendering::kInvalidMesh) {
            return true;
          }
          const auto mesh_it = meshes_.find(emitter.source_mesh);
          if (mesh_it == meshes_.end() || mesh_it->second.particle_source_samples.empty()) {
            return true;
          }
          const auto& samples = mesh_it->second.particle_source_samples;
          if (samples.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()) ||
              persistent_gpu_mesh_samples.size() >
                  static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()) - samples.size()) {
            return false;
          }

          gpu_desc.source_mesh_sample_offset =
              static_cast<uint32_t>(persistent_gpu_mesh_samples.size());
          gpu_desc.source_mesh_sample_count = static_cast<uint32_t>(samples.size());
          persistent_gpu_mesh_samples.insert(persistent_gpu_mesh_samples.end(),
                                             samples.begin(),
                                             samples.end());
          return true;
        };

    for (const auto& submission : particle_emitter_submissions_) {
      const auto& emitter = submission.desc;
      if (emitter.layer != layer || emitter.max_particles == 0u) {
        continue;
      }
      const bool gpu_visible = is_emitter_visible_to_camera(emitter);
      if (!gpu_visible) {
        particle_pass_stats_.gpu_culled_emitters += 1u;
      }

      ParticleEmitterRuntimeState& runtime =
          particle_emitter_runtime_states_[emitter.instance_id];
      const uint32_t requested_capacity = std::max(emitter.max_particles, 1u);
      if (!runtime.gpu_emitter_state_allocated) {
        if (!allocate_particle_gpu_emitter_state_slot(runtime.gpu_emitter_state_index)) {
          particle_pass_stats_.gpu_fallback_active = true;
          return false;
        }
        runtime.gpu_emitter_state_allocated = true;
        runtime.gpu_reset_pending = true;
      }
      if (runtime.gpu_slot_capacity < requested_capacity) {
        if (runtime.gpu_slot_capacity > 0u) {
          release_particle_gpu_slot(runtime.gpu_slot_offset, runtime.gpu_slot_capacity);
        }
        if (!allocate_particle_gpu_slot(requested_capacity, runtime.gpu_slot_offset)) {
          particle_pass_stats_.gpu_fallback_active = true;
          return false;
        }
        runtime.gpu_slot_capacity = requested_capacity;
        runtime.gpu_reset_pending = true;
      }

      const ParticleBatchGroupKey key = make_emitter_group_key(submission);
      uint32_t material_id = 0u;
      if (persistent_gpu_global_sort_active && isTransparentParticleBlend(emitter.blend_mode)) {
        if (auto it = persistent_gpu_material_lookup.find(key);
            it != persistent_gpu_material_lookup.end()) {
          material_id = it->second;
        }
      }
      const uint32_t group_index =
          find_or_add_group(key, emitter.blend_mode, emitter.depth_test);
      auto& group = persistent_gpu_groups[group_index];
      group.max_particles += requested_capacity;

      ParticleGpuEmitterDesc gpu_desc{};
      gpu_desc.position[0] = emitter.position.x;
      gpu_desc.position[1] = emitter.position.y;
      gpu_desc.position[2] = emitter.position.z;
      gpu_desc.rotation[0] = emitter.rotation.x;
      gpu_desc.rotation[1] = emitter.rotation.y;
      gpu_desc.rotation[2] = emitter.rotation.z;
      gpu_desc.rotation[3] = emitter.rotation.w;
      gpu_desc.scale_time[0] = emitter.scale.x;
      gpu_desc.scale_time[1] = emitter.scale.y;
      gpu_desc.scale_time[2] = emitter.scale.z;
      gpu_desc.playback[0] = std::max(emitter.delta_seconds, 0.0f);
      gpu_desc.playback[1] = std::max(emitter.time_scale, 0.0f);
      gpu_desc.playback[2] = std::max(emitter.start_delay, 0.0f);
      gpu_desc.playback[3] = std::max(emitter.duration, 0.0f);
      gpu_desc.emission[0] = std::max(emitter.spawn_rate, 0.0f);
      gpu_desc.emission[1] = static_cast<float>(emitter.burst_count);
      gpu_desc.emission[2] = static_cast<float>(requested_capacity);
      gpu_desc.lifetime[0] = emitter.particle_lifetime_min;
      gpu_desc.lifetime[1] = emitter.particle_lifetime_max;
      gpu_desc.size[0] = emitter.start_size_min;
      gpu_desc.size[1] = emitter.start_size_max;
      gpu_desc.size[2] = emitter.end_size_min;
      gpu_desc.size[3] = emitter.end_size_max;
      gpu_desc.rotation_params[0] = emitter.initial_rotation_min;
      gpu_desc.rotation_params[1] = emitter.initial_rotation_max;
      gpu_desc.rotation_params[2] = emitter.angular_velocity_min;
      gpu_desc.rotation_params[3] = emitter.angular_velocity_max;
      fill_source_data(gpu_desc, emitter);
      gpu_desc.velocity_min[0] = emitter.velocity_min.x;
      gpu_desc.velocity_min[1] = emitter.velocity_min.y;
      gpu_desc.velocity_min[2] = emitter.velocity_min.z;
      gpu_desc.velocity_max[0] = emitter.velocity_max.x;
      gpu_desc.velocity_max[1] = emitter.velocity_max.y;
      gpu_desc.velocity_max[2] = emitter.velocity_max.z;
      gpu_desc.acceleration_drag[0] = emitter.acceleration.x;
      gpu_desc.acceleration_drag[1] = emitter.acceleration.y;
      gpu_desc.acceleration_drag[2] = emitter.acceleration.z;
      gpu_desc.acceleration_drag[3] = std::max(emitter.drag, 0.0f);
      gpu_desc.collision[0] = emitter.ground_height;
      gpu_desc.collision[1] = std::clamp(emitter.bounce_damping, 0.0f, 1.0f);
      gpu_desc.collision[2] = std::clamp(emitter.collision_friction, 0.0f, 1.0f);
      gpu_desc.collision[3] = std::max(emitter.rest_speed_threshold, 0.0f);
      gpu_desc.color_start[0] = emitter.start_color.r;
      gpu_desc.color_start[1] = emitter.start_color.g;
      gpu_desc.color_start[2] = emitter.start_color.b;
      gpu_desc.color_start[3] = emitter.start_color.a;
      gpu_desc.color_end[0] = emitter.end_color.r;
      gpu_desc.color_end[1] = emitter.end_color.g;
      gpu_desc.color_end[2] = emitter.end_color.b;
      gpu_desc.color_end[3] = emitter.end_color.a;
      gpu_desc.slot_offset = runtime.gpu_slot_offset;
      gpu_desc.slot_capacity = requested_capacity;
      gpu_desc.group_index = group_index;
      gpu_desc.restart_count = emitter.restart_count;
      const uint32_t fallback_seed = static_cast<uint32_t>(
          (emitter.instance_id >> 32u) ^
          (emitter.instance_id & 0xFFFFFFFFu) ^
          0x9E3779B9u);
      gpu_desc.seed = emitter.seed != 0u ? emitter.seed : std::max(fallback_seed, 1u);
      gpu_desc.flags = 0u;
      gpu_desc.flags |= emitter.emit_burst_on_start ? 1u : 0u;
      gpu_desc.flags |= emitter.loop ? 2u : 0u;
      gpu_desc.flags |= emitter.local_space ? 4u : 0u;
      gpu_desc.flags |= emitter.collide_with_ground ? 8u : 0u;
      gpu_desc.flags |= emitter.enabled ? 16u : 0u;
      gpu_desc.flags |= emitter.playing ? 32u : 0u;
      gpu_desc.flags |= gpu_visible ? 64u : 0u;
      gpu_desc.flags |= runtime.gpu_reset_pending ? 128u : 0u;
      gpu_desc.emitter_index = static_cast<uint32_t>(persistent_gpu_emitters.size());
      gpu_desc.emitter_state_index = runtime.gpu_emitter_state_index;
      gpu_desc.material_id = material_id;
      if (!append_mesh_source_samples(gpu_desc, emitter)) {
        particle_pass_stats_.gpu_allocator_allocation_failures += 1u;
        particle_pass_stats_.gpu_fallback_active = true;
        return false;
      }
      persistent_gpu_emitters.push_back(gpu_desc);
      persistent_gpu_instance_ids.push_back(emitter.instance_id);
    }

    if (persistent_gpu_emitters.empty()) {
      record_particle_gpu_allocator_stats();
      return true;
    }

    uint32_t instance_base = 0u;
    uint32_t sort_base = 0u;
    persistent_gpu_material_groups.reserve(persistent_gpu_groups.size());
    for (auto& group : persistent_gpu_groups) {
      group.instance_base = instance_base;
      instance_base += group.max_particles;
      if (group.sortable) {
        group.sort_base = sort_base;
        group.sort_capacity = next_power_of_two(std::max(group.max_particles, 1u));
        sort_base += group.sort_capacity;
      }
      persistent_gpu_material_groups.push_back(ParticleGpuMaterialGroup{
          .instance_base = group.instance_base,
          .sort_base = group.sort_base,
          .max_particles = group.max_particles,
          .sort_capacity = group.sort_capacity,
          .flags = group.sortable ? 1u : 0u,
      });
    }
    persistent_gpu_instance_capacity = instance_base;

    const auto simulation_start = core::SteadyClock::now();
    const std::size_t emitter_count = persistent_gpu_emitters.size();
    const std::size_t group_count = persistent_gpu_material_groups.size();
    const std::size_t sort_capacity = sort_base;
    if (!ensure_particle_instance_buffer(persistent_gpu_instance_capacity) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Emitter Descs",
                                               emitter_count,
                                               sizeof(ParticleGpuEmitterDesc),
                                               Diligent::BIND_SHADER_RESOURCE,
                                               particle_gpu_emitter_desc_buffer_,
                                               std::addressof(particle_gpu_emitter_desc_srv_),
                                               nullptr,
                                               particle_gpu_emitter_desc_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Emitter States",
                                               particle_gpu_emitter_state_allocated_capacity_,
                                               sizeof(ParticleGpuEmitterState),
                                               Diligent::BIND_SHADER_RESOURCE |
                                                   Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_emitter_state_buffer_,
                                               std::addressof(particle_gpu_emitter_state_srv_),
                                               std::addressof(particle_gpu_emitter_state_uav_),
                                               particle_gpu_emitter_state_capacity_,
                                               true) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU States",
                                               particle_gpu_allocated_capacity_,
                                               sizeof(ParticleGpuState),
                                               Diligent::BIND_SHADER_RESOURCE |
                                                   Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_state_buffer_,
                                               std::addressof(particle_gpu_state_srv_),
                                               std::addressof(particle_gpu_state_uav_),
                                               particle_gpu_state_capacity_,
                                               true) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Alive List",
                                               particle_gpu_allocated_capacity_,
                                               sizeof(uint32_t),
                                               Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_alive_list_buffer_,
                                               nullptr,
                                               std::addressof(particle_gpu_alive_list_uav_),
                                               particle_gpu_alive_list_capacity_,
                                               false)) {
      particle_pass_stats_.gpu_fallback_active = true;
      return false;
    }

    if (!ensure_particle_gpu_structured_buffer("Karma Particle GPU Dead List",
                                               particle_gpu_allocated_capacity_,
                                               sizeof(uint32_t),
                                               Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_dead_list_buffer_,
                                               nullptr,
                                               std::addressof(particle_gpu_dead_list_uav_),
                                               particle_gpu_dead_list_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Material Groups",
                                               group_count,
                                               sizeof(ParticleGpuMaterialGroup),
                                               Diligent::BIND_SHADER_RESOURCE,
                                               particle_gpu_group_buffer_,
                                               std::addressof(particle_gpu_group_srv_),
                                               nullptr,
                                               particle_gpu_group_capacity_,
                                               false) ||
        (persistent_gpu_global_sort_active &&
         !ensure_particle_gpu_structured_buffer("Karma Particle GPU Material Records",
                                                persistent_gpu_material_records.size(),
                                                sizeof(ParticleGpuMaterialRecord),
                                                Diligent::BIND_SHADER_RESOURCE,
                                                particle_gpu_material_record_buffer_,
                                                std::addressof(particle_gpu_material_record_srv_),
                                                nullptr,
                                                particle_gpu_material_record_capacity_,
                                                false)) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Mesh Samples",
                                               persistent_gpu_mesh_samples.size(),
                                               sizeof(ParticleGpuMeshSample),
                                               Diligent::BIND_SHADER_RESOURCE,
                                               particle_gpu_mesh_sample_buffer_,
                                               std::addressof(particle_gpu_mesh_sample_srv_),
                                               nullptr,
                                               particle_gpu_mesh_sample_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Group Counters",
                                               group_count,
                                               sizeof(uint32_t),
                                               Diligent::BIND_SHADER_RESOURCE |
                                                   Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_group_counter_buffer_,
                                               std::addressof(particle_gpu_group_counter_srv_),
                                               std::addressof(particle_gpu_group_counter_uav_),
                                               particle_gpu_group_counter_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Sort Items",
                                               sort_capacity,
                                               sizeof(ParticleGpuSortItem),
                                               Diligent::BIND_SHADER_RESOURCE |
                                                   Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_sort_item_buffer_,
                                               std::addressof(particle_gpu_sort_item_srv_),
                                               std::addressof(particle_gpu_sort_item_uav_),
                                               particle_gpu_sort_capacity_,
                                               false)) {
      particle_pass_stats_.gpu_fallback_active = true;
      return false;
    }

    if (!ensure_particle_gpu_structured_buffer("Karma Particle GPU Indirect Draw Args",
                                               group_count,
                                               sizeof(ParticleGpuIndirectArgs),
                                               Diligent::BIND_UNORDERED_ACCESS |
                                                   Diligent::BIND_INDIRECT_DRAW_ARGS,
                                               particle_gpu_indirect_draw_buffer_,
                                               nullptr,
                                               std::addressof(particle_gpu_indirect_draw_uav_),
                                               particle_gpu_indirect_draw_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Indirect Dispatch Args",
                                               1u,
                                               sizeof(uint32_t) * 4u,
                                               Diligent::BIND_UNORDERED_ACCESS |
                                                   Diligent::BIND_INDIRECT_DRAW_ARGS,
                                               particle_gpu_indirect_dispatch_buffer_,
                                               nullptr,
                                               std::addressof(particle_gpu_indirect_dispatch_uav_),
                                               particle_gpu_indirect_dispatch_capacity_,
                                               false) ||
        !ensure_particle_gpu_structured_buffer("Karma Particle GPU Stats",
                                               1u,
                                               sizeof(ParticleGpuStatsReadback),
                                               Diligent::BIND_UNORDERED_ACCESS,
                                               particle_gpu_stats_buffer_,
                                               nullptr,
                                               std::addressof(particle_gpu_stats_uav_),
                                               particle_gpu_stats_capacity_,
                                               false) ||
        !ensure_particle_gpu_readback_buffers()) {
      particle_pass_stats_.gpu_fallback_active = true;
      return false;
    }

    context_->UpdateBuffer(particle_gpu_emitter_desc_buffer_,
                           0,
                           static_cast<Diligent::Uint32>(
                               persistent_gpu_emitters.size() * sizeof(ParticleGpuEmitterDesc)),
                           persistent_gpu_emitters.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->UpdateBuffer(particle_gpu_group_buffer_,
                           0,
                           static_cast<Diligent::Uint32>(
                               persistent_gpu_material_groups.size() *
                               sizeof(ParticleGpuMaterialGroup)),
                           persistent_gpu_material_groups.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (persistent_gpu_global_sort_active && particle_gpu_material_record_buffer_) {
      context_->UpdateBuffer(
          particle_gpu_material_record_buffer_,
          0,
          static_cast<Diligent::Uint32>(persistent_gpu_material_records.size() *
                                        sizeof(ParticleGpuMaterialRecord)),
          persistent_gpu_material_records.data(),
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    if (!persistent_gpu_mesh_samples.empty() && particle_gpu_mesh_sample_buffer_) {
      context_->UpdateBuffer(
          particle_gpu_mesh_sample_buffer_,
          0,
          static_cast<Diligent::Uint32>(persistent_gpu_mesh_samples.size() *
                                        sizeof(ParticleGpuMeshSample)),
          persistent_gpu_mesh_samples.data(),
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    ParticleGpuFrameConstants frame_constants{};
    frame_constants.emitter_count = static_cast<uint32_t>(emitter_count);
    frame_constants.particle_capacity =
        static_cast<uint32_t>(std::min<std::size_t>(
            particle_gpu_allocated_capacity_,
            static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
    frame_constants.group_count = static_cast<uint32_t>(group_count);
    frame_constants.sort_capacity = static_cast<uint32_t>(sort_capacity);
    frame_constants.global_sort_active = persistent_gpu_global_sort_active ? 1u : 0u;
    frame_constants.grouped_sort_fallback =
        has_transparent_gpu_emitters && !persistent_gpu_global_sort_active ? 1u : 0u;
    frame_constants.camera_position[0] = camera_.position.x;
    frame_constants.camera_position[1] = camera_.position.y;
    frame_constants.camera_position[2] = camera_.position.z;
    frame_constants.camera_position[3] = 1.0f;
    frame_constants.camera_forward[0] = context.camera_forward.x;
    frame_constants.camera_forward[1] = context.camera_forward.y;
    frame_constants.camera_forward[2] = context.camera_forward.z;
    frame_constants.camera_forward[3] = 0.0f;
    {
      Diligent::MapHelper<ParticleGpuFrameConstants> cb_map(
          context_,
          particle_gpu_frame_cb_,
          Diligent::MAP_WRITE,
          Diligent::MAP_FLAG_DISCARD);
      if (auto* mapped = getMappedData(cb_map)) {
        *mapped = frame_constants;
      }
    }

    auto set_var = [](Diligent::IShaderResourceVariable* var, auto* resource) {
      if (var && resource) {
        var->Set(resource, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      }
    };

    const auto dispatch_direct = [&](Diligent::IPipelineState* pso,
                                     Diligent::IShaderResourceBinding* srb,
                                     uint32_t item_count) {
      if (!pso || !srb || item_count == 0u) {
        return;
      }
      context_->SetPipelineState(pso);
      context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::DispatchComputeAttribs dispatch{};
      dispatch.ThreadGroupCountX = (item_count + 63u) / 64u;
      dispatch.ThreadGroupCountY = 1u;
      dispatch.ThreadGroupCountZ = 1u;
      context_->DispatchCompute(dispatch);
      particle_pass_stats_.gpu_compute_dispatches += 1u;
    };

    set_var(particle_gpu_clear_groups_var_, particle_gpu_group_srv_.RawPtr());
    set_var(particle_gpu_clear_counters_var_, particle_gpu_group_counter_uav_.RawPtr());
    set_var(particle_gpu_clear_sort_items_var_, particle_gpu_sort_item_uav_.RawPtr());
    set_var(particle_gpu_clear_draw_args_var_, particle_gpu_indirect_draw_uav_.RawPtr());
    set_var(particle_gpu_clear_dispatch_args_var_, particle_gpu_indirect_dispatch_uav_.RawPtr());
    set_var(particle_gpu_clear_stats_var_, particle_gpu_stats_uav_.RawPtr());
    dispatch_direct(particle_gpu_clear_compute_pso_,
                    particle_gpu_clear_compute_srb_,
                    static_cast<uint32_t>(
                        std::max<std::size_t>({1u, group_count, sort_capacity})));

    set_var(particle_gpu_update_emitters_descs_var_, particle_gpu_emitter_desc_srv_.RawPtr());
    set_var(particle_gpu_update_emitters_states_var_, particle_gpu_emitter_state_uav_.RawPtr());
    dispatch_direct(particle_gpu_update_emitters_pso_,
                    particle_gpu_update_emitters_srb_,
                    static_cast<uint32_t>(emitter_count));

    set_var(particle_gpu_simulate_descs_var_, particle_gpu_emitter_desc_srv_.RawPtr());
    set_var(particle_gpu_simulate_emitters_var_, particle_gpu_emitter_state_uav_.RawPtr());
    set_var(particle_gpu_simulate_states_var_, particle_gpu_state_uav_.RawPtr());
    set_var(particle_gpu_simulate_alive_var_, particle_gpu_alive_list_uav_.RawPtr());
    set_var(particle_gpu_simulate_dead_var_, particle_gpu_dead_list_uav_.RawPtr());
    set_var(particle_gpu_simulate_stats_var_, particle_gpu_stats_uav_.RawPtr());
    set_var(particle_gpu_simulate_mesh_samples_var_, particle_gpu_mesh_sample_srv_.RawPtr());
    context_->SetPipelineState(particle_gpu_simulate_pso_);
    context_->CommitShaderResources(particle_gpu_simulate_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs(
        particle_gpu_indirect_dispatch_buffer_,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        0));
    particle_pass_stats_.gpu_compute_dispatches += 1u;

    set_var(particle_gpu_prepare_unsorted_descs_var_, particle_gpu_emitter_desc_srv_.RawPtr());
    set_var(particle_gpu_prepare_unsorted_states_var_, particle_gpu_state_srv_.RawPtr());
    set_var(particle_gpu_prepare_unsorted_groups_var_, particle_gpu_group_srv_.RawPtr());
    set_var(particle_gpu_prepare_unsorted_counters_var_, particle_gpu_group_counter_uav_.RawPtr());
    set_var(particle_gpu_prepare_unsorted_instances_var_, particle_instance_uav_.RawPtr());
    set_var(particle_gpu_prepare_unsorted_stats_var_, particle_gpu_stats_uav_.RawPtr());
    context_->SetPipelineState(particle_gpu_prepare_unsorted_pso_);
    context_->CommitShaderResources(particle_gpu_prepare_unsorted_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs(
        particle_gpu_indirect_dispatch_buffer_,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        0));
    particle_pass_stats_.gpu_compute_dispatches += 1u;

    set_var(particle_gpu_generate_sort_descs_var_, particle_gpu_emitter_desc_srv_.RawPtr());
    set_var(particle_gpu_generate_sort_states_var_, particle_gpu_state_srv_.RawPtr());
    set_var(particle_gpu_generate_sort_groups_var_, particle_gpu_group_srv_.RawPtr());
    set_var(particle_gpu_generate_sort_counters_var_, particle_gpu_group_counter_uav_.RawPtr());
    set_var(particle_gpu_generate_sort_items_var_, particle_gpu_sort_item_uav_.RawPtr());
    set_var(particle_gpu_generate_sort_stats_var_, particle_gpu_stats_uav_.RawPtr());
    context_->SetPipelineState(particle_gpu_generate_sort_pso_);
    context_->CommitShaderResources(particle_gpu_generate_sort_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->DispatchComputeIndirect(Diligent::DispatchComputeIndirectAttribs(
        particle_gpu_indirect_dispatch_buffer_,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        0));
    particle_pass_stats_.gpu_compute_dispatches += 1u;

    set_var(particle_gpu_sort_items_var_, particle_gpu_sort_item_uav_.RawPtr());
    for (const auto& group : persistent_gpu_groups) {
      if (!group.sortable || group.sort_capacity <= 1u) {
        continue;
      }
      for (uint32_t k = 2u; k <= group.sort_capacity; k <<= 1u) {
        for (uint32_t j = k >> 1u; j > 0u; j >>= 1u) {
          ParticleGpuSortConstants sort_constants{};
          sort_constants.sort_base = group.sort_base;
          sort_constants.sort_count_power2 = group.sort_capacity;
          sort_constants.k = k;
          sort_constants.j = j;
          {
            Diligent::MapHelper<ParticleGpuSortConstants> sort_cb(
                context_,
                particle_gpu_sort_cb_,
                Diligent::MAP_WRITE,
                Diligent::MAP_FLAG_DISCARD);
            if (auto* mapped = getMappedData(sort_cb)) {
              *mapped = sort_constants;
            }
          }
          dispatch_direct(particle_gpu_sort_pso_,
                          particle_gpu_sort_srb_,
                          group.sort_capacity);
          particle_pass_stats_.gpu_sort_passes += 1u;
        }
      }
    }

    if (sort_capacity > 0u) {
      set_var(particle_gpu_prepare_sorted_states_var_, particle_gpu_state_srv_.RawPtr());
      set_var(particle_gpu_prepare_sorted_groups_var_, particle_gpu_group_srv_.RawPtr());
      set_var(particle_gpu_prepare_sorted_counters_var_, particle_gpu_group_counter_srv_.RawPtr());
      set_var(particle_gpu_prepare_sorted_sort_items_var_, particle_gpu_sort_item_srv_.RawPtr());
      set_var(particle_gpu_prepare_sorted_instances_var_, particle_instance_uav_.RawPtr());
      set_var(particle_gpu_prepare_sorted_stats_var_, particle_gpu_stats_uav_.RawPtr());
      dispatch_direct(particle_gpu_prepare_sorted_pso_,
                      particle_gpu_prepare_sorted_srb_,
                      static_cast<uint32_t>(sort_capacity));
    }

    set_var(particle_gpu_indirect_args_groups_var_, particle_gpu_group_srv_.RawPtr());
    set_var(particle_gpu_indirect_args_counters_var_, particle_gpu_group_counter_srv_.RawPtr());
    set_var(particle_gpu_indirect_args_draw_args_var_, particle_gpu_indirect_draw_uav_.RawPtr());
    set_var(particle_gpu_indirect_args_stats_var_, particle_gpu_stats_uav_.RawPtr());
    dispatch_direct(particle_gpu_indirect_args_pso_,
                    particle_gpu_indirect_args_srb_,
                    static_cast<uint32_t>(group_count));

    context_->CopyBuffer(particle_gpu_stats_buffer_,
                         0,
                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                         particle_gpu_stats_readback_buffers_[particle_gpu_stats_readback_frame_],
                         0,
                         sizeof(ParticleGpuStatsReadback),
                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    particle_gpu_stats_readback_valid_ = true;
    particle_gpu_stats_readback_frame_ ^= 1u;

    for (const uint64_t instance_id : persistent_gpu_instance_ids) {
      auto it = particle_emitter_runtime_states_.find(instance_id);
      if (it != particle_emitter_runtime_states_.end()) {
        it->second.gpu_reset_pending = false;
      }
    }

    particle_pass_stats_.simulation_ms +=
        core::elapsedMilliseconds(simulation_start, core::SteadyClock::now());
    record_particle_gpu_allocator_stats();
    return true;
  };

  auto draw_persistent_gpu_particles =
      [&](rendering::ParticleBlendMode blend_mode,
          bool depth_test,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
          Diligent::IShaderResourceVariable* texture_var,
          Diligent::IShaderResourceVariable* scene_color_var,
          Diligent::IShaderResourceVariable* scene_depth_var,
          const ParticleDrawTarget& draw_target) {
    if (!persistent_gpu_ready || !particle_gpu_indirect_draw_buffer_) {
      return false;
    }

    persistent_gpu_spans.clear();
    for (const auto& group : persistent_gpu_groups) {
      if (group.blend_mode != blend_mode || group.depth_test != depth_test ||
          group.max_particles == 0u) {
        continue;
      }
      persistent_gpu_spans.push_back(PreparedParticleSpan{
          .key = group.key,
          .particle_offset = group.instance_base,
          .particle_count = group.max_particles,
          .indirect_draw_index = group.group_index,
          .indirect = true,
      });
      particle_pass_stats_.submitted_batches += 1u;
      switch (blend_mode) {
        case rendering::ParticleBlendMode::Additive:
          particle_pass_stats_.additive_batches += 1u;
          break;
        case rendering::ParticleBlendMode::Alpha:
          particle_pass_stats_.alpha_batches += 1u;
          break;
        case rendering::ParticleBlendMode::Distortion:
          particle_pass_stats_.distortion_batches += 1u;
          particle_pass_stats_.distortion_present = true;
          break;
      }
    }
    if (persistent_gpu_spans.empty()) {
      return true;
    }

    ParticleGlobalPipeline* global_pipeline = nullptr;
    if (persistent_gpu_global_sort_active && isTransparentParticleBlend(blend_mode)) {
      if (blend_mode == rendering::ParticleBlendMode::Alpha) {
        if (use_half_res_alpha) {
          global_pipeline = &particle_global_alpha_half_res_;
        } else {
          global_pipeline = depth_test ? &particle_global_alpha_depth_
                                       : &particle_global_alpha_no_depth_;
        }
      } else if (blend_mode == rendering::ParticleBlendMode::Distortion) {
        global_pipeline = depth_test ? &particle_global_distortion_depth_
                                     : &particle_global_distortion_no_depth_;
      }
    }
    if (global_pipeline != nullptr) {
      draw_global_particle_spans(blend_mode,
                                 *global_pipeline,
                                 draw_target,
                                 persistent_gpu_spans,
                                 persistent_gpu_instance_capacity,
                                 particle_gpu_indirect_draw_buffer_);
      return true;
    }

    draw_particle_spans(blend_mode,
                        pso,
                        srb,
                        texture_var,
                        scene_color_var,
                        scene_depth_var,
                        draw_target,
                        persistent_gpu_spans,
                        persistent_gpu_instance_capacity,
                        nullptr,
                        particle_gpu_indirect_draw_buffer_);
    return true;
  };

  persistent_gpu_ready = run_persistent_gpu_particles();

  auto render_gpu_emitters =
      [&](rendering::ParticleBlendMode blend_mode,
          bool depth_test,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb,
          Diligent::IShaderResourceVariable* texture_var,
          Diligent::IShaderResourceVariable* scene_color_var,
          Diligent::IShaderResourceVariable* scene_depth_var,
          const ParticleDrawTarget& draw_target) {
    if (!particle_sim_compute_pso_ || !particle_sim_compute_srb_ || !particle_sim_cb_ ||
        !particle_sim_output_var_) {
      return;
    }

    thread_local std::vector<GpuEmitterDraw> gpu_draws;
    thread_local std::vector<PreparedParticleSpan> gpu_spans;
    gpu_draws.clear();
    gpu_spans.clear();

    for (const auto& submission : particle_emitter_submissions_) {
      const auto& emitter = submission.desc;
      if (emitter.layer != layer || emitter.depth_test != depth_test ||
          emitter.blend_mode != blend_mode || emitter.max_particles == 0u) {
        continue;
      }

      const uint32_t capacity = std::max(emitter.max_particles, 1u);
      const uint32_t alive_count = estimate_alive_particles(submission);
      if (alive_count == 0u) {
        particle_pass_stats_.gpu_particle_capacity += capacity;
        particle_pass_stats_.gpu_killed_particles += capacity;
        continue;
      }

      const glm::vec3 emitter_position(
          emitter.position.x,
          emitter.position.y,
          emitter.position.z);
      const uint32_t total_spawned = estimate_total_spawned(submission, submission.elapsed_seconds);
      const uint32_t previous_spawned =
          estimate_total_spawned(submission, submission.previous_elapsed_seconds);
      gpu_draws.push_back(GpuEmitterDraw{
          .submission = &submission,
          .key = make_emitter_group_key(submission),
          .particle_offset = 0u,
          .particle_count = capacity,
          .alive_count = alive_count,
          .spawned_this_frame = total_spawned > previous_spawned
                                    ? total_spawned - previous_spawned
                                    : 0u,
          .depth = glm::dot(emitter_position - camera_.position, context.camera_forward),
      });
    }

    if (gpu_draws.empty()) {
      return;
    }

    if (blend_mode == rendering::ParticleBlendMode::Alpha ||
        blend_mode == rendering::ParticleBlendMode::Distortion) {
      particle_pass_stats_.gpu_sort_passes += 1u;
      std::sort(gpu_draws.begin(), gpu_draws.end(), [](const auto& a, const auto& b) {
        return a.depth > b.depth;
      });
    }

    std::size_t required_capacity = 0u;
    for (auto& draw : gpu_draws) {
      draw.particle_offset = required_capacity;
      required_capacity += draw.particle_count;
      gpu_spans.push_back(PreparedParticleSpan{
          .key = draw.key,
          .particle_offset = draw.particle_offset,
          .particle_count = draw.particle_count,
      });
    }

    const auto compute_start = core::SteadyClock::now();
    if (!ensure_particle_instance_buffer(required_capacity)) {
      return;
    }

    if (particle_sim_output_var_) {
      particle_sim_output_var_->Set(particle_instance_uav_,
                                    Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }

    for (const auto& draw : gpu_draws) {
      ParticleSimComputeConstants constants{};
      fill_sim_constants(draw, constants);
      {
        Diligent::MapHelper<ParticleSimComputeConstants> cb_map(context_,
                                                                particle_sim_cb_,
                                                                Diligent::MAP_WRITE,
                                                                Diligent::MAP_FLAG_DISCARD);
        auto* mapped_constants = getMappedData(cb_map);
        if (mapped_constants == nullptr) {
          return;
        }
        *mapped_constants = constants;
      }
      context_->SetPipelineState(particle_sim_compute_pso_);
      context_->CommitShaderResources(particle_sim_compute_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      Diligent::DispatchComputeAttribs dispatch{};
      dispatch.ThreadGroupCountX =
          static_cast<Diligent::Uint32>((draw.particle_count + 63u) / 64u);
      dispatch.ThreadGroupCountY = 1u;
      dispatch.ThreadGroupCountZ = 1u;
      context_->DispatchCompute(dispatch);
      particle_pass_stats_.gpu_compute_dispatches += 1u;
      record_gpu_draw_stats(blend_mode,
                            draw.alive_count,
                            static_cast<uint32_t>(std::min<std::size_t>(
                                draw.particle_count,
                                static_cast<std::size_t>(std::numeric_limits<uint32_t>::max()))),
                            draw.spawned_this_frame);
    }
    particle_pass_stats_.simulation_ms +=
        core::elapsedMilliseconds(compute_start, core::SteadyClock::now());

    Diligent::StateTransitionDesc transition{};
    transition.pResource = particle_instance_vb_;
    transition.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    transition.NewState = Diligent::RESOURCE_STATE_VERTEX_BUFFER;
    transition.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    context_->TransitionResourceStates(1, &transition);

    draw_particle_spans(blend_mode,
                        pso,
                        srb,
                        texture_var,
                        scene_color_var,
                        scene_depth_var,
                        draw_target,
                        gpu_spans,
                        required_capacity,
                        nullptr,
                        nullptr);
  };

  auto build_additive_stream = [&](bool depth_test, PreparedParticleStream& stream) {
    const auto grouping_start = core::SteadyClock::now();
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
          batch.blend_mode != rendering::ParticleBlendMode::Additive ||
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
      group.batch_indices.push_back(batch_index);
      group.particle_count += batch.particles.size();
    }

    stream.particles.reserve(total_particles);
    stream.spans.reserve(additive_groups.size());
    for (const auto& group : additive_groups) {
      if (group.particle_count == 0u) {
        continue;
      }
      const std::size_t particle_offset = stream.particles.size();
      stream.particles.reserve(stream.particles.size() + group.particle_count);
      for (const std::size_t batch_index : group.batch_indices) {
        const auto& batch = particle_batches_[batch_index];
        stream.particles.insert(stream.particles.end(),
                                batch.particles.begin(),
                                batch.particles.end());
      }
      stream.spans.push_back(PreparedParticleSpan{
          .key = group.key,
          .particle_offset = particle_offset,
          .particle_count = stream.particles.size() - particle_offset,
      });
    }

    particle_pass_stats_.additive_grouping_ms +=
        core::elapsedMilliseconds(grouping_start, core::SteadyClock::now());
  };

  struct SortedStreamBuildMetrics {
    float collect_ms = 0.0f;
    float sort_only_ms = 0.0f;
    float span_ms = 0.0f;
  };

  auto build_sorted_stream = [&](rendering::ParticleBlendMode blend_mode,
                                 bool depth_test,
                                 PreparedParticleStream& stream) {
    SortedStreamBuildMetrics metrics{};
    stream.particles.clear();
    stream.spans.clear();

    const auto collect_start = core::SteadyClock::now();
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
          if (blend_mode == rendering::ParticleBlendMode::Alpha) {
            particle_pass_stats_.alpha_invalid_depth_particles += 1u;
          } else if (blend_mode == rendering::ParticleBlendMode::Distortion) {
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
    metrics.collect_ms = core::elapsedMilliseconds(collect_start, core::SteadyClock::now());

    if (sorted_particles.empty()) {
      return metrics;
    }

    const uint32_t sorted_particle_count = static_cast<uint32_t>(std::min<std::size_t>(
        sorted_particles.size(),
        static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())));
    if (blend_mode == rendering::ParticleBlendMode::Alpha) {
      particle_pass_stats_.alpha_sorted_particles += sorted_particle_count;
    } else if (blend_mode == rendering::ParticleBlendMode::Distortion) {
      particle_pass_stats_.distortion_sorted_particles += sorted_particle_count;
    }

    const auto sort_only_start = core::SteadyClock::now();
    if (sorted_particles.size() > 1u) {
      std::sort(sorted_particles.begin(), sorted_particles.end(), sortedParticleLess);
    }
    metrics.sort_only_ms = core::elapsedMilliseconds(sort_only_start, core::SteadyClock::now());

    const auto span_start = core::SteadyClock::now();
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
    metrics.span_ms = core::elapsedMilliseconds(span_start, core::SteadyClock::now());
    return metrics;
  };

  auto record_sorted_stream_metrics = [&](rendering::ParticleBlendMode blend_mode,
                                          const SortedStreamBuildMetrics& metrics) {
    if (blend_mode == rendering::ParticleBlendMode::Alpha) {
      particle_pass_stats_.alpha_collect_ms += metrics.collect_ms;
      particle_pass_stats_.alpha_sort_only_ms += metrics.sort_only_ms;
      particle_pass_stats_.alpha_span_ms += metrics.span_ms;
      particle_pass_stats_.alpha_sort_ms +=
          metrics.collect_ms + metrics.sort_only_ms + metrics.span_ms;
    } else if (blend_mode == rendering::ParticleBlendMode::Distortion) {
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

    const auto composite_start = core::SteadyClock::now();
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
        core::elapsedMilliseconds(composite_start, core::SteadyClock::now());
  };

  if (context.allow_distortion_particles) {
    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Distortion,
                                       true,
                                       particle_pipeline_state_distortion_depth_,
                                       particle_srb_distortion_depth_,
                                       particle_texture_var_distortion_depth_,
                                       particle_scene_color_var_distortion_depth_,
                                       particle_scene_depth_var_distortion_depth_,
                                       default_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Distortion,
                          true,
                          particle_pipeline_state_distortion_depth_,
                          particle_srb_distortion_depth_,
                          particle_texture_var_distortion_depth_,
                          particle_scene_color_var_distortion_depth_,
                          particle_scene_depth_var_distortion_depth_,
                          default_target);
    }
    const auto distortion_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Distortion, true, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Distortion,
                            particle_pipeline_state_distortion_depth_,
                            particle_srb_distortion_depth_,
                            particle_texture_var_distortion_depth_,
                            particle_scene_color_var_distortion_depth_,
                            particle_scene_depth_var_distortion_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Distortion, distortion_depth_metrics);

    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Distortion,
                                       false,
                                       particle_pipeline_state_distortion_no_depth_,
                                       particle_srb_distortion_no_depth_,
                                       particle_texture_var_distortion_no_depth_,
                                       particle_scene_color_var_distortion_no_depth_,
                                       particle_scene_depth_var_distortion_no_depth_,
                                       default_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Distortion,
                          false,
                          particle_pipeline_state_distortion_no_depth_,
                          particle_srb_distortion_no_depth_,
                          particle_texture_var_distortion_no_depth_,
                          particle_scene_color_var_distortion_no_depth_,
                          particle_scene_depth_var_distortion_no_depth_,
                          default_target);
    }
    const auto distortion_no_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Distortion, false, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Distortion,
                            particle_pipeline_state_distortion_no_depth_,
                            particle_srb_distortion_no_depth_,
                            particle_texture_var_distortion_no_depth_,
                            particle_scene_color_var_distortion_no_depth_,
                            particle_scene_depth_var_distortion_no_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Distortion,
                                 distortion_no_depth_metrics);
  }

  if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Additive,
                                     true,
                                     particle_pipeline_state_additive_depth_,
                                     particle_srb_additive_depth_,
                                     particle_texture_var_additive_depth_,
                                     particle_scene_color_var_additive_depth_,
                                     particle_scene_depth_var_additive_depth_,
                                     default_target)) {
    render_gpu_emitters(rendering::ParticleBlendMode::Additive,
                        true,
                        particle_pipeline_state_additive_depth_,
                        particle_srb_additive_depth_,
                        particle_texture_var_additive_depth_,
                        particle_scene_color_var_additive_depth_,
                        particle_scene_depth_var_additive_depth_,
                        default_target);
  }
  build_additive_stream(true, prepared_stream);
  draw_prepared_particles(rendering::ParticleBlendMode::Additive,
                          particle_pipeline_state_additive_depth_,
                          particle_srb_additive_depth_,
                          particle_texture_var_additive_depth_,
                          particle_scene_color_var_additive_depth_,
                          particle_scene_depth_var_additive_depth_,
                          default_target,
                          prepared_stream);
  if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Additive,
                                     false,
                                     particle_pipeline_state_additive_no_depth_,
                                     particle_srb_additive_no_depth_,
                                     particle_texture_var_additive_no_depth_,
                                     particle_scene_color_var_additive_no_depth_,
                                     particle_scene_depth_var_additive_no_depth_,
                                     default_target)) {
    render_gpu_emitters(rendering::ParticleBlendMode::Additive,
                        false,
                        particle_pipeline_state_additive_no_depth_,
                        particle_srb_additive_no_depth_,
                        particle_texture_var_additive_no_depth_,
                        particle_scene_color_var_additive_no_depth_,
                        particle_scene_depth_var_additive_no_depth_,
                        default_target);
  }
  build_additive_stream(false, prepared_stream);
  draw_prepared_particles(rendering::ParticleBlendMode::Additive,
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
    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Alpha,
                                       true,
                                       particle_pipeline_state_alpha_half_res_,
                                       particle_srb_alpha_half_res_,
                                       particle_texture_var_alpha_half_res_,
                                       particle_scene_color_var_alpha_half_res_,
                                       particle_scene_depth_var_alpha_half_res_,
                                       depth_alpha_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Alpha,
                          true,
                          particle_pipeline_state_alpha_half_res_,
                          particle_srb_alpha_half_res_,
                          particle_texture_var_alpha_half_res_,
                          particle_scene_color_var_alpha_half_res_,
                          particle_scene_depth_var_alpha_half_res_,
                          depth_alpha_target);
    }
    const auto alpha_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Alpha, true, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_half_res_,
                            particle_srb_alpha_half_res_,
                            particle_texture_var_alpha_half_res_,
                            particle_scene_color_var_alpha_half_res_,
                            particle_scene_depth_var_alpha_half_res_,
                            depth_alpha_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Alpha, alpha_depth_metrics);

    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Alpha,
                                       false,
                                       particle_pipeline_state_alpha_half_res_,
                                       particle_srb_alpha_half_res_,
                                       particle_texture_var_alpha_half_res_,
                                       particle_scene_color_var_alpha_half_res_,
                                       particle_scene_depth_var_alpha_half_res_,
                                       half_res_alpha_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Alpha,
                          false,
                          particle_pipeline_state_alpha_half_res_,
                          particle_srb_alpha_half_res_,
                          particle_texture_var_alpha_half_res_,
                          particle_scene_color_var_alpha_half_res_,
                          particle_scene_depth_var_alpha_half_res_,
                          half_res_alpha_target);
    }
    const auto alpha_no_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Alpha, false, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_half_res_,
                            particle_srb_alpha_half_res_,
                            particle_texture_var_alpha_half_res_,
                            particle_scene_color_var_alpha_half_res_,
                            particle_scene_depth_var_alpha_half_res_,
                            half_res_alpha_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Alpha, alpha_no_depth_metrics);
    composite_half_res_alpha();
  } else {
    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Alpha,
                                       true,
                                       particle_pipeline_state_alpha_depth_,
                                       particle_srb_alpha_depth_,
                                       particle_texture_var_alpha_depth_,
                                       particle_scene_color_var_alpha_depth_,
                                       particle_scene_depth_var_alpha_depth_,
                                       default_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Alpha,
                          true,
                          particle_pipeline_state_alpha_depth_,
                          particle_srb_alpha_depth_,
                          particle_texture_var_alpha_depth_,
                          particle_scene_color_var_alpha_depth_,
                          particle_scene_depth_var_alpha_depth_,
                          default_target);
    }
    const auto alpha_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Alpha, true, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_depth_,
                            particle_srb_alpha_depth_,
                            particle_texture_var_alpha_depth_,
                            particle_scene_color_var_alpha_depth_,
                            particle_scene_depth_var_alpha_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Alpha, alpha_depth_metrics);

    if (!draw_persistent_gpu_particles(rendering::ParticleBlendMode::Alpha,
                                       false,
                                       particle_pipeline_state_alpha_no_depth_,
                                       particle_srb_alpha_no_depth_,
                                       particle_texture_var_alpha_no_depth_,
                                       particle_scene_color_var_alpha_no_depth_,
                                       particle_scene_depth_var_alpha_no_depth_,
                                       default_target)) {
      render_gpu_emitters(rendering::ParticleBlendMode::Alpha,
                          false,
                          particle_pipeline_state_alpha_no_depth_,
                          particle_srb_alpha_no_depth_,
                          particle_texture_var_alpha_no_depth_,
                          particle_scene_color_var_alpha_no_depth_,
                          particle_scene_depth_var_alpha_no_depth_,
                          default_target);
    }
    const auto alpha_no_depth_metrics =
        build_sorted_stream(rendering::ParticleBlendMode::Alpha, false, prepared_stream);
    draw_prepared_particles(rendering::ParticleBlendMode::Alpha,
                            particle_pipeline_state_alpha_no_depth_,
                            particle_srb_alpha_no_depth_,
                            particle_texture_var_alpha_no_depth_,
                            particle_scene_color_var_alpha_no_depth_,
                            particle_scene_depth_var_alpha_no_depth_,
                            default_target,
                            prepared_stream);
    record_sorted_stream_metrics(rendering::ParticleBlendMode::Alpha, alpha_no_depth_metrics);
  }
}

}  // namespace karma::rendering::backend
