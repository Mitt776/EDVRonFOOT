#pragma once
// EDVRonFOOT desktop eye view (desktop_view.h): the draw of the eye's picture into the game's window. One full-screen triangle over the
// viewport the placement chose (desktop_view_core.h place), a bilinear sample of the eye between the two corners uv (u0 v0 -> u1 v1, the
// shown part's top left and bottom right; flipped bounds stay flipped) and the colour path (desktop_view_core.h colorPlan): 0 copy, 1 the
// sRGB curve onto a linear eye, 2 the curve undone for a float window. Compiled at build time (tools\temporal_shader_build,
// kDesktopViewVsBytecode and kDesktopViewPsBytecode).
namespace edvr {
constexpr char kDesktopViewVsHlsl[] = R"HLSL(
cbuffer View : register(b0) {
    float4 uv;     // u0, v0, u1, v1
    float4 mode;   // x: the colour path
};
struct O { float4 p : SV_POSITION; float2 t : TEXCOORD0; };
O main(uint id : SV_VertexID) {
    float2 c = float2((id << 1) & 2, id & 2);
    O o;
    o.p = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.t = lerp(uv.xy, uv.zw, c);
    return o;
}
)HLSL";

constexpr char kDesktopViewPsHlsl[] = R"HLSL(
cbuffer View : register(b0) {
    float4 uv;
    float4 mode;
};
Texture2D<float4> Eye : register(t0);
SamplerState Lin : register(s0);
float3 encodeSrgb(float3 c) {
    c = saturate(c);
    return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}
float3 decodeSrgb(float3 c) {
    c = saturate(c);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
float4 main(float4 p : SV_POSITION, float2 t : TEXCOORD0) : SV_Target {
    float3 c = Eye.SampleLevel(Lin, t, 0).rgb;
    if (mode.x == 1) c = encodeSrgb(c);
    else if (mode.x == 2) c = decodeSrgb(c);
    return float4(c, 1);
}
)HLSL";
}  // namespace edvr
