#pragma once
// EDVRonFOOT scope zoom (scope_zoom.h): the lens. One compute pass over the box around the lens, writing into the eye's own copy (the
// pass before it copied the whole region there). Each pixel p inside takes the eye at c + (p - c) / M -- offsets in the eye's tangent plane, which
// is linear in pixels -- with a bilinear sample clamped into the eye's region (a double-wide texture's other eye is never read). The lens is the
// weapon's glass (a convex polygon, scope_zoom_core.h Glass; its last 2 pixels inside blend into the untouched eye, so the frame around it is
// never magnified) or, without one, a circle (its edge blends 2 pixels either side). Outside nothing is written. Compiled at build time
// (tools\temporal_shader_build, kScopeZoomLensBytecode).
//
// The sharp lens (openspec change on-foot-scope-sharpness): when params.w is 1 the point c + (p - c) / M is sampled from the glass's gathered
// image instead (t1: DLSS's output over the area `sharp` names, S times the eye's pixels), wherever it falls inside that area; the eye stays the
// source outside it (never for a point of the glass -- scope_zoom_core.h sharpAreaFor -- but the clamp costs nothing). The area's output covers
// its input exactly, so the sample is (q - corner) / size across it, whatever S is; a zoom above S stretches the gathered image by M / S.
//
// The area's motion vectors (kScopeZoomMvCs): the glass's DLSS reads the eye's own vectors, shifted by the area's move since the last frame
// (scope_zoom_core.h sharpMvShift), into an area-sized texture of its own. The eye's vectors are only read.
namespace edvr {
constexpr char kScopeZoomLensCs[] = R"HLSL(
cbuffer Lens : register(b0) {
    float4 centreRadius;   // cx, cy, rx, ry: the zoom's centre and the circle's radii, in region pixels
    float4 params;         // 1/M, the soft edge in pixels, the glass's corner count (0: the circle), 1 to sample the gathered image
    int4   box;            // x0, y0, x1, y1: the pixels this dispatch covers, in region pixels
    int4   origin;         // the region's first pixel in the source, and its size
    float4 texInv;         // 1 / the source's width and height
    float4 corner[8];      // the glass's corners, xy, in region pixels
    float4 sharp;          // the gathered area's first pixel (x, y) and 1 / its size (w, h), in region pixels
};
Texture2D<float4>   Src : register(t0);
Texture2D<float4>   Sharp : register(t1);
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
    float4 zoomed;
    const float2 uv = (q - sharp.xy) * sharp.zw;
    if (params.w > 0.5 && all(uv > 0.0) && all(uv < 1.0))
        zoomed = Sharp.SampleLevel(Lin, uv, 0);
    else
        zoomed = Src.SampleLevel(Lin, (q + float2(origin.xy)) * texInv.xy, 0);
    const float4 plain = Src.Load(int3(p + origin.xy, 0));
    Dst[p] = lerp(plain, zoomed, w);
}
)HLSL";

constexpr char kScopeZoomMvCs[] = R"HLSL(
cbuffer Area : register(b0) {
    int4   area;    // the area's first pixel in the eye's vectors (x, y) and its size (w, h)
    float4 shift;   // the area's move since the last frame (dx, dy), added to every vector
    int4   limit;   // the eye's vectors' size (w, h), the reads clamped inside
};
Texture2D<float2>   Src : register(t0);
RWTexture2D<float2> Dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)area.z || id.y >= (uint)area.w) return;
    const int2 p = clamp(area.xy + int2(id.xy), int2(0, 0), limit.xy - 1);
    Dst[id.xy] = Src.Load(int3(p, 0)) + shift.xy;
}
)HLSL";
}  // namespace edvr
