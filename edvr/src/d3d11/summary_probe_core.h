#pragma once
// The info-panel watch, the pure half (EDVRonFOOT, .50, openspec change on-foot-summary-menu, design decision 6; the project's
// docs\research\native-vr-on-foot.md, "Перепись в кинотеатре"). TEMPORARY: removed whole after its flight.
//
// The TAB wheel's Summary is the info panel (1188x703 at HMD quality 0.75). The game draws it in its first frames and then only lays
// the finished panel into the picture -- in stereo it skips that, so EDVR's hold expires after 30 frames and the sheet goes. Does the
// panel keep its picture while the Summary is open, and is it cleared when the Summary closes? After the panel's last fresh draw
// the glue keeps a reference to its texture and every 0.5 s reads it back: up to 30 s, or until three reads in a row are empty.
// Plain C++ the rig (tools\on_foot_hud_test) drives.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace edvr {
namespace smp {

constexpr uint64_t kPeriodMs = 500;      // one read a period
constexpr uint64_t kWatchMs = 30000;     // after the last fresh draw
constexpr uint32_t kEmptyToStop = 3;     // empty reads in a row
constexpr uint32_t kSampleStep = 4;      // every 4th pixel each way
constexpr uint32_t kRing = 3;            // staging copies, read without waiting
constexpr uint32_t kLinesPerWatch = 120; // past it, counted, not written

// The copy, RGBA8 (27/28), rows `pitch` bytes apart. A pixel is opaque with alpha above 8, colour without alpha with its largest
// colour above 8 and alpha 8 or less, empty with all four 8 or less.
struct PixelStats {
    uint32_t sampled = 0, opaque = 0, colourNoAlpha = 0, empty = 0;
    uint64_t alphaSum = 0, colourSum = 0;
    // Empty as the watch reads it: nothing but empty pixels (the game cleared the panel).
    bool allEmpty() const { return sampled && empty == sampled; }
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

// One watch: started by a fresh draw (and restarted by each one: lastDraw moves), a read due every kPeriodMs, stopped after kWatchMs
// since the last fresh draw or kEmptyToStop empty reads in a row since it.
enum class WatchEnd : int { None = 0, Timeout = 1, Emptied = 2 };
struct Watch {
    bool active = false;
    uint64_t start = 0, lastDraw = 0, lastRead = 0;
    uint32_t emptyRun = 0, lines = 0, unwritten = 0;
    // A fresh draw of the panel: true when it starts a watch (the start line).
    bool drawn(uint64_t now) {
        const bool fresh = !active;
        if (fresh) {
            start = now;
            lastRead = 0;
            lines = unwritten = 0;
        }
        active = true;
        lastDraw = now;
        emptyRun = 0;
        return fresh;
    }
    bool due(uint64_t now) const { return active && (!lastRead || now - lastRead >= kPeriodMs); }
    void read(uint64_t now, bool empty) {
        lastRead = now;
        emptyRun = empty ? emptyRun + 1 : 0;
    }
    WatchEnd ended(uint64_t now) const {
        if (!active) return WatchEnd::None;
        if (emptyRun >= kEmptyToStop) return WatchEnd::Emptied;
        if (now - lastDraw >= kWatchMs) return WatchEnd::Timeout;
        return WatchEnd::None;
    }
    // A line within the watch's budget; past it counted.
    bool admit() {
        if (lines >= kLinesPerWatch) {
            ++unwritten;
            return false;
        }
        ++lines;
        return true;
    }
    void stop() { active = false; }
};

// ---- the lines ----

inline int formatWatchStart(uint32_t w, uint32_t h, uint32_t fmt, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "info watch: the info panel %ux%u (format %u) is drawn; after its last draw it is read back every 0.5 s, up to "
                         "30 s or 3 empty reads (temporary, .50)",
                         w, h, fmt);
}

// `sinceDraw` the ms since the panel's last fresh draw (the read's own moment), `shown` whether the info sheet was shown then.
inline int formatWatchRead(uint64_t sinceDraw, uint64_t sinceStart, bool shown, const PixelStats& s, char* out, size_t cap) {
    const double meanA = s.sampled ? double(s.alphaSum) / s.sampled : 0.0;
    return std::snprintf(out, cap,
                         "info watch: +%llu ms after its last draw (+%llu ms of the watch): opaque %.1f%% colour-no-alpha %.1f%% empty "
                         "%.1f%% mean-alpha %.1f, sheet %s",
                         static_cast<unsigned long long>(sinceDraw), static_cast<unsigned long long>(sinceStart),
                         pct(s.opaque, s.sampled), pct(s.colourNoAlpha, s.sampled), pct(s.empty, s.sampled), meanA,
                         shown ? "shown" : "not shown");
}

inline int formatWatchEnd(WatchEnd why, uint64_t sinceDraw, uint32_t unwritten, uint32_t unread, char* out, size_t cap) {
    return std::snprintf(out, cap, "info watch: ended %s, %llu ms after the panel's last draw; lines unwritten %u, copies unread %u",
                         why == WatchEnd::Emptied ? "(the panel is empty: 3 reads in a row)"
                                                  : why == WatchEnd::Timeout ? "(30 s without a draw)" : "(left the feet)",
                         static_cast<unsigned long long>(sinceDraw), unwritten, unread);
}

}  // namespace smp
}  // namespace edvr
