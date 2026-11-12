#pragma once
// EDVRonFOOT scope zoom probe (scope_zoom_probe.h): the lens. One compute pass over the box around the lens, writing into the eye's own copy (the
// pass before it copied the whole region there). Each pixel p inside takes the eye at c + (p - c) / M -- offsets in the eye's tangent plane, which
// is linear in pixels -- with a bilinear sample clamped into the eye's region (a double-wide texture's other eye is never read). The lens is the
// weapon's glass (a convex polygon, scope_zoom_probe_core.h Glass; its last 2 pixels inside blend into the untouched eye, so the frame around it is
// never magnified) or, without one, a circle (its edge blends 2 pixels either side). Outside nothing is written. Compiled at build time
// (tools\temporal_shader_build, kScopeZoomLensBytecode).
namespace edvr {
constexpr char kScopeZoomLensCs[] = R"HLSL(
cbuffer Lens : register(b0) {
    float4 centreRadius;   // cx, cy, rx, ry: the zoom's centre and the circle's radii, in region pixels
    float4 params;         // 1/M, the soft edge in pixels, the glass's corner count (0: the circle), 0
    int4   box;            // x0, y0, x1, y1: the pixels this dispatch covers, in region pixels
    int4   origin;         // the region's first pixel in the source, and its size
    float4 texInv;         // 1 / the source's width and height
    float4 corner[8];      // the glass's corners, xy, in region pixels
};
Texture2D<float4>   Src : register(t0);
SamplerState        Lin : register(s0);
RWTexture2D<float4> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 p = int2(box.xy) + int2(id.xy);
    if (p.x >= box.z || p.y >= box.w) return;
    const float2 c = centreRadius.xy;
    const float2 at = float2(p) + 0.5;
    const int n = (int)params.z;
    float w;
    if (n > 0) {
        // Inside the glass by the least distance to an edge, each normal turned toward the centre.
        float inside = 1e30;
        [loop] for (int i = 0; i < n; ++i) {
            const float2 a = corner[i].xy, b = corner[(i + 1) % n].xy;
            float2 nrm = float2(-(b.y - a.y), b.x - a.x);
            const float len = length(nrm);
            if (len <= 0.0) continue;
            nrm /= len;
            if (dot(c - a, nrm) < 0.0) nrm = -nrm;
            inside = min(inside, dot(at - a, nrm));
        }
        if (inside <= 0.0) return;
        w = saturate(inside / params.y);
    } else {
        const float2 r = centreRadius.zw;
        const float k = length((at - c) / r);
        const float inside = (1.0 - k) * min(r.x, r.y);
        if (inside <= -params.y) return;
        w = saturate(inside / params.y * 0.5 + 0.5);
    }
    float2 q = c + (at - c) * params.x;
    q = clamp(q, float2(0.5, 0.5), float2(origin.zw) - 0.5);
    const float4 zoomed = Src.SampleLevel(Lin, (q + float2(origin.xy)) * texInv.xy, 0);
    const float4 plain = Src.Load(int3(p + origin.xy, 0));
    Dst[p] = lerp(plain, zoomed, w);
}
)HLSL";
}  // namespace edvr
