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
// experimental.native_vr_desktop_view_fit: crop (the default; also empty or unknown), whole.
enum class Fit { Crop, Whole };

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
    if (equalsWord(raw, "crop") || blank(raw)) return Fit::Crop;
    if (valid) *valid = false;
    return Fit::Crop;
}
inline const char* viewName(View v) { return v == View::Right ? "right" : v == View::Left ? "left" : "off"; }
inline const char* fitName(Fit f) { return f == Fit::Whole ? "whole" : "crop"; }
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
    if (fit == Fit::Crop) {
        // The window is filled; the eye is cut to the window's shape around its centre.
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
    return std::snprintf(out, size,
        "desktop view: %s %s%s, presents %llu, drawn %llu, skipped no-eye %llu / stale %llu / failed %llu (last %s), window %ux%u, "
        "eye %ux%u, colour %s, %s.",
        viewName(view), fitName(fit), runtime ? "" : ", no runtime", (unsigned long long)w.presents, (unsigned long long)w.drawn,
        (unsigned long long)w.noEye, (unsigned long long)w.stale, (unsigned long long)w.failed, failureName(w.lastFailure),
        w.windowW, w.windowH, w.eyeW, w.eyeH, colorPathName(w.path), gpu);
}

}  // namespace edvr::dv
