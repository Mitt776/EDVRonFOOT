#pragma once
// EDVRonFOOT scope zoom probe (scope_zoom_probe.h): the lens. One compute pass over the box around the circle, writing into the eye's own copy
// (the pass before it copied the whole region there). Each pixel p of the circle takes the eye at c + (p - c) / M -- offsets in the eye's
// tangent plane, which is linear in pixels -- with a bilinear sample clamped into the eye's region (a double-wide texture's other eye is never
// read); the circle's last 2 pixels blend into the untouched eye. Outside the circle nothing is written. Compiled at build time
// (tools\temporal_shader_build, kScopeZoomLensBytecode).
namespace edvr {
constexpr char kScopeZoomLensCs[] = R"HLSL(
cbuffer Lens : register(b0) {
    float4 centreRadius;   // cx, cy, rx, ry: the circle, in region pixels
    float4 params;         // 1/M, the soft edge in pixels, 0, 0
    int4   box;            // x0, y0, x1, y1: the pixels this dispatch covers, in region pixels
    int4   origin;         // the region's first pixel in the source, and its size
    float4 texInv;         // 1 / the source's width and height
};
Texture2D<float4>   Src : register(t0);
SamplerState        Lin : register(s0);
RWTexture2D<float4> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 p = int2(box.xy) + int2(id.xy);
    if (p.x >= box.z || p.y >= box.w) return;
    const float2 c = centreRadius.xy, r = centreRadius.zw;
    const float2 at = float2(p) + 0.5;
    const float2 d = (at - c) / r;
    const float k = length(d);
    const float rmin = min(r.x, r.y);
    // Positive inside the circle, in pixels from its edge.
    const float inside = (1.0 - k) * rmin;
    if (inside <= -params.y) return;
    float2 q = c + (at - c) * params.x;
    q = clamp(q, float2(0.5, 0.5), float2(origin.zw) - 0.5);
    const float4 zoomed = Src.SampleLevel(Lin, (q + float2(origin.xy)) * texInv.xy, 0);
    const float4 plain = Src.Load(int3(p + origin.xy, 0));
    const float w = saturate(inside / params.y * 0.5 + 0.5);
    Dst[p] = lerp(plain, zoomed, w);
}
)HLSL";
}  // namespace edvr
