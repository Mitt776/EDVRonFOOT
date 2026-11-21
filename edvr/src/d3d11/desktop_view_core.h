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
// experimental.native_vr_desktop_view_steady (desktop-eye-steady): on (the default; also empty or unknown), off.
inline bool parseSteady(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (equalsWord(raw, "off")) return false;
    if (equalsWord(raw, "on") || blank(raw)) return true;
    if (valid) *valid = false;
    return true;
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

// ---- Steady (openspec change desktop-eye-steady). The picture is turned towards a smoothed head orientation S at the draw: each window
// pixel's ray in S is turned into the picture's own orientation P and projected by the picture's angles (one sample, as before).
// Orientations are XrQuaternionf (x y z w): the eye's view space (x right, y up, looking down -z) into the tracking space.
struct Quat { double x = 0, y = 0, z = 0, w = 1; };
inline double dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat normalized(const Quat& q) {
    const double n = std::sqrt(dot(q, q));
    if (!(n > 1e-12) || !std::isfinite(n)) return {};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
inline Quat conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat multiply(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// The rotation angle (radians, 0..pi) between two orientations.
inline double angleBetween(const Quat& a, const Quat& b) {
    double d = std::fabs(dot(a, b));
    if (d > 1) d = 1;
    return 2 * std::acos(d);
}
// From a towards b by t (0..1) along the shorter arc.
inline Quat slerp(const Quat& a, Quat b, double t) {
    double d = dot(a, b);
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    // Exact slerp down to a few microradians: the limit pull must land on L.
    if (d > 1 - 1e-12) return normalized({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    const double th = std::acos(d), s = std::sin(th);
    const double ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    return normalized({a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb});
}
// A turn by angle (radians) about a unit axis.
inline Quat axisAngle(double ax, double ay, double az, double angle) {
    const double n = std::sqrt(ax * ax + ay * ay + az * az), s = std::sin(angle / 2);
    if (!(n > 0)) return {};
    return {ax / n * s, ay / n * s, az / n * s, std::cos(angle / 2)};
}
// Row-major 3x3 rotation of q.
inline void rotationMatrix(const Quat& q, double m[9]) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}

// The filter's constants (design.md decision 2; tuned by the flight's video, not keys). L: the most S may trail P.
constexpr double kSteadyMinCutoffHz = 0.4;
constexpr double kSteadyBeta = 3.0;          // per rad/s
constexpr double kSteadyDerivCutoffHz = 1.0;
constexpr double kSteadyLimitDeg = 2.0;
constexpr double kSteadyResetGapS = 0.25;
constexpr double kPi = 3.14159265358979323846;
inline double steadyLimitRad() { return kSteadyLimitDeg * kPi / 180; }

// The "1 euro" filter on the orientation: the cutoff rises with the (smoothed) turning speed, so slow sway is smoothed hard and a quick
// turn passes. S never trails P by more than L (a recentre, an origin change, any jump lands at once at L). The first frame and a gap
// longer than kSteadyResetGapS (loading, no eye) set S = P.
class SteadyFilter {
public:
    void reset() { valid_ = false; }
    bool valid() const { return valid_; }
    // t: seconds (any steady clock). Returns S. lag: the angle S trails P after this frame (radians); clamped: S was pulled to L.
    Quat update(const Quat& raw, double t, double* lag = nullptr, bool* clamped = nullptr) {
        const Quat p = normalized(raw);
        if (clamped) *clamped = false;
        const double dt = t - lastT_;
        if (!valid_ || !(dt > 0) || dt > kSteadyResetGapS || !std::isfinite(dt)) {
            valid_ = true; s_ = p; prev_ = p; speed_ = 0; lastT_ = t;
            if (lag) *lag = 0;
            return s_;
        }
        lastT_ = t;
        const double speed = angleBetween(prev_, p) / dt;
        prev_ = p;
        speed_ += (speed - speed_) * alpha(kSteadyDerivCutoffHz, dt);
        s_ = slerp(s_, p, alpha(kSteadyMinCutoffHz + kSteadyBeta * speed_, dt));
        double a = angleBetween(s_, p);
        const double limit = steadyLimitRad();
        if (a > limit) {
            s_ = slerp(s_, p, 1 - limit / a);
            a = angleBetween(s_, p);
            if (clamped) *clamped = true;
        }
        if (lag) *lag = a;
        return s_;
    }
private:
    static double alpha(double cutoffHz, double dt) {
        const double tau = 1 / (2 * kPi * cutoffHz);
        return 1 / (1 + tau / dt);
    }
    bool valid_ = false;
    Quat s_, prev_;
    double speed_ = 0, lastT_ = 0;
};

// The picture's angles as tangents (left, right, up, down: left and down negative). false: not a usable field.
inline bool fovTangents(const float fov[4], double t[4]) {
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(fov[i]) || std::fabs(fov[i]) >= 1.5) return false;
        t[i] = std::tan(double(fov[i]));
    }
    return t[1] - t[0] > 1e-4 && t[2] - t[3] > 1e-4;
}
// The steady margin (design.md decision 3) for a box of window tangents (tx0, ty0, tx1, ty1): a turn by up to L moves a point at radius
// rho no further than tan(L)*(1+rho^2) in tangents (first order; kMarginSafety for the second), so the box widened by that on every side
// holds every point the window can show. rho: the widened box's farthest corner (a fixed point, three rounds).
constexpr double kMarginSafety = 1.05;
inline double steadyMargin(const double box[4]) {
    const double k = std::tan(steadyLimitRad()) * kMarginSafety;
    double m = 0;
    for (int i = 0; i < 3; ++i) {
        double rho2 = 0;
        for (int c = 0; c < 4; ++c) {
            const double x = std::fabs(box[c & 1 ? 2 : 0]) + m, y = std::fabs(box[c & 2 ? 3 : 1]) + m;
            rho2 = x * x + y * y > rho2 ? x * x + y * y : rho2;
        }
        m = k * (1 + rho2);
    }
    return m;
}
// A rectangle of picture fractions (x0, y0, x1, y1; y down) as tangents (tx0, ty0, tx1, ty1; y up: ty0 is the top).
inline void fractionToTangent(const double f[4], const double fov[4], double t[4]) {
    t[0] = fov[0] + f[0] * (fov[1] - fov[0]); t[1] = fov[2] - f[1] * (fov[2] - fov[3]);
    t[2] = fov[0] + f[2] * (fov[1] - fov[0]); t[3] = fov[2] - f[3] * (fov[2] - fov[3]);
}
// The fraction rectangle r widened by the steady margin of its own tangents.
inline void widened(const double r[4], const double fov[4], double out[4]) {
    double t[4];
    fractionToTangent(r, fov, t);
    const double m = steadyMargin(t), mx = m / (fov[1] - fov[0]), my = m / (fov[2] - fov[3]);
    out[0] = r[0] - mx; out[1] = r[1] - my; out[2] = r[2] + mx; out[3] = r[3] + my;
}
// crop and zoom with steady on: crop's rectangle shrunk around its centre to the largest share (to 1/4096) whose widened rectangle stays
// inside the picture and, for zoom, meets no triangle of the mask; at least kZoomMinShare. whole, or no usable field: as without steady.
inline Placement steadyPlacement(Fit fit, double windowW, double windowH, double eyeW, double eyeH, const float* triangles,
                                 uint32_t vertexCount, const double fov[4], double* share = nullptr) {
    if (fit == Fit::Whole) { if (share) *share = 1; return place(fit, windowW, windowH, eyeW, eyeH); }
    Placement p = place(Fit::Crop, windowW, windowH, eyeW, eyeH);
    if (share) *share = 1;
    if (!p.ok) return p;
    const double cx = (double(p.src[0]) + p.src[2]) / 2, cy = (double(p.src[1]) + p.src[3]) / 2;
    const double hw = (double(p.src[2]) - p.src[0]) / 2, hh = (double(p.src[3]) - p.src[1]) / 2;
    const bool mask = fit == Fit::Zoom && triangles && vertexCount >= 3;
    auto fits = [&](double s) {
        const double r[4] = {cx - hw * s, cy - hh * s, cx + hw * s, cy + hh * s};
        double w[4];
        widened(r, fov, w);
        if (w[0] < 0 || w[1] < 0 || w[2] > 1 || w[3] > 1) return false;
        return !mask || !rectHitsMask(w, triangles, vertexCount);
    };
    double s = 1;
    if (!fits(1)) {
        double lo = kZoomMinShare, hi = 1;
        if (fits(lo)) {
            for (int i = 0; i < 12; ++i) { const double mid = (lo + hi) / 2; if (fits(mid)) lo = mid; else hi = mid; }
        }
        s = lo;
    }
    if (share) *share = s;
    p.src[0] = float(cx - hw * s); p.src[1] = float(cy - hh * s); p.src[2] = float(cx + hw * s); p.src[3] = float(cy + hh * s);
    return p;
}

// The placement a draw uses: steady's (crop or zoom with steady on and a usable field), else zoom's or the plain one, as before steady.
inline Placement fitPlacement(Fit fit, bool steady, double windowW, double windowH, double eyeW, double eyeH, const float* triangles,
                              uint32_t vertexCount, const double* fov, double* share = nullptr) {
    if (steady && fov && fit != Fit::Whole)
        return steadyPlacement(fit, windowW, windowH, eyeW, eyeH, triangles, vertexCount, fov, share);
    if (fit == Fit::Zoom) return zoomPlacement(windowW, windowH, eyeW, eyeH, triangles, vertexCount, share);
    if (share) *share = 1;
    return place(fit, windowW, windowH, eyeW, eyeH);
}

// What the shader is given (design.md decision 5): m, three rows of P^-1 * S (xyz, w unused); win, the window's tangents at its top
// left and bottom right; frame, the picture's tangents (left, right, up, down). Without a turn m is the identity; without a usable field
// win and frame are the fractions themselves (frame 0, 1, 0, -1), which is the old linear mapping exactly.
struct Projection {
    float m[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float win[4]{};
    float frame[4]{};
};
// fov: tangents, or null for none. turn: S and P apply (steady on, crop or zoom, a pose).
inline Projection projection(const float src[4], const double* fov, bool turn, const Quat& p, const Quat& s) {
    Projection out;
    if (fov) {
        const double f[4] = {src[0], src[1], src[2], src[3]};
        double t[4];
        fractionToTangent(f, fov, t);
        for (int i = 0; i < 4; ++i) { out.win[i] = float(t[i]); out.frame[i] = float(fov[i]); }
    } else {
        out.win[0] = src[0]; out.win[1] = -src[1]; out.win[2] = src[2]; out.win[3] = -src[3];
        out.frame[0] = 0; out.frame[1] = 1; out.frame[2] = 0; out.frame[3] = -1;
        turn = false;
    }
    if (turn) {
        double r[9];
        rotationMatrix(multiply(conjugate(normalized(p)), normalized(s)), r);
        for (int row = 0; row < 3; ++row)
            for (int c = 0; c < 3; ++c) out.m[row * 4 + c] = float(r[row * 3 + c]);
    }
    return out;
}
// The shader's mapping on the CPU (the rig checks it): the window point c (0..1, 0,0 its top left) to the picture's fractions f and to
// the texture's uv through the bounds. false: the ray falls behind the picture (only for a turn near 90 degrees).
inline bool mapPoint(const Projection& pr, const float bounds[4], double cx, double cy, double f[2], double uv[2]) {
    const double tx = pr.win[0] + (double(pr.win[2]) - pr.win[0]) * cx, ty = pr.win[1] + (double(pr.win[3]) - pr.win[1]) * cy;
    const double d[3] = {tx, ty, -1};
    double r[3];
    for (int i = 0; i < 3; ++i) r[i] = pr.m[i * 4] * d[0] + pr.m[i * 4 + 1] * d[1] + pr.m[i * 4 + 2] * d[2];
    if (!(r[2] < -1e-6)) return false;
    const double px = r[0] / -r[2], py = r[1] / -r[2];
    f[0] = (px - pr.frame[0]) / (double(pr.frame[1]) - pr.frame[0]);
    f[1] = (pr.frame[2] - py) / (double(pr.frame[2]) - pr.frame[3]);
    uv[0] = bounds[0] + f[0] * (double(bounds[2]) - bounds[0]);
    uv[1] = bounds[1] + f[1] * (double(bounds[3]) - bounds[1]);
    return true;
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
    // steady: the lag of S behind P per turned frame (degrees), the frames pulled to the limit, the frames drawn without a pose.
    double lagSumDeg = 0, lagMaxDeg = 0;
    uint64_t lagSamples = 0, clamped = 0, unposed = 0;
    void noteGpu(double ms) {
        if (!(ms >= 0) || !std::isfinite(ms)) return;
        gpuSumMs += ms; ++gpuSamples;
        if (ms > gpuMaxMs) gpuMaxMs = ms;
    }
    void noteLag(double deg, bool wasClamped) {
        if (!(deg >= 0) || !std::isfinite(deg)) return;
        lagSumDeg += deg; ++lagSamples;
        if (deg > lagMaxDeg) lagMaxDeg = deg;
        if (wasClamped) ++clamped;
    }
};
// The line's steady tail: "steady on, lag mean M max X deg, clamped N" (and ", no pose K" when frames had none), "steady off", or for
// whole "steady off (whole)".
inline int formatSteady(char* out, size_t size, bool steady, Fit fit, const Window& w) {
    if (!out || !size) return 0;
    if (!steady) return std::snprintf(out, size, "steady off");
    if (fit == Fit::Whole) return std::snprintf(out, size, "steady off (whole)");
    char pose[48] = "";
    if (w.unposed) std::snprintf(pose, sizeof(pose), ", no pose %llu", (unsigned long long)w.unposed);
    return std::snprintf(out, size, "steady on, lag mean %.2f max %.2f deg, clamped %llu%s",
                         w.lagSamples ? w.lagSumDeg / double(w.lagSamples) : 0.0, w.lagMaxDeg, (unsigned long long)w.clamped, pose);
}
// runtime: the runtime acquired the table (false: an openvr_api.dll from another build, the view cannot run).
inline int formatLine(char* out, size_t size, View view, Fit fit, bool runtime, const Window& w, bool steady = false) {
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
    else if (fit == Fit::Crop && w.zoomShare < 1 && w.zoomShare > 0)
        std::snprintf(fitText, sizeof(fitText), "crop x%.3f", 1.0 / w.zoomShare);   // steady's margin
    else std::snprintf(fitText, sizeof(fitText), "%s", fitName(fit));
    char steadyText[128];
    formatSteady(steadyText, sizeof(steadyText), steady, fit, w);
    return std::snprintf(out, size,
        "desktop view: %s %s%s, presents %llu, drawn %llu, skipped no-eye %llu / stale %llu / failed %llu (last %s), window %ux%u, "
        "eye %ux%u, colour %s, %s, %s.",
        viewName(view), fitText, runtime ? "" : ", no runtime", (unsigned long long)w.presents, (unsigned long long)w.drawn,
        (unsigned long long)w.noEye, (unsigned long long)w.stale, (unsigned long long)w.failed, failureName(w.lastFailure),
        w.windowW, w.windowH, w.eyeW, w.eyeH, colorPathName(w.path), gpu, steadyText);
}

}  // namespace edvr::dv
