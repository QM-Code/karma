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

    float t_surface = t0 > 0.0 ? t0 : t1;
    if (t_surface <= 0.0)
    {
        discard;
    }
    float ray_forward = max(dot(ray_dir, g_CameraForward.xyz), 1.0e-4);
    float raw_depth = g_SceneDepth.Sample(g_SamplerData, screen_uv);
    float scene_t = 1.0e20;
    if (raw_depth < 0.9999)
    {
        scene_t = LinearizeSceneDepth(raw_depth) / ray_forward;
    }
    float shell_depth_bias = radius * 0.006;
    bool front_visible = scene_t >= t_surface - shell_depth_bias;
    bool surface_double_sided = g_VolumeParams4.w > 0.5;
    bool back_visible = surface_double_sided &&
                        t0 > 0.0 &&
                        t1 > t_surface + radius * 0.01 &&
                        scene_t >= t1 - shell_depth_bias;
    if (!front_visible && !back_visible)
    {
        discard;
    }
    float back_layer = back_visible ? 1.0 : 0.0;

    float3 surface_pos = g_CameraPos.xyz + ray_dir * t_surface;
    float3 normal = SafeNormalize(surface_pos - center, -ray_dir);
    float3 view_dir = SafeNormalize(g_CameraPos.xyz - surface_pos, -ray_dir);
    float ndv = saturate(abs(dot(normal, view_dir)));
    float fresnel = pow(1.0 - ndv, 3.0);

    float2 normal_uv = float2(dot(normal, axis_y), dot(normal, axis_z));
    float edge = smoothstep(0.30, 0.92, fresnel);
    float time = g_LocalLightMeta.w;
    float glint = smoothstep(0.86,
                             1.0,
                             0.5 + 0.5 * sin(dot(normal, float3(17.0, 9.0, -13.0)) +
                                             time * 2.4));
    float2 reflect_uv_offset =
        normal_uv * (0.018 + fresnel * 0.045) +
        float2(sin(time * 1.7 + normal.y * 8.0),
               cos(time * 1.3 + normal.z * 7.0)) * 0.003;

    float3 scene = g_SceneColor.Sample(g_SamplerColor, screen_uv).rgb;
    float3 reflected = g_SceneColor.Sample(g_SamplerColor,
                                           clamp(screen_uv + reflect_uv_offset,
                                                 0.001,
                                                 0.999)).rgb;
    float3 reflected_opposite =
        g_SceneColor.Sample(g_SamplerColor,
                            clamp(screen_uv - reflect_uv_offset * 0.42,
                                  0.001,
                                  0.999)).rgb;

    float3 shell_tint = float3(0.82, 0.92, 1.0);
    float3 highlight =
        shell_tint * (0.10 + edge * 0.42 + glint * fresnel * 0.20 + back_layer * 0.08);
    float3 reflection = lerp(reflected, reflected_opposite, 0.22);
    reflection = max(reflection, scene * 0.82);
    float3 color = lerp(scene * 1.02,
                        reflection + highlight,
                        0.25 + fresnel * 0.30 + back_layer * 0.08);
    float alpha = saturate((0.18 + fresnel * 0.36 + glint * edge * 0.08 +
                            back_layer * 0.12) *
                           g_BaseColorFactor.a);
    if (alpha <= 0.002)
    {
        discard;
    }
    return float4(color, alpha);
}
