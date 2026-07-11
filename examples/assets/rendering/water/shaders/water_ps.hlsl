// Flat-plane water with runtime-generated micro normals. Scene color/depth
// provide refraction and automatic shoreline depth; analytic swells add broad
// motion without deforming geometry.

cbuffer Constants
{
    float4x4 g_MVP;
    float4x4 g_Model;
    float4x4 g_LightViewProj;
    float4x4 g_ShadowUVProj;
    float4x4 g_ShadowCascadeUVProj[4];
    float4x4 g_PointShadowUVProj[96];
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
    float4 g_PbrParams;
    float4 g_EnvParams;
    float4 g_ShadowParams;
    float4 g_PointShadowParams;
    float4 g_LocalLightParams;
    float4 g_PointShadowTuning;
    float4 g_ShadowBiasParams;
    float4 g_ShadowCascadeSplits;
    float4 g_ShadowCascadeWorldTexel;
    float4 g_ShadowCascadeParams;
    float4 g_LightDir;
    float4 g_LightColor;
    float4 g_CameraPos;
    float4 g_CameraForward;
    float4 g_ScreenParams;
    float4 g_CameraClipParams;
    float4 g_ForwardPlusParams;
    float4 g_LocalLightPositionRange[64];
    float4 g_LocalLightDirectionType[64];
    float4 g_LocalLightColorIntensity[64];
    float4 g_LocalLightSpotParams[64];
    float4 g_LocalLightMeta;
    float4 g_InstanceParams;
    float4 g_MaterialParams0;
    float4 g_MaterialParams1;
    float4 g_MaterialParams2;
    float4 g_MaterialParams3;
    float4 g_MaterialParams4;
    float4 g_MaterialParams5;
    float4 g_MaterialParams6;
    float4 g_VolumeParams0;
    float4 g_VolumeParams1;
    float4 g_VolumeParams2;
    float4 g_VolumeParams3;
    float4 g_VolumeParams4;
    float4 g_TexCoordRow0[12];
    float4 g_TexCoordRow1[12];
};

Texture2D g_SceneColor;
Texture2D<float> g_SceneDepth;
Texture2D g_NormalTex;
TextureCube g_PrefilterTex;
SamplerState g_SamplerColor;
SamplerState g_SamplerData;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    float length_sq = dot(value, value);
    return length_sq > 1.0e-8 ? value * rsqrt(length_sq) : fallback;
}

float LinearizeDepth(float depth)
{
    float near_clip = max(g_CameraClipParams.x, 0.001);
    float far_clip = max(g_CameraClipParams.y, near_clip + 0.001);
    if (g_CameraClipParams.z > 0.5)
    {
        return (near_clip * far_clip) /
               max(far_clip - depth * (far_clip - near_clip), 1.0e-4);
    }
    return near_clip + depth * (far_clip - near_clip);
}

void AccumulateWave(float2 position,
                    float2 direction,
                    float frequency,
                    float amplitude,
                    float phase,
                    float pixel_footprint,
                    inout float2 slope,
                    inout float height)
{
    direction = normalize(direction);
    float angle = dot(position, direction) * frequency + phase;
    float antialias = 1.0 - smoothstep(0.35, 1.15, pixel_footprint * frequency);
    height += sin(angle) * amplitude * antialias;
    slope += direction * (cos(angle) * frequency * amplitude * antialias);
}

float Hash21(float2 position)
{
    float3 p = frac(float3(position.x, position.y, position.x) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise(float2 position)
{
    float2 cell = floor(position);
    float2 local = frac(position);
    float2 blend = local * local * (3.0 - 2.0 * local);
    float a = Hash21(cell);
    float b = Hash21(cell + float2(1.0, 0.0));
    float c = Hash21(cell + float2(0.0, 1.0));
    float d = Hash21(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

float Fbm3(float2 position)
{
    float value = ValueNoise(position) * 0.57;
    value += ValueNoise(position * 2.03 + 17.7) * 0.29;
    value += ValueNoise(position * 4.11 - 9.2) * 0.14;
    return value;
}

float3 ProceduralWaterNormal(float2 world_xz,
                             float time,
                             out float normalized_height,
                             out float micro_variation)
{
    float wave_scale = max(g_MaterialParams3.x, 0.001);
    float wave_strength = max(g_MaterialParams3.y, 0.0);
    float wave_speed = max(g_MaterialParams3.z, 0.0);
    float flow_angle = g_MaterialParams3.w;
    float flow_speed = max(g_MaterialParams6.x, 0.0);
    float detail = max(g_MaterialParams5.w, 0.25);

    float2 flow_direction = float2(cos(flow_angle), sin(flow_angle));
    float2 position = world_xz * wave_scale + flow_direction * (time * flow_speed);
    float warp_x = Fbm3(world_xz * 0.085 + flow_direction * time * 0.018) - 0.5;
    float warp_y = Fbm3(world_xz * 0.117 - flow_direction * time * 0.014 + 23.7) - 0.5;
    position += float2(warp_x, warp_y) * 1.85;
    float pixel_footprint = max(length(ddx(position)), length(ddy(position)));
    float phase = time * wave_speed;
    float2 slope = float2(0.0, 0.0);
    float height = 0.0;
    AccumulateWave(position, float2(1.0, 0.25), 0.65, 0.28, phase * 0.83,
                   pixel_footprint, slope, height);
    AccumulateWave(position, float2(-0.32, 1.0), 1.17, 0.22, phase * 1.11 + 1.7,
                   pixel_footprint, slope, height);
    AccumulateWave(position, float2(0.72, -0.68), 2.15, 0.12, phase * 1.49 + 3.1,
                   pixel_footprint, slope, height);
    AccumulateWave(position, float2(-0.91, -0.41), 3.90, 0.055, phase * 1.93 + 0.6,
                   pixel_footprint, slope, height);
    AccumulateWave(position, float2(0.28, 1.0), 6.40 * detail, 0.012,
                   phase * 2.37 + 2.2, pixel_footprint, slope, height);
    AccumulateWave(position, float2(-0.66, 0.75), 10.8 * detail, 0.005,
                   phase * 2.81 + 4.4, pixel_footprint, slope, height);
    normalized_height = height / 0.692;

    float2 normal_uv_a = world_xz * (0.052 * detail) +
                         flow_direction * time * (0.012 + flow_speed * 0.018);
    const float rotation_cos = 0.7771;
    const float rotation_sin = 0.6293;
    float2 rotated_world = float2(rotation_cos * world_xz.x +
                                  rotation_sin * world_xz.y,
                                  -rotation_sin * world_xz.x +
                                  rotation_cos * world_xz.y);
    float2 normal_uv_b = rotated_world * (0.087 * detail) -
                         flow_direction.yx * time * (0.009 + flow_speed * 0.014);
    float4 detail_sample_a = g_NormalTex.Sample(g_SamplerColor, normal_uv_a);
    float4 detail_sample_b = g_NormalTex.Sample(g_SamplerColor, normal_uv_b);
    float2 detail_slope_a = detail_sample_a.xy * 2.0 - 1.0;
    float2 detail_slope_b_local = detail_sample_b.xy * 2.0 - 1.0;
    float2 detail_slope_b = float2(rotation_cos * detail_slope_b_local.x -
                                   rotation_sin * detail_slope_b_local.y,
                                   rotation_sin * detail_slope_b_local.x +
                                   rotation_cos * detail_slope_b_local.y);
    float detail_strength = 0.16 + wave_strength * 0.32;
    float2 combined_slope = -slope * wave_strength +
                            (detail_slope_a + detail_slope_b) * detail_strength;
    micro_variation = (detail_sample_a.a + detail_sample_b.a) * 0.5;
    return normalize(float3(combined_slope.x, 1.0, combined_slope.y));
}

float FoamNoise(float2 world_xz, float time)
{
    float flow_angle = g_MaterialParams3.w;
    float2 flow = float2(cos(flow_angle), sin(flow_angle));
    float2 p = world_xz * 0.46 + flow * time * (0.10 + g_MaterialParams6.x * 0.35);
    float broad = Fbm3(p);
    float streak = 0.5 + 0.5 * sin(dot(p, float2(2.31, -1.17)) + time * 0.38);
    return saturate(broad * 0.78 + streak * 0.22);
}

float CausticPattern(float2 world_xz, float time)
{
    float2 p = world_xz * 1.28;
    float a = sin(p.x + sin(p.y * 0.73 - time * 0.52) + time * 0.31);
    float b = sin(p.y * 1.17 + sin(p.x * 0.81 + time * 0.47) - time * 0.27);
    float c = sin((p.x + p.y) * 0.67 - time * 0.36);
    float primary = pow(saturate(1.0 - abs(a - b) * 0.72), 7.0);
    float secondary = pow(saturate(1.0 - abs(b - c) * 0.86), 9.0);
    return saturate(primary * 0.72 + secondary * 0.42);
}

float DistributionGGX(float n_dot_h, float perceptual_roughness)
{
    const float PI = 3.14159265;
    float alpha = max(perceptual_roughness * perceptual_roughness, 0.001);
    float alpha_sq = alpha * alpha;
    float denominator = n_dot_h * n_dot_h * (alpha_sq - 1.0) + 1.0;
    return alpha_sq / max(PI * denominator * denominator, 1.0e-5);
}

float GeometrySchlickGGX(float n_dot_direction, float perceptual_roughness)
{
    float r = perceptual_roughness + 1.0;
    float k = r * r * 0.125;
    return n_dot_direction / max(n_dot_direction * (1.0 - k) + k, 1.0e-5);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
    float raw_scene_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
    float water_linear_depth = LinearizeDepth(input.Pos.z);
    float scene_linear_depth = LinearizeDepth(raw_scene_depth);
    float3 unrefracted_scene = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;

    // The transmission pass can cover the complete rectangular plane. Preserve
    // opaque terrain and props that are already in front of it instead of
    // applying water reflection to those pixels near the shoreline.
    if (raw_scene_depth < 0.9999 &&
        scene_linear_depth <= water_linear_depth + 0.02)
    {
        return float4(unrefracted_scene, 1.0);
    }

    float3 ray_direction = SafeNormalize(input.WorldPos - g_CameraPos.xyz,
                                         g_CameraForward.xyz);
    float ray_forward = max(dot(ray_direction, g_CameraForward.xyz), 0.001);
    float optical_depth = max(scene_linear_depth - water_linear_depth, 0.0) /
                          ray_forward;
    float vertical_depth = optical_depth * abs(ray_direction.y);
    float depth_range = max(g_MaterialParams5.z, 0.05);
    if (raw_scene_depth >= 0.9999)
    {
        optical_depth = depth_range * 2.0;
        vertical_depth = depth_range * 2.0;
    }

    float time = g_LocalLightMeta.w * step(-0.5, g_MaterialParams3.z);
    float wave_height = 0.0;
    float micro_variation = 0.5;
    float3 normal = ProceduralWaterNormal(input.WorldPos.xz,
                                          time,
                                          wave_height,
                                          micro_variation);
    float3 view_direction = SafeNormalize(g_CameraPos.xyz - input.WorldPos,
                                          -ray_direction);
    if (dot(normal, view_direction) < 0.0)
    {
        normal = -normal;
    }

    float refraction_strength = max(g_MaterialParams4.x, 0.0);
    float depth_fraction = saturate(vertical_depth / depth_range);
    float shoreline_refraction_fade =
        smoothstep(0.06, max(g_MaterialParams2.a * 1.6, 0.12), vertical_depth);
    float2 refraction_offset = normal.xz * refraction_strength *
                               lerp(0.0015, 0.007, depth_fraction) *
                               shoreline_refraction_fade;
    float2 refracted_uv = clamp(screen_uv + refraction_offset, 0.001, 0.999);
    float refracted_raw_depth = g_SceneDepth.Sample(g_SamplerData, refracted_uv);
    float refracted_linear_depth = LinearizeDepth(refracted_raw_depth);
    float foreground_guard = max(0.08, optical_depth * 0.18);
    if (refracted_linear_depth <= water_linear_depth + 0.015 ||
        refracted_linear_depth < scene_linear_depth - foreground_guard)
    {
        refracted_uv = screen_uv;
    }
    float3 refracted_scene = g_SceneColor.Sample(g_SamplerColor, refracted_uv).rgb;
    float3 floor_position = input.WorldPos + ray_direction * optical_depth;

    float3 light_direction = SafeNormalize(-g_LightDir.xyz,
                                           float3(0.3, 0.8, 0.2));
    float caustic = CausticPattern(floor_position.xz, time) *
                    max(g_MaterialParams5.y, 0.0) *
                    exp(-vertical_depth * 0.72) *
                    saturate(light_direction.y);
    refracted_scene += g_LightColor.rgb * caustic * 0.22;

    float3 shallow_color = saturate(g_MaterialParams0.rgb);
    float clarity = saturate(g_MaterialParams0.a);
    float3 deep_color = saturate(g_MaterialParams1.rgb);
    float absorption_density = max(g_MaterialParams1.a, 0.001);
    float turbidity = 1.0 - clarity;
    float3 spectral_absorption = lerp(float3(2.05, 0.98, 0.32),
                                      float3(1.35, 1.00, 0.78),
                                      turbidity);
    float effective_density = absorption_density * lerp(0.50, 1.35, turbidity);
    float bounded_optical_depth = min(optical_depth, depth_range * 4.0);
    float3 transmittance = exp(-spectral_absorption *
                               effective_density *
                               bounded_optical_depth);
    float3 body_scattering = lerp(shallow_color,
                                  deep_color,
                                  smoothstep(0.08, 1.0, depth_fraction));
    float3 color = refracted_scene * transmittance +
                   body_scattering * (1.0 - transmittance);

    float roughness = clamp(g_MaterialParams4.y +
                            (micro_variation - 0.5) * 0.035 +
                            g_MaterialParams3.y * 0.025,
                            0.025,
                            0.78);
    float fresnel_power = max(g_MaterialParams4.z, 0.25);
    float reflection_strength = max(g_MaterialParams4.w, 0.0);
    float ndotv = saturate(dot(normal, view_direction));
    float fresnel = 0.0204 + 0.9796 * pow(1.0 - ndotv, fresnel_power);
    float3 reflection_direction = reflect(-view_direction, normal);
    float reflection_mip = roughness * max(g_EnvParams.y, 0.0);
    float3 environment_reflection =
        g_PrefilterTex.SampleLevel(g_SamplerColor,
                                   reflection_direction,
                                   reflection_mip).rgb * g_EnvParams.x;
    color = color * (1.0 - fresnel) +
            environment_reflection * fresnel * reflection_strength;

    float3 half_vector = SafeNormalize(light_direction + view_direction,
                                       view_direction);
    float n_dot_l = saturate(dot(normal, light_direction));
    float n_dot_h = saturate(dot(normal, half_vector));
    float h_dot_v = saturate(dot(half_vector, view_direction));
    float direct_fresnel = 0.0204 + 0.9796 * pow(1.0 - h_dot_v, 5.0);
    float distribution = DistributionGGX(n_dot_h, roughness);
    float geometry = GeometrySchlickGGX(ndotv, roughness) *
                     GeometrySchlickGGX(n_dot_l, roughness);
    float sun_specular = distribution * geometry * direct_fresnel /
                         max(4.0 * ndotv * n_dot_l, 1.0e-4);
    color += g_LightColor.rgb * sun_specular * n_dot_l *
             max(g_MaterialParams6.y, 0.0);

    float foam_width = max(g_MaterialParams2.a, 0.001);
    float shore_coordinate = vertical_depth / foam_width;
    float shoreline = 1.0 - smoothstep(0.0, 1.0, shore_coordinate);
    float foam_noise = FoamNoise(floor_position.xz, time);
    float foam_noise_fine = FoamNoise(floor_position.xz * 1.73 + 8.1, time * 1.19);
    float breaker_center = 0.32 + (foam_noise - 0.5) * 0.20;
    float breaker_distance = abs(shore_coordinate - breaker_center);
    float breaker = (1.0 - smoothstep(0.065, 0.18, breaker_distance)) *
                    smoothstep(0.50, 0.74, foam_noise_fine + foam_noise * 0.12);
    float backwash = (1.0 - smoothstep(0.0, 0.11, shore_coordinate)) *
                     (0.015 + foam_noise * 0.045);
    float crest_foam = smoothstep(0.24, 0.45, g_MaterialParams3.y) *
                       smoothstep(0.42, 0.78, wave_height + foam_noise * 0.14) *
                       smoothstep(0.012, 0.075, 1.0 - normal.y) * 0.42;
    float foam_mask = (breaker + backwash + crest_foam) *
                      max(g_MaterialParams5.x, 0.0) *
                      step(0.5, g_MaterialParams6.w);
    float3 foam_color = saturate(g_MaterialParams2.rgb) *
                        lerp(0.86, 1.04, foam_noise_fine);
    color = lerp(color, foam_color, saturate(foam_mask));

    int debug_mode = (int)round(g_MaterialParams6.z);
    if (debug_mode == 1)
    {
        float3 depth_debug = lerp(float3(0.94, 0.92, 0.72),
                                  float3(0.01, 0.08, 0.26), depth_fraction);
        return float4(depth_debug, 1.0);
    }
    if (debug_mode == 2)
    {
        return float4(normal * 0.5 + 0.5, 1.0);
    }
    if (debug_mode == 3)
    {
        return float4(shoreline.xxx, 1.0);
    }

    // The shader has already composited the copied opaque scene with water,
    // so full alpha avoids blending the scene a second time.
    return float4(max(color, 0.0), 1.0);
}
