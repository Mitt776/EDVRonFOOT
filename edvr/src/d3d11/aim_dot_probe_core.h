#pragma once
// The on-foot aim dot probe, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_dot, temporary; the project's
// docs\research\native-vr-on-foot.md, "Зонд точки прицела (.42)", and aim_dot_probe.h for the glue). Plain C++ the rig
// (tools\aim_dot_probe_test) drives directly: the key's reading, the atlas by its proportions, the dot's cell in pixels, the snapshot
// files' names and their limit, and the text of every line.
//
// WHAT .22 FOUND. In headset stereo on foot the game draws the small things of its interface -- the aim dot, the markers' icons -- into
// a hidden atlas (2768x2383, the cinema's 4089x3521; width over height 1.161 for both) with its interface shaders, then lays them into
// each eye as quads of 6 indices with vs B10B032B, each cutting its own piece of the atlas. The dot is the cell u 0.444..0.556,
// v 0.194..0.323; the markers' icons lie at u 0..0.167. The probe finds the atlas, snapshots it on the log mark (PAUSE, census) and
// tries making the dot's cell transparent before the eyes read it (erase): does the weapon's crosshair share the cell?
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace adp {

// experimental.native_vr_on_foot_dot: off, census, erase. Missing or empty is off; anything else is off too, with *valid false so the
// glue says so in its key line.
enum class Mode : int { Off = 0, Census = 1, Erase = 2 };
inline const char* modeName(Mode m) { return m == Mode::Census ? "census" : m == Mode::Erase ? "erase" : "off"; }
inline Mode parseMode(const char* text, bool* valid) {
    if (valid) *valid = true;
    if (!text) return Mode::Off;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    if (!n) return Mode::Off;
    auto is = [&](const char* word) {
        if (std::strlen(word) != n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (is("off")) return Mode::Off;
    if (is("census")) return Mode::Census;
    if (is("erase")) return Mode::Erase;
    if (valid) *valid = false;
    return Mode::Off;
}

// The atlas: width over height 1.161 +- 0.01, any size (it grows and shrinks with HMD quality). Its neighbours are the HUD's 1.665, the
// ship name's atlas 1.125 and the lift menu's 1.084 -- the nearest is 0.036 away.
constexpr double kAtlasRatio = 1.161;
constexpr double kAtlasTolerance = 0.01;
inline bool isAtlasShape(uint32_t w, uint32_t h) {
    if (!w || !h) return false;
    return std::fabs(double(w) / double(h) - kAtlasRatio) <= kAtlasTolerance + 1e-9;
}
// R8G8B8A8_TYPELESS (27) or UNORM (28): what the probe can erase (a zero texture of the same format, one CopySubresourceRegion).
inline bool atlasFormatOk(uint32_t format) { return format == 27 || format == 28; }

// The dot's cell in .22's fractions, in pixels of the atlas found: rounded outward (no edge of the dot left), held inside the atlas,
// never empty. x0, y0 inclusive; x1, y1 exclusive.
constexpr double kCellU0 = 0.444, kCellU1 = 0.556, kCellV0 = 0.194, kCellV1 = 0.323;
struct Cell {
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t w() const { return x1 - x0; }
    uint32_t h() const { return y1 - y0; }
};
inline void cellSpan(double f0, double f1, uint32_t size, uint32_t* a, uint32_t* b) {
    double lo = std::floor(f0 * size + 1e-9), hi = std::ceil(f1 * size - 1e-9);
    if (lo < 0) lo = 0;
    if (hi > size) hi = size;
    uint32_t l = uint32_t(lo), h = uint32_t(hi);
    if (h <= l) {  // a tiny atlas: one pixel at least, inside
        if (l >= size) l = size ? size - 1 : 0;
        h = l + 1;
    }
    *a = l;
    *b = h;
}
inline Cell cellOf(uint32_t w, uint32_t h) {
    Cell c;
    if (!w || !h) return c;
    cellSpan(kCellU0, kCellU1, w, &c.x0, &c.x1);
    cellSpan(kCellV0, kCellV1, h, &c.y0, &c.y1);
    return c;
}

// The snapshots: 12 a launch (24 files: the atlas and the cell 4x), named by their number. After the limit the line still comes,
// without files. The wait for a frame with the atlas: 2 s from the press.
constexpr int kMaxSnapshots = 12;
constexpr int kCellZoom = 4;
constexpr uint64_t kSnapshotWaitMs = 2000;
constexpr int kMaxGuiDraws = 256;
constexpr int kDrawsPerLine = 20;
constexpr uint64_t kSummaryMs = 30000;
inline bool snapshotFilesAllowed(int taken) { return taken < kMaxSnapshots; }
inline void atlasFileName(int n, uint32_t w, uint32_t h, char* out, size_t cap) { std::snprintf(out, cap, "%02d_atlas_%ux%u.bmp", n, w, h); }
inline void cellFileName(int n, char* out, size_t cap) { std::snprintf(out, cap, "%02d_cell.bmp", n); }

// The cell out of the atlas's pixels (w x h x 4, tight), each pixel kCellZoom times both ways: out is cell.w()*zoom x cell.h()*zoom x 4.
inline void zoomCell(const uint8_t* atlas, uint32_t w, const Cell& c, int zoom, uint8_t* out) {
    const uint32_t ow = c.w() * uint32_t(zoom);
    for (uint32_t y = 0; y < c.h() * uint32_t(zoom); ++y)
        for (uint32_t x = 0; x < ow; ++x)
            std::memcpy(out + (size_t(y) * ow + x) * 4, atlas + (size_t(c.y0 + y / zoom) * w + (c.x0 + x / zoom)) * 4, 4);
}

// A 32-bit BMP (BI_RGB, bottom-up rows) of R, G, B, A pixels (w x h x 4, tight), written as they are (.22's makeBmp32). `out` must
// hold bmpBytes(w, h).
constexpr uint32_t kBmpHeader = 14 + 40;
inline size_t bmpBytes(uint32_t w, uint32_t h) { return kBmpHeader + size_t(w) * h * 4; }
inline void makeBmp32(const uint8_t* rgba, uint32_t w, uint32_t h, uint8_t* out) {
    const uint32_t bytes = w * h * 4;
    std::memset(out, 0, kBmpHeader);
    auto w16 = [&](size_t at, uint16_t v) { std::memcpy(out + at, &v, 2); };
    auto w32 = [&](size_t at, uint32_t v) { std::memcpy(out + at, &v, 4); };
    out[0] = 'B';
    out[1] = 'M';
    w32(2, kBmpHeader + bytes);
    w32(10, kBmpHeader);
    w32(14, 40);
    w32(18, w);
    w32(22, h);
    w16(26, 1);
    w16(28, 32);
    w32(34, bytes);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* src = rgba + size_t(h - 1 - y) * w * 4;
        uint8_t* dst = out + kBmpHeader + size_t(y) * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
}

// One interface draw into the atlas, for the snapshot's list: the shader, the index count, the viewport and the scissor rectangle.
enum class Gui : int { Vector = 0, Text = 1, Icons = 2, Other = 3 };
inline const char* guiName(Gui g) { return g == Gui::Vector ? "vector" : g == Gui::Text ? "text" : g == Gui::Icons ? "icons" : "other"; }
struct GuiDraw {
    Gui kind = Gui::Other;
    uint32_t count = 0;
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;  // 0 x 0 when no viewport was bound
    int scL = 0, scT = 0, scR = 0, scB = 0;  // all 0 when no scissor rectangle was set
    bool hasScissor = false;
};

// -- the lines --
inline void formatKey(Mode m, bool valid, const char* text, char* out, size_t cap) {
    if (valid)
        std::snprintf(out, cap,
                      "aim dot: key %s (experimental.native_vr_on_foot_dot; temporary: off does nothing, census snapshots the atlas on "
                      "PAUSE, erase makes the aim dot's cell transparent; on foot, with native_vr_on_foot and the on-foot HUD on)",
                      modeName(m));
    else
        std::snprintf(out, cap, "aim dot: key off (experimental.native_vr_on_foot_dot = '%s' is not understood: off, census or erase)",
                      text ? text : "");
}
inline void formatWaitingForHud(char* out, size_t cap) {
    std::snprintf(out, cap,
                  "aim dot: waiting for the on-foot HUD census (native_vr_on_foot_hud is off or native_vr_on_foot is 0): the probe "
                  "does nothing while it is");
}
inline void formatAtlasFound(uint32_t w, uint32_t h, uint32_t format, const Cell& c, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: atlas found %ux%u (format %u), the dot's cell x %u..%u y %u..%u (%ux%u)", w, h, format, c.x0, c.x1,
                  c.y0, c.y1, c.w(), c.h());
}
inline void formatAtlasSize(uint32_t w0, uint32_t h0, uint32_t w, uint32_t h, const Cell& c, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: atlas size %ux%u -> %ux%u, the dot's cell x %u..%u y %u..%u (%ux%u)", w0, h0, w, h, c.x0, c.x1,
                  c.y0, c.y1, c.w(), c.h());
}
inline void formatInert(const char* why, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: inert: %s; the picture is as with off until the key changes", why);
}
// The snapshot's head line; its draws follow on lines of their own (formatDraws), kDrawsPerLine a line.
inline void formatSnapshot(int n, uint32_t w, uint32_t h, uint32_t quadsA, uint32_t quadsB, uint32_t targets, int draws,
                           const char* files, char* out, size_t cap) {
    std::snprintf(out, cap,
                  "aim dot: snapshot %d: atlas %ux%u, eye quads %u/%u (eye targets %u, in the order met), gui draws %d%s, files %s", n,
                  w, h, quadsA, quadsB, targets, draws, draws > kMaxGuiDraws ? " (the first 256 listed)" : "", files);
}
inline void formatDraws(int n, const GuiDraw* d, int from, int to, char* out, size_t cap) {
    int len = std::snprintf(out, cap, "aim dot: snapshot %d draws %d..%d: [", n, from, to - 1);
    for (int i = from; i < to && len > 0 && size_t(len) < cap; ++i) {
        const GuiDraw& g = d[i];
        int k;
        if (g.hasScissor)
            k = std::snprintf(out + len, cap - size_t(len), "%s%s n=%u vp=%d,%d,%d,%d sc=%d,%d,%d,%d", i > from ? ", " : "",
                              guiName(g.kind), g.count, g.vpX, g.vpY, g.vpW, g.vpH, g.scL, g.scT, g.scR, g.scB);
        else
            k = std::snprintf(out + len, cap - size_t(len), "%s%s n=%u vp=%d,%d,%d,%d sc=none", i > from ? ", " : "", guiName(g.kind),
                              g.count, g.vpX, g.vpY, g.vpW, g.vpH);
        if (k < 0) break;
        len += k;
    }
    if (len > 0 && size_t(len) + 2 <= cap) std::snprintf(out + len, cap - size_t(len), "]");
}
inline void formatSkipped(char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: snapshot skipped: no atlas within 2 s (not on foot, or the atlas was not drawn)");
}
inline void formatLimit(char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: snapshot limit: 12 snapshots (24 files) written this launch; the lines go on, without files");
}

struct Summary {
    uint64_t frames = 0, atlasFrames = 0, eyeQuads = 0, erased = 0, rewrites = 0, snapshots = 0, failures = 0;
    void clear() { *this = Summary{}; }
};
inline void formatSummary(Mode m, uint32_t w, uint32_t h, const Summary& s, char* out, size_t cap) {
    std::snprintf(out, cap,
                  "aim dot: summary (%s, 30 s on foot): atlas %ux%u, frames=%llu, atlas_frames=%llu, eye_quads=%llu, erased=%llu, "
                  "rewrites_after_erase=%llu, snapshots=%llu, failures=%llu",
                  modeName(m), w, h, (unsigned long long)s.frames, (unsigned long long)s.atlasFrames, (unsigned long long)s.eyeQuads,
                  (unsigned long long)s.erased, (unsigned long long)s.rewrites, (unsigned long long)s.snapshots,
                  (unsigned long long)s.failures);
}

}  // namespace adp
}  // namespace edvr
