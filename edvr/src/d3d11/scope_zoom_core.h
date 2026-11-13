#pragma once
// The on-foot scope zoom, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_scope_zoom; openspec change on-foot-scope-zoom, and
// scope_zoom.h for the glue). Everything here is plain C++ the rig (tools\scope_zoom_test) drives directly: the key's reading, the zoom table,
// the weapon names out of Status.json and the journal's loadout, the aiming centre and the glass from the eye's tangents, the zoom from the game's
// FOV, the aim's and the summary's bookkeeping, the zoom's delay, the sway's correction, and the text of every line.
//
// WHAT IT IS FOR. In the flat game and in the cinema the game narrows its field of view while a weapon aims (LT); in stereo the lenses fix the
// field of view and the game cannot narrow it, so a scope shows the world 1:1. The lens puts the zoom back where it belongs: in the dominant eye
// only, inside the scope's glass, by the M the game's own FOV gives (measured by the probe scope-zoom-probe, three flights 13.11).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace scz {

// ---- the key ----------------------------------------------------------------------------------------------------------------------------------
// experimental.native_vr_on_foot_scope_zoom: on (the default), off. Empty and unknown -- the probe's census and preview among them -- read as on
// and are flagged (one line): a player who flew the probe has preview in the file and expects the lens.
enum class Key : int { Off = 0, On = 1 };
inline const char* keyName(Key k) { return k == Key::On ? "on" : "off"; }

// Case-insensitive, trimmed. *valid is false for empty and for anything that is neither word; a missing key (null) is on and valid.
inline Key parseKey(const char* text, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (!text) return Key::On;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    auto is = [&](const char* word) {
        if (std::strlen(word) != n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (n) {
        if (is("on")) return Key::On;
        if (is("off")) return Key::Off;
    }
    if (valid) *valid = false;
    return Key::On;
}

// ---- constants --------------------------------------------------------------------------------------------------------------------------------
constexpr float kRadiusDeg = 5.0f;     // the lens circle's radius around the aiming centre (design, decision 2)
constexpr float kFRefDeg = 56.3f;      // the game's default field of view, the reference the table's FOVs narrow from (decision 3)
constexpr float kEdgePx = 2.0f;        // the circle's soft edge
constexpr uint64_t kSummaryMs = 60000;      // the summary's period (said only for a window with aims in it)
constexpr float kLensMinM = 1.005f;         // below this the lens is not drawn (the game's FOV is not narrowed)

inline float degToRad(float d) { return d * 0.017453292519943295f; }
inline float radToDeg(float r) { return r * 57.29577951308232f; }

// ---- weapon names -----------------------------------------------------------------------------------------------------------------------------
// The internal name, lower case, as both sources give it: Status.json's SelectedWeapon is "$wpn_m_sniper_plasma_charged_name;", the journal's
// ModuleName is "wpn_m_sniper_plasma_charged". The leading '$', the trailing ';' and "_name" go; the rest is lowered. False for an empty result.
inline bool normaliseWeapon(const char* raw, size_t len, char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!raw) return false;
    size_t b = 0, e = len;
    while (b < e && (raw[b] == ' ' || raw[b] == '$')) ++b;
    while (e > b && (raw[e - 1] == ' ' || raw[e - 1] == ';')) --e;
    if (e - b >= 5) {
        const char* t = raw + e - 5;
        bool tail = true;
        const char* want = "_name";
        for (int i = 0; i < 5; ++i) {
            char c = t[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != want[i]) { tail = false; break; }
        }
        if (tail) e -= 5;
    }
    size_t n = 0;
    for (size_t i = b; i < e && n + 1 < cap; ++i) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        out[n++] = c;
    }
    out[n] = '\0';
    return n > 0;
}

// ---- the zoom table ---------------------------------------------------------------------------------------------------------------------------
// The weapons with a "smart" scope whose glass is a screen (Dima, 12.11): the Manticore Executioner, 35 degrees aiming without the Scope mod and
// 15 with it (the upstream's flat measurements), so M caps at 1.70 and 4.07. Everything else is not in the table: no lens (Dima, 13.11: only
// these).
constexpr const char* kExecutioner = "wpn_m_sniper_plasma_charged";
constexpr float kExecutionerAdsDeg = 35.0f;
constexpr float kExecutionerScopeAdsDeg = 15.0f;

// M = tan(F_ref / 2) / tan(F_ads / 2); 0 for a FOV that is not a usable angle.
inline float magnification(float adsDeg, float refDeg = kFRefDeg) {
    if (!(adsDeg > 0.0f && adsDeg < 179.0f) || !(refDeg > 0.0f && refDeg < 179.0f)) return 0.0f;
    return std::tan(degToRad(refDeg) * 0.5f) / std::tan(degToRad(adsDeg) * 0.5f);
}

// Whether a mods list (the journal's WeaponMods, comma-joined) carries the Scope mod: any entry containing "scope".
inline bool modsHaveScope(const char* mods) {
    if (!mods) return false;
    for (const char* p = mods; *p; ++p) {
        if ((p[0] == 's' || p[0] == 'S') && std::strlen(p) >= 5) {
            char w[6] = {};
            for (int i = 0; i < 5; ++i) w[i] = char(p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 'a' : p[i]);
            if (std::strcmp(w, "scope") == 0) return true;
        }
    }
    return false;
}

struct Zoom {
    bool inTable = false;
    float adsDeg = 0.0f;
    float m = 1.0f;
};
// The zoom for a normalised weapon name and its mods (null mods: none known).
inline Zoom zoomFor(const char* weapon, const char* mods) {
    Zoom z;
    if (!weapon || std::strcmp(weapon, kExecutioner) != 0) return z;
    z.inTable = true;
    z.adsDeg = modsHaveScope(mods) ? kExecutionerScopeAdsDeg : kExecutionerAdsDeg;
    z.m = magnification(z.adsDeg);
    return z;
}

// The zoom the game itself is at: its camera's FOV (radians, read by the hook after its ADS step) against F_ref. The game ramps that FOV with the same
// aiming fraction that raises the weapon (flight 13.11: 56 -> 35 degrees over about 0.4 s, starting about 0.15 s after LT), so a lens driven by it
// grows exactly as the sights come to the eye -- the first preview ramped with the dominant eye (0.12 s from LT) and zoomed before the weapon was up.
// 1 for anything that is not a usable FOV, never below 1, never above `cap`.
inline float zoomFromFov(float fovRad, float cap, float refDeg = kFRefDeg) {
    if (!std::isfinite(fovRad) || !(fovRad > degToRad(1.0f)) || !(fovRad < degToRad(179.0f))) return 1.0f;
    const float m = std::tan(degToRad(refDeg) * 0.5f) / std::tan(fovRad * 0.5f);
    if (!(m > 1.0f)) return 1.0f;
    return cap >= 1.0f && m > cap ? cap : m;
}

// The zoom while the aiming blend (the dominant eye's ramp, 0..1) runs: 1 + (M - 1) * blend, without a jump at either end.
inline float rampedM(float m, float blend) {
    if (!(blend > 0.0f)) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;
    if (!(m >= 1.0f)) m = 1.0f;
    return 1.0f + (m - 1.0f) * blend;
}

// The zoom held back until the glass is at the eye (design, decision 4). Flight 1 of on-foot-scope-zoom (13.11, three PAUSE recordings): the game's
// FOV is full 385-410 ms after LT, the Executioner's glass lands 237-277 ms later. The lens takes the smaller of the zoom now and the zoom D ago, so
// on the way up it runs D late and on the way down it follows the FOV at once (a magnified glass on a weapon going away looks worse than a zoom
// gone a moment early). A gap in the samples (the lens was not asked for a while) starts the history again, so an old aim's zoom never comes back;
// until a sample D old exists the zoom is 1.
constexpr double kZoomDelayMs = 260.0;
constexpr double kZoomGapMs = 100.0;
struct ZoomDelay {
    static constexpr int kN = 128;   // 1.7 s at 75 samples a second, past D with room
    double t[kN] = {};
    float m[kN] = {};
    int head = 0, count = 0;         // the newest sample is at head - 1
};
inline float delayedZoom(ZoomDelay& z, float m, double nowMs, double delayMs = kZoomDelayMs) {
    if (!(m >= 1.0f)) m = 1.0f;
    if (z.count > 0) {
        const double last = z.t[(z.head + ZoomDelay::kN - 1) % ZoomDelay::kN];
        if (!(nowMs >= last) || nowMs - last > kZoomGapMs) z.count = 0;
    }
    z.t[z.head] = nowMs;
    z.m[z.head] = m;
    z.head = (z.head + 1) % ZoomDelay::kN;
    if (z.count < ZoomDelay::kN) ++z.count;
    // The newest sample at or before now - D.
    const double want = nowMs - delayMs;
    float then = 1.0f;
    for (int i = 1; i <= z.count; ++i) {
        const int k = (z.head + ZoomDelay::kN - i) % ZoomDelay::kN;
        if (z.t[k] <= want) { then = z.m[k]; break; }
    }
    return then < m ? then : m;
}

// The Executioner's glass swings further than the barrel's lag in a head turn and lifts with it: the game's own weapon sway (design, decision 5).
// Flight 1 (13.11, recording 224709, turns up to 50 degrees a second, lag up to 5.9 degrees): the glass moved 0.157 of a tangent more per tangent of
// yaw lag than the lens, and 0.137 of a tangent up; with both the error left is 5 px (sd) of 26.
constexpr float kLagYawGain = 1.157f;   // the lens's yaw tangent per tangent of the barrel's yaw lag
constexpr float kLagYawLift = -0.137f;  // the lens's down tangent per tangent of the barrel's yaw lag (negative: up as the lag grows right)

// ---- the journal's loadout --------------------------------------------------------------------------------------------------------------------
// SuitLoadout lists the loadout's modules ({"SlotName":"PrimaryWeapon1", "ModuleName":"wpn_...", "WeaponMods":["weapon_scope", ...]} each);
// LoadoutEquipModule carries one module with the same three keys at the top level. Weapon slots only; at most kSlots of them.
constexpr int kSlots = 4;
struct LoadoutSlot {
    char slot[24] = {};
    char weapon[48] = {};   // normalised
    char mods[96] = {};     // the mods' names, comma-joined; empty for none
};
struct Loadout {
    LoadoutSlot slots[kSlots];
    int count = 0;
    uint32_t updates = 0;   // SuitLoadout and LoadoutEquipModule lines taken
};

namespace detail {
// The string value of "key":"...", searched in [p, end); the value is copied (truncated to cap-1). Returns the position after the value or null.
inline const char* stringField(const char* p, const char* end, const char* key, char* out, size_t cap) {
    const size_t kl = std::strlen(key);
    for (const char* q = p; q + kl + 4 <= end; ++q) {
        if (q[0] != '"' || std::memcmp(q + 1, key, kl) != 0 || q[kl + 1] != '"' || q[kl + 2] != ':') continue;
        const char* v = q + kl + 3;
        while (v < end && *v == ' ') ++v;
        if (v >= end || *v != '"') continue;
        ++v;
        size_t n = 0;
        while (v < end && *v != '"') {
            if (n + 1 < cap) out[n++] = *v;
            ++v;
        }
        if (cap) out[n < cap ? n : cap - 1] = '\0';
        return v < end ? v + 1 : nullptr;
    }
    return nullptr;
}
// The WeaponMods array in [p, end), comma-joined into out (empty for []). False when the key is not there.
inline bool modsField(const char* p, const char* end, char* out, size_t cap) {
    const char* key = "\"WeaponMods\":";
    const size_t kl = std::strlen(key);
    for (const char* q = p; q + kl < end; ++q) {
        if (std::memcmp(q, key, kl) != 0) continue;
        const char* v = q + kl;
        while (v < end && *v == ' ') ++v;
        if (v >= end || *v != '[') return false;
        ++v;
        size_t n = 0;
        out[0] = '\0';
        while (v < end && *v != ']') {
            if (*v == '"') {
                ++v;
                if (n && n + 1 < cap) out[n++] = ',';
                while (v < end && *v != '"') {
                    if (n + 1 < cap) out[n++] = *v;
                    ++v;
                }
                if (v < end) ++v;
            } else {
                ++v;
            }
        }
        out[n] = '\0';
        return true;
    }
    return false;
}
// The first occurrence of `tok` in [p, end), or end.
inline const char* findToken(const char* p, const char* end, const char* tok) {
    const size_t tl = std::strlen(tok);
    for (const char* q = p; q + tl <= end; ++q)
        if (std::memcmp(q, tok, tl) == 0) return q;
    return end;
}
inline bool isWeaponSlot(const char* slot) { return std::strstr(slot, "Weapon") != nullptr; }
inline LoadoutSlot* findSlot(Loadout& l, const char* slot) {
    for (int i = 0; i < l.count; ++i)
        if (std::strcmp(l.slots[i].slot, slot) == 0) return &l.slots[i];
    if (l.count < kSlots) return &l.slots[l.count++];
    return nullptr;
}
}  // namespace detail

// One journal line (from its "event" to its end). True when it was a SuitLoadout or LoadoutEquipModule and was taken. A SuitLoadout replaces the
// whole loadout; a LoadoutEquipModule replaces its slot.
inline bool takeLoadoutLine(Loadout& l, const char* line, size_t len) {
    if (!line) return false;
    const char* end = line + len;
    char ev[32] = {};
    if (!detail::stringField(line, end, "event", ev, sizeof(ev))) return false;
    const bool suit = std::strcmp(ev, "SuitLoadout") == 0, equip = std::strcmp(ev, "LoadoutEquipModule") == 0;
    if (!suit && !equip) return false;
    if (suit) l.count = 0;
    // Every "SlotName" opens a module; its ModuleName and WeaponMods are searched up to the next SlotName (or the line's end).
    const char* p = line;
    while (p < end) {
        char slot[24] = {};
        const char* after = detail::stringField(p, end, "SlotName", slot, sizeof(slot));
        if (!after) break;
        const char* next = detail::findToken(after, end, "\"SlotName\":");
        if (detail::isWeaponSlot(slot)) {
            // In LoadoutEquipModule the module's keys may sit BEFORE SlotName too, so an equip line searches the whole line.
            const char* from = equip ? line : after;
            const char* to = equip ? end : next;
            char raw[64] = {};
            if (detail::stringField(from, to, "ModuleName", raw, sizeof(raw))) {
                if (LoadoutSlot* s = detail::findSlot(l, slot)) {
                    std::snprintf(s->slot, sizeof(s->slot), "%s", slot);
                    normaliseWeapon(raw, std::strlen(raw), s->weapon, sizeof(s->weapon));
                    if (!detail::modsField(from, to, s->mods, sizeof(s->mods))) s->mods[0] = '\0';
                }
            }
        }
        if (next >= end) break;
        p = next;
    }
    ++l.updates;
    return true;
}

// The mods of `weapon` in the loadout: true and the list when the weapon is in it.
inline bool modsOf(const Loadout& l, const char* weapon, const char** mods) {
    for (int i = 0; i < l.count; ++i)
        if (std::strcmp(l.slots[i].weapon, weapon) == 0) {
            if (mods) *mods = l.slots[i].mods;
            return true;
        }
    return false;
}

// ---- the aiming centre and the lens circle ----------------------------------------------------------------------------------------------------
// The eye's frustum as the runtime publishes it (nativeTemporalEyeGeometry: {left, right, down, up} tangents, left and down negative; the image's
// first row is up) and the eye region's size. The eye's straight-ahead lands at x = W L / (L + R), y = H U / (U + D); the sights sit pitchDownDeg
// below it (the head aim's pitch offset), so the centre is y = H (U + tan(pitchDown)) / (U + D). Flight 13.11 (Quest 3, right eye 2448x2672):
// the sights at about (950, 1270), this gives (927, 1274). The first build read the pair as {top, bottom} and put the lens at y 1594, 324 px
// low. The circle of kRadiusDeg around the centre is an ellipse in pixels (rx, ry: tan(radius) over the pixel's tangent), and the box the lens
// pass touches is the circle plus its soft edge, clipped to the region.
// The glass of a weapon's scope, measured on a snapshot: its corners and the point the zoom is centred on (the reticle), as offsets from the
// computed centre in the eye's tangent plane (x right, y down), so they hold at any resolution. Convex, in order around it.
constexpr int kGlassCorners = 8;
struct Glass {
    float aim[2];
    float v[kGlassCorners][2];
};
// The bare Executioner's glass, flight 13.11 (Quest 3 right eye 2448x2672, eye_001007, LT held, no lens; the computed centre (927.2, 1274.7), a
// tangent 1105.3 px across and 1116.6 px down): corners (892,1199) (1008,1200) (1050,1252) (1049,1305) (1015,1338) (887,1337) (850,1310)
// (846,1270), the reticle at (947,1271) -- 20 px (about 1 degree) right of the computed centre, a fact the report keeps open.
constexpr Glass kExecutionerGlass = {
    {0.0179f, -0.0033f},
    {{-0.0321f, -0.0675f}, {0.0734f, -0.0669f}, {0.1111f, -0.0206f}, {0.1105f, 0.0271f},
     {0.0794f, 0.0564f}, {-0.0366f, 0.0555f}, {-0.0698f, 0.0316f}, {-0.0735f, -0.0042f}}};
// With the Scope mod the same glass (Dima's guess, 13.11: the frame is the same, only the zoom is bigger), so the glass goes by the weapon alone.
inline const Glass* glassFor(const char* weapon) {
    return weapon && std::strcmp(weapon, kExecutioner) == 0 ? &kExecutionerGlass : nullptr;
}

struct Lens {
    bool valid = false;
    float cx = 0, cy = 0;     // the zoom's centre (the reticle with a glass; the computed centre without), in region pixels
    float rx = 0, ry = 0;     // the circle's radii, or the glass's half extents, in region pixels
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the box, [x0, x1) x [y0, y1), clipped to the region
    int corners = 0;          // 0: the circle; kGlassCorners: the glass below
    float px[kGlassCorners] = {}, py[kGlassCorners] = {};
};
// `pitchDownDeg` / `yawRightDeg`: where the sights sit against the eye's axis (the head aim's offset and its lag). `glass`: the weapon's glass, or
// null for the circle of `radiusDeg`.
inline Lens lensFor(const float tans[4], uint32_t w, uint32_t h, float radiusDeg = kRadiusDeg, float pitchDownDeg = 0.0f, float yawRightDeg = 0.0f,
                    const Glass* glass = nullptr) {
    Lens o;
    if (!tans || !w || !h) return o;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(tans[i])) return o;
    const float L = std::fabs(tans[0]), R = std::fabs(tans[1]), D = std::fabs(tans[2]), U = std::fabs(tans[3]);
    if (!(L + R > 1e-4f) || !(U + D > 1e-4f) || !(radiusDeg > 0.0f && radiusDeg < 80.0f) || !(pitchDownDeg > -60.0f && pitchDownDeg < 60.0f) ||
        !(yawRightDeg > -60.0f && yawRightDeg < 60.0f))
        return o;
    const float sx = float(w) / (L + R), sy = float(h) / (U + D);   // pixels per tangent
    // yawRightDeg is the barrel's lag alone (the head aim has no yaw offset), so the sway's correction rides on it (kLagYawGain, kLagYawLift).
    const float yawTan = std::tan(degToRad(yawRightDeg));
    const float bx = sx * (L + kLagYawGain * yawTan);
    const float by = sy * (U + std::tan(degToRad(pitchDownDeg)) + kLagYawLift * yawTan);
    auto clampi = [](float v, int lo, int hi) { int i = int(std::floor(v)); return i < lo ? lo : i > hi ? hi : i; };
    float minX, maxX, minY, maxY;
    if (glass) {
        o.corners = kGlassCorners;
        o.cx = bx + glass->aim[0] * sx;
        o.cy = by + glass->aim[1] * sy;
        minX = maxX = bx + glass->v[0][0] * sx;
        minY = maxY = by + glass->v[0][1] * sy;
        for (int i = 0; i < kGlassCorners; ++i) {
            o.px[i] = bx + glass->v[i][0] * sx;
            o.py[i] = by + glass->v[i][1] * sy;
            minX = o.px[i] < minX ? o.px[i] : minX;
            maxX = o.px[i] > maxX ? o.px[i] : maxX;
            minY = o.py[i] < minY ? o.py[i] : minY;
            maxY = o.py[i] > maxY ? o.py[i] : maxY;
        }
        o.rx = 0.5f * (maxX - minX);
        o.ry = 0.5f * (maxY - minY);
    } else {
        o.cx = bx;
        o.cy = by;
        const float t = std::tan(degToRad(radiusDeg));
        o.rx = t * sx;
        o.ry = t * sy;
        minX = o.cx - o.rx;
        maxX = o.cx + o.rx;
        minY = o.cy - o.ry;
        maxY = o.cy + o.ry;
    }
    o.x0 = clampi(minX - kEdgePx, 0, int(w));
    o.y0 = clampi(minY - kEdgePx, 0, int(h));
    o.x1 = clampi(std::ceil(maxX + kEdgePx), 0, int(w));
    o.y1 = clampi(std::ceil(maxY + kEdgePx), 0, int(h));
    o.valid = o.x1 > o.x0 && o.y1 > o.y0;
    return o;
}

// How far a pixel is inside the glass (positive) or outside (negative), in pixels: the least distance to an edge line, each edge's normal turned
// toward the zoom's centre. The shader does the same sum; this is the rig's reference.
inline float insideGlass(const Lens& l, float x, float y) {
    float best = 1e30f;
    for (int i = 0; i < l.corners; ++i) {
        const int j = (i + 1) % l.corners;
        float nx = -(l.py[j] - l.py[i]), ny = l.px[j] - l.px[i];
        const float len = std::sqrt(nx * nx + ny * ny);
        if (len <= 0.0f) continue;
        nx /= len;
        ny /= len;
        if ((l.cx - l.px[i]) * nx + (l.cy - l.py[i]) * ny < 0.0f) { nx = -nx; ny = -ny; }
        const float d = (x - l.px[i]) * nx + (y - l.py[i]) * ny;
        best = d < best ? d : best;
    }
    return best;
}

// ---- one aim --------------------------------------------------------------------------------------------------------------------------------
// LT on foot with a weapon from the table is one aim: from the press to the release. Its line says the peak zoom the lens drew and how many frames
// had the lens. Once a frame: LT now, the frames with a lens so far (the door's counter), the M the door last drew.
struct Aim {
    bool active = false;
    uint64_t startMs = 0;
    uint32_t lensAtStart = 0;
    float peakM = 1.0f;
    uint32_t aims = 0;   // aims begun, all time
};
struct AimEnd {
    bool ended = false;
    uint64_t ms = 0;
    uint32_t lensFrames = 0;
    float peakM = 1.0f;
};
inline AimEnd stepAim(Aim& a, bool lt, uint32_t lensFrames, float drawnM, uint64_t nowMs) {
    AimEnd e;
    if (lt && !a.active) {
        a.active = true;
        a.startMs = nowMs;
        a.lensAtStart = lensFrames;
        a.peakM = 1.0f;
        ++a.aims;
    }
    if (a.active && lensFrames != a.lensAtStart && drawnM > a.peakM) a.peakM = drawnM;
    if (!lt && a.active) {
        a.active = false;
        e.ended = true;
        e.ms = nowMs - a.startMs;
        e.lensFrames = lensFrames - a.lensAtStart;
        e.peakM = e.lensFrames ? a.peakM : 1.0f;
    }
    return e;
}

// ---- the summary's gate -----------------------------------------------------------------------------------------------------------------------
// A window is closed every kSummaryMs; it is said only when aims were begun in it.
inline bool summaryDue(uint64_t windowMs, uint64_t nowMs) { return nowMs - windowMs >= kSummaryMs; }

// ---- the lines --------------------------------------------------------------------------------------------------------------------------------
inline int formatKey(Key k, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: key=%s f_ref=%.1f deg", keyName(k), double(kFRefDeg));
}
inline int formatKeyUnknown(const char* raw, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: the key's value '%s' is not understood (on, off); reading on", raw ? raw : "");
}
// The weapon line: the name ("unknown" when Status.json has none), the mods ("none", or "unknown" when the loadout does not list the weapon),
// and the cap on M, or "lens=no" for a weapon without one.
inline int formatWeapon(const char* weapon, bool modsKnown, const char* mods, const Zoom& z, char* out, size_t cap) {
    const char* w = weapon && *weapon ? weapon : "unknown";
    const char* m = !modsKnown ? "unknown" : (mods && *mods ? mods : "none");
    if (z.inTable)
        return std::snprintf(out, cap, "scope zoom: weapon=%s mods=%s lens=yes zoom_cap=%.2f", w, m, double(z.m));
    return std::snprintf(out, cap, "scope zoom: weapon=%s mods=%s lens=no", w, m);
}
inline int formatNoSelectedWeapon(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: Status.json has no SelectedWeapon on foot; the weapon is unknown and no lens is drawn");
}
inline int formatAim(const char* weapon, const AimEnd& e, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: aim weapon=%s zoom=%.2f lens_frames=%u held=%llu ms", weapon && *weapon ? weapon : "unknown",
                         double(e.peakM), e.lensFrames, (unsigned long long)e.ms);
}
inline int formatFovUnread(const char* reason, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: fov unread: %s", reason ? reason : "unknown");
}
inline int formatFovHooked(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: fov hooked (slot +0x51887B8, adaptor +0x1031D90, ADS FOV step +0x10748D0; read only)");
}
inline int formatLensInert(const char* reason, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: lens inert: %s; standing without a lens until the key changes", reason ? reason : "unknown");
}
inline int formatLensNeedsEye(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: no lens: it needs the dominant eye (native_vr_on_foot_aim_eye is off)");
}
struct Summary {
    uint32_t aims = 0, lensFrames = 0, failures = 0;
    const char* weapon = nullptr;
    uint32_t gpuSamples = 0;
    double gpuMsSum = 0, gpuMsMax = 0;
};
inline int formatSummary(const Summary& s, char* out, size_t cap) {
    char gpu[48];
    if (s.gpuSamples) std::snprintf(gpu, sizeof(gpu), "%.3f ms avg %.3f max (%u)", s.gpuMsSum / s.gpuSamples, s.gpuMsMax, s.gpuSamples);
    else std::snprintf(gpu, sizeof(gpu), "none");
    return std::snprintf(out, cap, "scope zoom: summary 60 s aims=%u lens_frames=%u failures=%u weapon=%s lens_gpu=%s", s.aims, s.lensFrames,
                         s.failures, s.weapon && *s.weapon ? s.weapon : "unknown", gpu);
}

}  // namespace scz
}  // namespace edvr
