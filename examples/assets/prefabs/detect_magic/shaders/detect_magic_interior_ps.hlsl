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
SamplerState g_SamplerColor;
SamplerState g_SamplerData;

struct PSInput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    float2 UV : TEXCOORD0;
    float2 UV1 : TEXCOORD1;
    float4 Tangent : TEXCOORD2;
    float3 WorldPos : TEXCOORD3;
    float4 InstanceParams : TEXCOORD4;
};

float3 SafeNormalize(float3 v, float3 fallback)
{
    float len_sq = dot(v, v);
    return len_sq > 1.0e-8 ? v * rsqrt(len_sq) : fallback;
}

float LinearizeSceneDepth(float depth)
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

bool IntersectSphere(float3 ro, float3 rd, float radius, out float t0, out float t1)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = b * b - c;
    if (h < 0.0)
    {
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }
    h = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float2 screen_uv = clamp(input.Pos.xy * g_ScreenParams.zw, 0.001, 0.999);
    float3 center = g_VolumeParams0.xyz;
    float radius = max(g_VolumeParams0.w, 1.0e-4);
    float3 axis_x = SafeNormalize(g_VolumeParams1.xyz, float3(1.0, 0.0, 0.0));
    float3 axis_y = SafeNormalize(g_VolumeParams2.xyz, float3(0.0, 1.0, 0.0));
    float3 axis_z = SafeNormalize(g_VolumeParams3.xyz, float3(0.0, 0.0, 1.0));
    float3 ray_dir = SafeNormalize(input.WorldPos - g_CameraPos.xyz, -g_CameraForward.xyz);
    float3 ro = g_CameraPos.xyz - center;

    float t0;
    float t1;
    if (!IntersectSphere(ro, ray_dir, radius, t0, t1) || t1 <= 0.0)
    {
        discard;
    }
    float t_enter = max(t0, 0.0);
    float t_exit = max(t1, t_enter + 1.0e-4);
    float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
    float raw_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
    if (raw_depth >= 0.9999)
    {
        discard;
    }
    float scene_t = LinearizeSceneDepth(raw_depth) / ray_forward;
    if (scene_t < t_enter || scene_t > t_exit)
    {
        discard;
    }

    float time = g_LocalLightMeta.w;
    float3 scene_pos = g_CameraPos.xyz + ray_dir * scene_t;
    float3 scene_offset = scene_pos - center;
    float3 scene_local3 = float3(dot(scene_offset, axis_x),
                                 dot(scene_offset, axis_y),
                                 dot(scene_offset, axis_z)) / radius;
    float volume_radius = length(scene_local3);
    if (volume_radius > 1.0)
    {
        discard;
    }

    float boundary_fade = 1.0 - smoothstep(0.82, 1.0, volume_radius);
    float surface_depth = min(scene_t - t_enter, t_exit - scene_t);
    float surface_fade = smoothstep(0.0, radius * 0.08, surface_depth);
    float mask = saturate(boundary_fade * surface_fade);
    if (mask <= 0.001)
    {
        discard;
    }

    float2 local_plane = scene_local3.xz;
    float radial = length(local_plane);
    float angle = atan2(local_plane.y, local_plane.x);
    float2 tangent = SafeNormalize(float3(-local_plane.y, local_plane.x, 0.0),
                                   float3(1.0, 0.0, 0.0)).xy;
    float2 radial_dir = SafeNormalize(float3(local_plane, 0.0),
                                      float3(0.0, 1.0, 0.0)).xy;
    float inner_gate = smoothstep(0.03, 0.24, radial) *
                       (1.0 - smoothstep(0.88, 1.0, volume_radius));

    float3 heat_p = scene_local3 * 8.0;
    float plume_a = sin(heat_p.x * 1.91 + heat_p.y * 3.37 + time * 2.10);
    float plume_b = sin(heat_p.z * 2.47 - heat_p.y * 2.81 - time * 1.73);
    float plume_c = sin(dot(heat_p, float3(1.43, -2.11, 1.67)) + time * 2.87);
    float thread_a = sin(scene_local3.y * 42.0 + scene_local3.x * 12.0 + time * 4.10);
    float thread_b = cos(scene_local3.y * 31.0 - scene_local3.z * 15.0 - time * 3.30);
    float shimmer_a = sin(angle * 4.0 + scene_local3.y * 16.0 + time * 3.10);
    float shimmer_b = cos((scene_local3.x - scene_local3.z) * 21.0 - time * 2.45);
    float shimmer_c = sin(dot(scene_local3, float3(17.0, 11.0, -13.0)) + time * 4.70);
    float turbulence = saturate(0.50 + 0.18 * plume_a + 0.18 * plume_b + 0.14 * plume_c);
    float threads = saturate(0.50 + 0.28 * thread_a + 0.22 * thread_b);
    float shimmer = smoothstep(0.24,
                               0.88,
                               0.50 + 0.20 * shimmer_a + 0.18 * shimmer_b +
                                   0.16 * shimmer_c);
    float flicker_cell = Hash21(floor((local_plane + radial_dir * time * 0.035) * 10.0) +
                                floor(time * 6.0));
    float flicker = saturate(0.82 +
                             0.10 * sin(time * 10.5 + shimmer * 6.2831853) +
                             0.08 * step(0.62, flicker_cell));
    float2 drift = float2(plume_a + plume_c * 0.65 + thread_a * 0.45,
                          plume_b - plume_c * 0.55 + thread_b * 0.40);
    float2 rise = float2(sin(scene_local3.y * 24.0 + time * 2.60),
                         cos((scene_local3.x - scene_local3.z) * 18.0 - time * 2.20));
    float heat_strength = (0.0025 + turbulence * 0.0030 + threads * 0.0020) * mask;
    float rainbow_strength = (0.0008 + shimmer * 0.0019) * inner_gate * mask * flicker;
    float2 rainbow_axis =
        SafeNormalize(float3(tangent * (0.72 + 0.28 * shimmer_a) +
                                 radial_dir * (0.24 * shimmer_b),
                             0.0),
                      float3(1.0, 0.0, 0.0)).xy;
    float2 warp = (drift * 0.62 + rise * 0.38) * heat_strength;
    float2 chroma = rainbow_axis * rainbow_strength;

    float3 scene_base = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
    float2 uv_mid = clamp(screen_uv + warp, 0.001, 0.999);
    float3 scene_mid = g_SceneColor.Sample(g_SamplerColor, uv_mid).rgb;
    float red = g_SceneColor.Sample(g_SamplerColor,
                                    clamp(uv_mid + chroma * 1.25, 0.001, 0.999)).r;
    float green = g_SceneColor.Sample(g_SamplerColor,
                                      clamp(uv_mid - chroma * 0.20, 0.001, 0.999)).g;
    float blue = g_SceneColor.Sample(g_SamplerColor,
                                     clamp(uv_mid - chroma * 1.05, 0.001, 0.999)).b;
    float3 chromatic = float3(red, green, blue);
    float3 rainbow_wave = 0.5 + 0.5 * sin(float3(0.0, 2.0944, 4.1888) +
                                          angle * 2.0 +
                                          scene_local3.y * 8.0 +
                                          time * 2.6);
    float heat_lift = (turbulence - 0.5) * 0.028 + threads * 0.012;
    float3 refracted = lerp(scene_mid, chromatic, 0.18 + shimmer * 0.08);
    refracted = lerp(scene_base, refracted, 0.42);
    float3 shimmer_tint = (rainbow_wave - 0.42) * shimmer * flicker * 0.020 * mask;
    float3 color = refracted * (1.0 + heat_lift * mask + (flicker - 0.86) * 0.04 * mask) +
                   shimmer_tint;
    float alpha = saturate(mask * g_BaseColorFactor.a);
    return float4(color, alpha);
}
