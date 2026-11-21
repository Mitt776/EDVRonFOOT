#pragma once
// EDVRonFOOT desktop eye view (desktop_view.h): the draw of the eye's picture into the game's window. One full-screen triangle over the
// viewport the placement chose (desktop_view_core.h fitPlacement). Each window pixel c (0..1) is a ray in the smoothed orientation S
// (desktop-eye-steady): its tangents lerp between win.xy (top left) and win.zw (bottom right), the matrix m (P^-1 * S, the identity
// without a turn) turns it into the picture's own orientation, and the picture's tangents (frame: left, right, up, down) and its bounds
// (u0, v0, u1, v1; a flipped axis stays flipped) give the texel; a bilinear sample. Without a turn this is the old linear mapping
// exactly (desktop_view_core.h mapPoint, the CPU twin the rig checks). Then the colour path (desktop_view_core.h colorPlan): 0 copy,
// 1 the sRGB curve onto a linear eye, 2 the curve undone for a float window. Compiled at build time (tools\temporal_shader_build,
// kDesktopViewVsBytecode and kDesktopViewPsBytecode).
namespace edvr {
constexpr char kDesktopViewVsHlsl[] = R"HLSL(
struct O { float4 p : SV_POSITION; float2 c : TEXCOORD0; };
O main(uint id : SV_VertexID) {
    float2 c = float2((id << 1) & 2, id & 2);
    O o;
    o.p = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.c = c;
    return o;
}
)HLSL";

constexpr char kDesktopViewPsHlsl[] = R"HLSL(
cbuffer View : register(b0) {
    float4 m0, m1, m2;   // rows of P^-1 * S (xyz)
    float4 win;          // the window's tangents: tx0, ty0 (top left), tx1, ty1 (bottom right)
    float4 frame;        // the picture's tangents: left, right, up, down
    float4 bounds;       // u0, v0, u1, v1
    float4 mode;         // x: the colour path
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
float4 main(float4 p : SV_POSITION, float2 c : TEXCOORD0) : SV_Target {
    float3 d = float3(lerp(win.xy, win.zw, c), -1);
    float3 r = float3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
    if (r.z > -1e-6) return float4(0, 0, 0, 1);
    float2 t = r.xy / -r.z;
    float2 f = float2((t.x - frame.x) / (frame.y - frame.x), (frame.z - t.y) / (frame.z - frame.w));
    float3 col = Eye.SampleLevel(Lin, lerp(bounds.xy, bounds.zw, f), 0).rgb;
    if (mode.x == 1) col = encodeSrgb(col);
    else if (mode.x == 2) col = decodeSrgb(col);
    return float4(col, 1);
}
)HLSL";
}  // namespace edvr
