#pragma once
// EDVRonFOOT desktop eye view (openspec change desktop-eye-mirror; desktop_view.h): the pure half. The keys' reading, where the eye's
// picture lands in the game's window (crop or whole), which colour path a pair of formats takes, whether this Present draws the eye
// it holds or skips it and why, and the 30 s log line. No D3D calls: tools\desktop_view_test drives every function here.
#include <dxgiformat.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr::dv {

// experimental.native_vr_desktop_view: off (the default; also empty or unknown), right, left.
enum class View { Off, Right, Left };
// experimental.native_vr_desktop_view_fit: crop (the default; also empty or unknown), zoom (crop enlarged just enough that no triangle
// of the lens mask -- the corners the game does not draw -- is in the window), whole.
enum class Fit { Crop, Whole, Zoom };

inline bool equalsWord(const char* raw, const char* word) {
    if (!raw) return false;
    while (*raw && std::isspace(static_cast<unsigned char>(*raw))) ++raw;
    const char* end = raw + std::strlen(raw);
    while (end > raw && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
    const size_t n = std::strlen(word);
    if (size_t(end - raw) != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(raw[i])) != word[i]) return false;
    return true;
}
inline bool blank(const char* raw) {
    if (!raw) return true;
    for (; *raw; ++raw) if (!std::isspace(static_cast<unsigned char>(*raw))) return false;
    return true;
}
// valid: false for a value that is neither blank nor one of the words (it reads as the default, and the log says so once).
inline View parseView(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (equalsWord(raw, "right")) return View::Right;
    if (equalsWord(raw, "left")) return View::Left;
    if (equalsWord(raw, "off") || blank(raw)) return View::Off;
    if (valid) *valid = false;
    return View::Off;
}
inline Fit parseFit(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (equalsWord(raw, "whole")) return Fit::Whole;
    if (equalsWord(raw, "zoom")) return Fit::Zoom;
    if (equalsWord(raw, "crop") || blank(raw)) return Fit::Crop;
    if (valid) *valid = false;
    return Fit::Crop;
}
inline const char* viewName(View v) { return v == View::Right ? "right" : v == View::Left ? "left" : "off"; }
inline const char* fitName(Fit f) { return f == Fit::Whole ? "whole" : f == Fit::Zoom ? "zoom" : "crop"; }
// The OpenVR eye index the view wants (0 left, 1 right); -1 for off.
inline int viewEye(View v) { return v == View::Right ? 1 : v == View::Left ? 0 : -1; }

// Where the eye's picture goes. dst: the window's pixels it covers (x, y, width, height; the draw's viewport). src: the part of the eye's
// picture shown, as fractions of it (x0, y0, x1, y1; 0,0 is the picture's top left as the headset shows it). clear: the window is
// cleared black first (whole, when the picture leaves bars). ok false: a size is zero or not finite, nothing is drawn.
struct Placement {
    float dst[4]{};
    float src[4]{};
    bool clear = false;
    bool ok = false;
};
inline Placement place(Fit fit, double windowW, double windowH, double eyeW, double eyeH) {
    Placement p;
    if (!(windowW >= 1 && windowH >= 1 && eyeW >= 1 && eyeH >= 1) || !std::isfinite(windowW + windowH + eyeW + eyeH)) return p;
    const double windowAspect = windowW / windowH, eyeAspect = eyeW / eyeH;
    if (fit != Fit::Whole) {
        // The window is filled; the eye is cut to the window's shape around its centre (zoom starts here: zoomPlacement).
        p.dst[0] = 0; p.dst[1] = 0; p.dst[2] = float(windowW); p.dst[3] = float(windowH);
        if (eyeAspect > windowAspect) {   // the eye is wider: cut its sides
            const double keep = windowAspect / eyeAspect;
            p.src[0] = float(0.5 - keep / 2); p.src[1] = 0; p.src[2] = float(0.5 + keep / 2); p.src[3] = 1;
        } else {                          // the eye is taller (the Quest 3's 2688x2944 in 16:9): cut top and bottom
            const double keep = eyeAspect / windowAspect;
            p.src[0] = 0; p.src[1] = float(0.5 - keep / 2); p.src[2] = 1; p.src[3] = float(0.5 + keep / 2);
        }
    } else {
        // All of the eye; black bars where the shapes differ. Whole pixels, so the picture's edge is not smeared.
        p.src[0] = 0; p.src[1] = 0; p.src[2] = 1; p.src[3] = 1;
        double w = windowW, h = windowH;
        if (eyeAspect > windowAspect) h = std::floor(windowW / eyeAspect + 0.5);
        else w = std::floor(windowH * eyeAspect + 0.5);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        const double x = std::floor((windowW - w) / 2), y = std::floor((windowH - h) / 2);
        p.dst[0] = float(x); p.dst[1] = float(y); p.dst[2] = float(w); p.dst[3] = float(h);
        p.clear = w < windowW || h < windowH;
    }
    p.ok = true;
    return p;
}

// The lens mask (zoom). Triangles in fractions of the eye's picture (x right, y down, 0..1: the hidden area mesh the runtime gives the
// game, which the game leaves black), three vertices each, xy pairs. rectHitsTriangle: the rectangle (x0, y0, x1, y1) and the triangle
// share more than an edge (separating axes: the rectangle's two and the triangle's three edge normals, a hair of tolerance).
inline bool rectHitsTriangle(const double r[4], const float* t) {
    constexpr double eps = 1e-7;
    double minX = t[0], maxX = t[0], minY = t[1], maxY = t[1];
    for (int i = 1; i < 3; ++i) {
        minX = t[2 * i] < minX ? t[2 * i] : minX; maxX = t[2 * i] > maxX ? t[2 * i] : maxX;
        minY = t[2 * i + 1] < minY ? t[2 * i + 1] : minY; maxY = t[2 * i + 1] > maxY ? t[2 * i + 1] : maxY;
    }
    if (maxX <= r[0] + eps || minX >= r[2] - eps || maxY <= r[1] + eps || minY >= r[3] - eps) return false;
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const double nx = -(double(t[2 * j + 1]) - t[2 * i + 1]), ny = double(t[2 * j]) - t[2 * i];
        if (nx == 0 && ny == 0) return false;   // a degenerate triangle covers nothing
        double tMin = 1e300, tMax = -1e300;
        for (int k = 0; k < 3; ++k) {
            const double d = nx * t[2 * k] + ny * t[2 * k + 1];
            tMin = d < tMin ? d : tMin; tMax = d > tMax ? d : tMax;
        }
        const double corners[4][2] = {{r[0], r[1]}, {r[2], r[1]}, {r[0], r[3]}, {r[2], r[3]}};
        double rMin = 1e300, rMax = -1e300;
        for (const auto& c : corners) {
            const double d = nx * c[0] + ny * c[1];
            rMin = d < rMin ? d : rMin; rMax = d > rMax ? d : rMax;
        }
        const double tol = eps * std::sqrt(nx * nx + ny * ny);
        if (tMax <= rMin + tol || rMax <= tMin + tol) return false;
    }
    return true;
}
inline bool rectHitsMask(const double r[4], const float* triangles, uint32_t vertexCount) {
    for (uint32_t i = 0; i + 2 < vertexCount; i += 3)
        if (rectHitsTriangle(r, triangles + 2 * i)) return true;
    return false;
}
// The share of crop's rectangle zoom keeps around its centre: 1 with no mask in it, else the largest share (to 1/1024) whose rectangle
// meets no triangle, at least kZoomMinShare (a mask over the centre, which no real lens has, gives that and no more).
constexpr double kZoomMinShare = 0.5;
inline double zoomShare(const float src[4], const float* triangles, uint32_t vertexCount) {
    const double cx = (double(src[0]) + src[2]) / 2, cy = (double(src[1]) + src[3]) / 2;
    const double hw = (double(src[2]) - src[0]) / 2, hh = (double(src[3]) - src[1]) / 2;
    auto rect = [&](double s, double r[4]) { r[0] = cx - hw * s; r[1] = cy - hh * s; r[2] = cx + hw * s; r[3] = cy + hh * s; };
    double r[4];
    rect(1, r);
    if (!triangles || vertexCount < 3 || !rectHitsMask(r, triangles, vertexCount)) return 1;
    double lo = kZoomMinShare, hi = 1;
    rect(lo, r);
    if (rectHitsMask(r, triangles, vertexCount)) return lo;
    for (int i = 0; i < 10; ++i) {
        const double mid = (lo + hi) / 2;
        rect(mid, r);
        if (rectHitsMask(r, triangles, vertexCount)) hi = mid; else lo = mid;
    }
    return lo;
}
// zoom's placement: crop's, its picture part shrunk around the centre to the share above. share: out, what was kept (1 = crop's).
inline Placement zoomPlacement(double windowW, double windowH, double eyeW, double eyeH, const float* triangles, uint32_t vertexCount,
                               double* share = nullptr) {
    Placement p = place(Fit::Crop, windowW, windowH, eyeW, eyeH);
    const double s = p.ok ? zoomShare(p.src, triangles, vertexCount) : 1;
    if (share) *share = s;
    if (!p.ok || s >= 1) return p;
    const double cx = (double(p.src[0]) + p.src[2]) / 2, cy = (double(p.src[1]) + p.src[3]) / 2;
    const double hw = (double(p.src[2]) - p.src[0]) / 2 * s, hh = (double(p.src[3]) - p.src[1]) / 2 * s;
    p.src[0] = float(cx - hw); p.src[1] = float(cy - hh); p.src[2] = float(cx + hw); p.src[3] = float(cy + hh);
    return p;
}

// The texture coordinates of the shown part: src (fractions of the picture) through the eye's bounds (OpenVR's uMin, vMin, uMax,
// vMax; a flipped axis has min > max and stays flipped). out: u0, v0 (the shown part's top left), u1, v1 (its bottom right).
inline void sourceUv(const float src[4], const float bounds[4], float out[4]) {
    const float du = bounds[2] - bounds[0], dv = bounds[3] - bounds[1];
    out[0] = bounds[0] + src[0] * du; out[1] = bounds[1] + src[1] * dv;
    out[2] = bounds[0] + src[2] * du; out[3] = bounds[1] + src[3] * dv;
}
// The eye picture's own size in pixels: the bounds' span of the texture.
inline void eyeSize(uint32_t textureW, uint32_t textureH, const float bounds[4], uint32_t& w, uint32_t& h) {
    w = uint32_t(std::lround(textureW * std::fabs(bounds[2] - bounds[0])));
    h = uint32_t(std::lround(textureH * std::fabs(bounds[3] - bounds[1])));
}
inline bool validBounds(const float b[4]) {
    for (int i = 0; i < 4; ++i) if (!std::isfinite(b[i]) || b[i] < 0 || b[i] > 1) return false;
    return b[0] != b[2] && b[1] != b[3];
}

// Formats. A format's view: the typed format read (or written) without sRGB conversion, so the bytes are taken as they are; linear: the
// values are linear light (float formats), else they are already encoded for a monitor (8- and 10-bit). ok false: not a format this draws.
struct FormatClass {
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    bool linear = false;
    bool ok = false;
};
inline FormatClass classify(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return {DXGI_FORMAT_R8G8B8A8_UNORM, false, true};
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return {DXGI_FORMAT_B8G8R8A8_UNORM, false, true};
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return {DXGI_FORMAT_B8G8R8X8_UNORM, false, true};
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return {DXGI_FORMAT_R10G10B10A2_UNORM, false, true};
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return {DXGI_FORMAT_R16G16B16A16_FLOAT, true, true};
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return {DXGI_FORMAT_R11G11B10_FLOAT, true, true};
    default:
        return {};
    }
}
// The colour path from the eye to the window: copy (both encoded or both linear), encode (a linear eye into an encoded window: the
// sRGB curve in the shader), decode (an encoded eye into a float window), unsupported (a format neither side knows: the frame is skipped).
enum class ColorPath { Copy = 0, Encode = 1, Decode = 2, Unsupported = 3 };
struct ColorPlan {
    DXGI_FORMAT srv = DXGI_FORMAT_UNKNOWN, rtv = DXGI_FORMAT_UNKNOWN;
    ColorPath path = ColorPath::Unsupported;
};
inline ColorPlan colorPlan(DXGI_FORMAT eye, DXGI_FORMAT window) {
    const FormatClass e = classify(eye), w = classify(window);
    ColorPlan plan;
    if (!e.ok || !w.ok) return plan;
    plan.srv = e.view; plan.rtv = w.view;
    plan.path = e.linear == w.linear ? ColorPath::Copy : e.linear ? ColorPath::Encode : ColorPath::Decode;
    return plan;
}
inline const char* colorPathName(ColorPath p) {
    return p == ColorPath::Copy ? "copy" : p == ColorPath::Encode ? "encode" : p == ColorPath::Decode ? "decode" : "unsupported";
}

// One Present's verdict on the eye it holds. The runtime notes the wanted eye after its last treatment (after the menu), during the
// game's Submit; the Present that follows draws it. NoEye: nothing noted since the last Present (loading, the eye not submitted, the
// view switched on a moment ago). Stale: the noted eye is a frame already drawn (its sequence is not newer). Never an older picture.
enum class Verdict { Draw, NoEye, Stale };
inline Verdict decide(bool held, uint64_t heldPresentIndex, uint64_t presentIndex, uint64_t heldSequence, uint64_t lastDrawnSequence) {
    if (!held || heldPresentIndex != presentIndex) return Verdict::NoEye;
    if (!heldSequence || heldSequence <= lastDrawnSequence) return Verdict::Stale;
    return Verdict::Draw;
}

// Why a held eye could not be drawn, the last one named in the log line.
enum class Failure { None, Device, Format, Bind, Views, Shaders, Window };
inline const char* failureName(Failure f) {
    switch (f) {
    case Failure::Device: return "device";
    case Failure::Format: return "format";
    case Failure::Bind: return "bind";
    case Failure::Views: return "views";
    case Failure::Shaders: return "shaders";
    case Failure::Window: return "window";
    default: return "none";
    }
}

// The 30 s window's counts, and the line made of them.
struct Window {
    uint64_t presents = 0, drawn = 0, noEye = 0, stale = 0, failed = 0;
    Failure lastFailure = Failure::None;
    uint32_t windowW = 0, windowH = 0, eyeW = 0, eyeH = 0;
    ColorPath path = ColorPath::Unsupported;
    double gpuSumMs = 0, gpuMaxMs = 0;
    uint64_t gpuSamples = 0;
    double zoomShare = 1;          // zoom: the share of crop kept (the last draw's)
    uint32_t maskTriangles = 0;    // zoom: the lens mask's triangles for the drawn eye
    void noteGpu(double ms) {
        if (!(ms >= 0) || !std::isfinite(ms)) return;
        gpuSumMs += ms; ++gpuSamples;
        if (ms > gpuMaxMs) gpuMaxMs = ms;
    }
};
// runtime: the runtime acquired the table (false: an openvr_api.dll from another build, the view cannot run).
inline int formatLine(char* out, size_t size, View view, Fit fit, bool runtime, const Window& w) {
    if (!out || !size) return 0;
    if (view == View::Off)
        return std::snprintf(out, size, "desktop view: off (experimental.native_vr_desktop_view), presents %llu, no GPU work.",
                             (unsigned long long)w.presents);
    char gpu[64];
    if (w.gpuSamples) std::snprintf(gpu, sizeof(gpu), "gpu mean %.3f ms max %.3f ms", w.gpuSumMs / double(w.gpuSamples), w.gpuMaxMs);
    else std::snprintf(gpu, sizeof(gpu), "gpu n/a");
    char fitText[96];
    if (fit == Fit::Zoom)
        std::snprintf(fitText, sizeof(fitText), "zoom x%.3f (mask %u triangles)", w.zoomShare > 0 ? 1.0 / w.zoomShare : 1.0,
                      w.maskTriangles);
    else std::snprintf(fitText, sizeof(fitText), "%s", fitName(fit));
    return std::snprintf(out, size,
        "desktop view: %s %s%s, presents %llu, drawn %llu, skipped no-eye %llu / stale %llu / failed %llu (last %s), window %ux%u, "
        "eye %ux%u, colour %s, %s.",
        viewName(view), fitText, runtime ? "" : ", no runtime", (unsigned long long)w.presents, (unsigned long long)w.drawn,
        (unsigned long long)w.noEye, (unsigned long long)w.stale, (unsigned long long)w.failed, failureName(w.lastFailure),
        w.windowW, w.windowH, w.eyeW, w.eyeH, colorPathName(w.path), gpu);
}

}  // namespace edvr::dv
