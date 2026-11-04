#pragma once
// The Summary probe, the pure half (EDVRonFOOT, .49, openspec change on-foot-summary-menu, design decision 5; the project's
// docs\research\native-vr-on-foot.md, "Полёт .48"). TEMPORARY: removed whole after its flight.
//
// On .48 the TAB wheel's Summary (1730x973) came on the trader's 360 mesh the whole time and the trader's sheet was shown the whole
// 28 s, yet Dima saw the menu only a couple of seconds. Which of three: C1 the panel empties (the copy has nothing in it), C2 its
// content has no alpha (colour without alpha, dark), C3 the content comes another way (another texture of the same draw, another
// eye draw, or draws into the panel by shaders that are not the interface's). While the trader's sheet is shown: every 0.5 s the
// copy the sheet reads is read back and counted (pixelStats), every eye draw of the eye-panel shader is a combination (indices, ps,
// slots 0-3), every draw into the trader's panel is counted by its vertex shader. Plain C++ the rig (tools\on_foot_hud_test) drives.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace edvr {
namespace smp {

constexpr uint64_t kPeriodMs = 500;       // one copy line and one window line each
constexpr uint32_t kLinesPerShow = 240;   // past it, counted, not written
constexpr uint32_t kSampleStep = 4;       // every 4th pixel each way
constexpr uint32_t kRing = 3;             // staging copies, read without waiting

// The copy, RGBA8 (27/28; 87 BGRA reads the same for the counts), rows `pitch` bytes apart. A pixel is opaque with alpha above 8,
// colour without alpha with its largest colour above 8 and alpha 8 or less, empty with all four 8 or less.
struct PixelStats {
    uint32_t sampled = 0, opaque = 0, colourNoAlpha = 0, empty = 0;
    uint64_t alphaSum = 0, colourSum = 0;  // of the sampled: alpha, and the largest colour
};
inline PixelStats pixelStats(const uint8_t* data, uint32_t pitch, uint32_t w, uint32_t h, uint32_t step = kSampleStep) {
    PixelStats s;
    if (!data || !w || !h || !step || pitch < w * 4) return s;
    for (uint32_t y = 0; y < h; y += step) {
        const uint8_t* row = data + size_t(y) * pitch;
        for (uint32_t x = 0; x < w; x += step) {
            const uint8_t* p = row + size_t(x) * 4;
            uint8_t c = p[0] > p[1] ? p[0] : p[1];
            if (p[2] > c) c = p[2];
            const uint8_t a = p[3];
            ++s.sampled;
            s.alphaSum += a;
            s.colourSum += c;
            if (a > 8)
                ++s.opaque;
            else if (c > 8)
                ++s.colourNoAlpha;
            else
                ++s.empty;
        }
    }
    return s;
}
inline double pct(uint32_t part, uint32_t whole) { return whole ? 100.0 * part / whole : 0.0; }

// One eye draw of the eye-panel shader: its index count, pixel shader and slots 0-3 (0x0 and format 0 for an empty slot or not a
// 2D texture).
constexpr int kSlots = 4;
struct Combo {
    uint32_t indices = 0;
    uint64_t ps = 0;
    uint32_t w[kSlots] = {}, h[kSlots] = {}, fmt[kSlots] = {};
    bool same(const Combo& o) const {
        if (indices != o.indices || ps != o.ps) return false;
        for (int i = 0; i < kSlots; ++i)
            if (w[i] != o.w[i] || h[i] != o.h[i] || fmt[i] != o.fmt[i]) return false;
        return true;
    }
};
constexpr int kMaxCombos = 16;
constexpr int kMaxVs = 8;
// One show's tables: the combinations met (a 17th counted, not kept) and the window's counts; the vertex shaders drawing into the
// trader's panel and their counts in the window.
struct Tables {
    Combo combos[kMaxCombos];
    int comboCount = 0;
    uint32_t comboWindow[kMaxCombos] = {};
    uint32_t combosLost = 0;
    uint64_t vs[kMaxVs] = {};
    uint32_t vsWindow[kMaxVs] = {};
    int vsCount = 0;
    uint32_t vsLost = 0;
    // The combination's index, *fresh true when it is new this show; -1 when the table is full.
    int noteCombo(const Combo& c, bool* fresh) {
        if (fresh) *fresh = false;
        for (int i = 0; i < comboCount; ++i)
            if (combos[i].same(c)) {
                ++comboWindow[i];
                return i;
            }
        if (comboCount == kMaxCombos) {
            ++combosLost;
            return -1;
        }
        combos[comboCount] = c;
        comboWindow[comboCount] = 1;
        if (fresh) *fresh = true;
        return comboCount++;
    }
    void noteVs(uint64_t v) {
        for (int i = 0; i < vsCount; ++i)
            if (vs[i] == v) {
                ++vsWindow[i];
                return;
            }
        if (vsCount == kMaxVs) {
            ++vsLost;
            return;
        }
        vs[vsCount] = v;
        vsWindow[vsCount++] = 1;
    }
    void newWindow() {
        for (auto& n : comboWindow) n = 0;
        for (auto& n : vsWindow) n = 0;
        combosLost = vsLost = 0;
    }
    void clear() { *this = Tables{}; }
};

// The interface's three vertex shaders (on_foot_hud_core.h's kGuiVector/kGuiText/kGuiIcons), named in the window line.
inline const char* vsName(uint64_t v) {
    switch (v) {
        case 0x666EF0C4C616F67Eull: return "gui-vector";
        case 0x1012E00B3CB44469ull: return "gui-text";
        case 0xA3E5D3FCBC1165F8ull: return "gui-icons";
    }
    return nullptr;
}

// ---- the lines ----

inline int formatStart(uint32_t w, uint32_t h, uint32_t fmt, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "summary probe: the trader sheet is shown (%ux%u, format %u): every 0.5 s its copy read back, the eye-panel "
                         "draws and the draws into its panel counted (temporary, .49)",
                         w, h, fmt);
}

inline int formatCopy(uint64_t ms, uint32_t w, uint32_t h, const PixelStats& s, char* out, size_t cap) {
    const double meanA = s.sampled ? double(s.alphaSum) / s.sampled : 0.0;
    const double meanC = s.sampled ? double(s.colourSum) / s.sampled : 0.0;
    return std::snprintf(out, cap,
                         "summary probe: copy %ux%u +%llu ms: opaque %.1f%% colour-no-alpha %.1f%% empty %.1f%% mean-alpha %.1f "
                         "mean-colour %.1f (%u sampled)",
                         w, h, static_cast<unsigned long long>(ms), pct(s.opaque, s.sampled), pct(s.colourNoAlpha, s.sampled),
                         pct(s.empty, s.sampled), meanA, meanC, s.sampled);
}

inline int formatCombo(int index, const Combo& c, uint64_t ms, char* out, size_t cap) {
    int n = std::snprintf(out, cap, "summary probe: eye draw #%d at +%llu ms: vs A888D51024D9798E ps %016llX n=%u slots [", index,
                          static_cast<unsigned long long>(ms), static_cast<unsigned long long>(c.ps), c.indices);
    for (int i = 0; i < kSlots && n >= 0 && size_t(n) < cap; ++i) {
        if (c.w[i])
            n += std::snprintf(out + n, cap - size_t(n), "%s%d: %ux%u fmt %u", i ? ", " : "", i, c.w[i], c.h[i], c.fmt[i]);
        else
            n += std::snprintf(out + n, cap - size_t(n), "%s%d: -", i ? ", " : "", i);
    }
    if (n >= 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - size_t(n), "]");
    return n;
}

// The window: the eye draws by combination, the draws into the trader's panel by vertex shader; empty fields say none.
inline int formatWindow(uint64_t ms, const Tables& t, char* out, size_t cap) {
    int n = std::snprintf(out, cap, "summary probe: window +%llu ms: eye draws [", static_cast<unsigned long long>(ms));
    bool any = false;
    for (int i = 0; i < t.comboCount && n >= 0 && size_t(n) < cap; ++i) {
        if (!t.comboWindow[i]) continue;
        n += std::snprintf(out + n, cap - size_t(n), "%s#%d=%u", any ? ", " : "", i, t.comboWindow[i]);
        any = true;
    }
    if (t.combosLost && n >= 0 && size_t(n) < cap)
        n += std::snprintf(out + n, cap - size_t(n), "%slost=%u", any ? ", " : "", t.combosLost);
    if (n >= 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - size_t(n), "] into the panel [");
    any = false;
    for (int i = 0; i < t.vsCount && n >= 0 && size_t(n) < cap; ++i) {
        if (!t.vsWindow[i]) continue;
        const char* name = vsName(t.vs[i]);
        if (name)
            n += std::snprintf(out + n, cap - size_t(n), "%s%s=%u", any ? ", " : "", name, t.vsWindow[i]);
        else
            n += std::snprintf(out + n, cap - size_t(n), "%svs %016llX=%u", any ? ", " : "", static_cast<unsigned long long>(t.vs[i]),
                               t.vsWindow[i]);
        any = true;
    }
    if (t.vsLost && n >= 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - size_t(n), "%slost=%u", any ? ", " : "", t.vsLost);
    if (n >= 0 && size_t(n) < cap) n += std::snprintf(out + n, cap - size_t(n), "]");
    return n;
}

inline int formatEnd(uint64_t ms, uint32_t unwritten, uint32_t unread, char* out, size_t cap) {
    return std::snprintf(out, cap, "summary probe: the trader sheet stopped after %llu ms; lines unwritten %u, copies unread %u",
                         static_cast<unsigned long long>(ms), unwritten, unread);
}

}  // namespace smp
}  // namespace edvr
