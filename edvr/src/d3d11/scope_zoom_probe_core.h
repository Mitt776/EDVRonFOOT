#pragma once
// The on-foot scope zoom probe, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_scope_zoom; openspec change scope-zoom-probe, and
// scope_zoom_probe.h for the glue). Everything here is plain C++ the rig (tools\scope_zoom_probe_test) drives directly: the key's reading, the
// zoom table, the weapon names out of Status.json and the journal's loadout, the aiming centre and the lens circle from the eye's tangents, the
// zoom's ramp, the aiming tracker (LT and the game's flag), and the text of every line.
//
// WHAT IT IS FOR. In the flat game and in the cinema the game narrows its field of view while a weapon aims (LT); in stereo the lenses fix the
// field of view and the game cannot narrow it, so a scope shows the world 1:1. A TEMPORARY probe: census watches (weapon, aiming, the game's
// aiming FOV, snapshots of both eyes on PAUSE); preview adds a rough lens -- a circle of constant angular size around the aiming centre, in the
// dominant eye only, magnified by the table's M. It goes away when the permanent lens comes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace szp {

// ---- the key ----------------------------------------------------------------------------------------------------------------------------------
// experimental.native_vr_on_foot_scope_zoom: off (the default), census, preview. Empty and unknown read as off and are flagged (one line).
enum class Key : int { Off = 0, Census = 1, Preview = 2 };
inline const char* keyName(Key k) { return k == Key::Census ? "census" : k == Key::Preview ? "preview" : "off"; }

// Case-insensitive, trimmed. *valid is false for empty and for anything that is none of the three; a missing key (null) is off and valid.
inline Key parseKey(const char* text, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (!text) return Key::Off;
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
        if (is("off")) return Key::Off;
        if (is("census")) return Key::Census;
        if (is("preview")) return Key::Preview;
    }
    if (valid) *valid = false;
    return Key::Off;
}

// ---- constants --------------------------------------------------------------------------------------------------------------------------------
constexpr float kRadiusDeg = 5.0f;     // the lens circle's radius around the aiming centre (design, decision 2)
constexpr float kFRefDeg = 56.3f;      // the game's default field of view, the reference the table's FOVs narrow from (decision 3)
constexpr float kEdgePx = 2.0f;        // the circle's soft edge
constexpr uint64_t kSnapshotGapMs = 2000;   // at most one snapshot in this long
constexpr uint64_t kSummaryMs = 30000;      // the summary's period
constexpr float kFovStepDeg = 0.5f;         // a FOV line only for a change bigger than this

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
// 15 with it (the upstream's flat measurements). Everything else is not in the table: no lens, by choice (realism).
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

// The zoom while the aiming blend (the dominant eye's ramp, 0..1) runs: 1 + (M - 1) * blend, without a jump at either end.
inline float rampedM(float m, float blend) {
    if (!(blend > 0.0f)) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;
    if (!(m >= 1.0f)) m = 1.0f;
    return 1.0f + (m - 1.0f) * blend;
}

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
struct Lens {
    bool valid = false;
    float cx = 0, cy = 0;     // the centre, in region pixels
    float rx = 0, ry = 0;     // the circle's radii, in region pixels
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the box, [x0, x1) x [y0, y1), clipped to the region
};
inline Lens lensFor(const float tans[4], uint32_t w, uint32_t h, float radiusDeg = kRadiusDeg, float pitchDownDeg = 0.0f) {
    Lens o;
    if (!tans || !w || !h) return o;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(tans[i])) return o;
    const float L = std::fabs(tans[0]), R = std::fabs(tans[1]), D = std::fabs(tans[2]), U = std::fabs(tans[3]);
    if (!(L + R > 1e-4f) || !(U + D > 1e-4f) || !(radiusDeg > 0.0f && radiusDeg < 80.0f) || !(pitchDownDeg > -60.0f && pitchDownDeg < 60.0f)) return o;
    o.cx = float(w) * L / (L + R);
    o.cy = float(h) * (U + std::tan(degToRad(pitchDownDeg))) / (U + D);
    const float t = std::tan(degToRad(radiusDeg));
    o.rx = t * float(w) / (L + R);
    o.ry = t * float(h) / (U + D);
    auto clampi = [](float v, int lo, int hi) { int i = int(std::floor(v)); return i < lo ? lo : i > hi ? hi : i; };
    o.x0 = clampi(o.cx - o.rx - kEdgePx, 0, int(w));
    o.y0 = clampi(o.cy - o.ry - kEdgePx, 0, int(h));
    o.x1 = clampi(std::ceil(o.cx + o.rx + kEdgePx), 0, int(w));
    o.y1 = clampi(std::ceil(o.cy + o.ry + kEdgePx), 0, int(h));
    o.valid = o.x1 > o.x0 && o.y1 > o.y0;
    return o;
}

// ---- the aiming tracker -----------------------------------------------------------------------------------------------------------------------
// The lines of an aim follow LT; the game's flag (Status.json Flags2 bit 5, AimDownSight) is added when it comes, with its delay from LT.
constexpr uint32_t kFlags2AimDownSight = 0x20u;
enum class AdsEvent : int { None = 0, LtDown, LtUp, FlagUp, FlagDown };
struct AdsTrack {
    bool lt = false, flag = false, flagKnown = false;
    uint64_t ltDownMs = 0, ltUpMs = 0;
    uint32_t aims = 0;
};
struct AdsOut {
    AdsEvent ev[2] = {AdsEvent::None, AdsEvent::None};
    int64_t delayMs[2] = {-1, -1};   // the flag's delay from the LT edge it follows; -1 when no LT edge came before it
};
// Once a frame: LT now, the flag now (flagKnown false: the file has no Flags2), the clock.
inline AdsOut stepAds(AdsTrack& t, bool lt, bool flagKnown, bool flag, uint64_t nowMs) {
    AdsOut o;
    int n = 0;
    if (lt != t.lt) {
        t.lt = lt;
        if (lt) { t.ltDownMs = nowMs; ++t.aims; o.ev[n++] = AdsEvent::LtDown; }
        else { t.ltUpMs = nowMs; o.ev[n++] = AdsEvent::LtUp; }
    }
    const bool f = flagKnown && flag;
    if (f != t.flag && n < 2) {
        t.flag = f;
        o.ev[n] = f ? AdsEvent::FlagUp : AdsEvent::FlagDown;
        const uint64_t from = f ? t.ltDownMs : t.ltUpMs;
        o.delayMs[n] = from ? int64_t(nowMs - from) : -1;
        ++n;
    }
    t.flagKnown = flagKnown;
    return o;
}

// ---- the FOV line's gate ----------------------------------------------------------------------------------------------------------------------
// A new value is worth a line when there was none, or it moved by more than kFovStepDeg (radians in, degrees compared); NaN never.
inline bool fovChanged(bool had, float prevRad, float nowRad) {
    if (!std::isfinite(nowRad)) return false;
    if (!had) return true;
    return std::fabs(radToDeg(nowRad - prevRad)) > kFovStepDeg;
}

// ---- the snapshot's gate ----------------------------------------------------------------------------------------------------------------------
inline bool snapshotAllowed(bool any, uint64_t lastMs, uint64_t nowMs) { return !any || nowMs - lastMs >= kSnapshotGapMs; }

// ---- the lines --------------------------------------------------------------------------------------------------------------------------------
inline int formatKey(Key k, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: key=%s radius=%.1f deg f_ref=%.1f deg", keyName(k), double(kRadiusDeg), double(kFRefDeg));
}
inline int formatKeyUnknown(const char* raw, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: the key's value '%s' is not understood (off, census, preview); reading off", raw ? raw : "");
}
// The weapon line: the name ("unknown" when Status.json has none), the mods ("none", or "unknown" when the loadout does not list the weapon),
// and M or "not in table".
inline int formatWeapon(const char* weapon, bool modsKnown, const char* mods, const Zoom& z, char* out, size_t cap) {
    const char* w = weapon && *weapon ? weapon : "unknown";
    const char* m = !modsKnown ? "unknown" : (mods && *mods ? mods : "none");
    if (z.inTable)
        return std::snprintf(out, cap, "scope zoom: weapon=%s mods=%s ads_fov=%.1f deg zoom=%.2f", w, m, double(z.adsDeg), double(z.m));
    return std::snprintf(out, cap, "scope zoom: weapon=%s mods=%s zoom=not in table", w, m);
}
inline int formatNoSelectedWeapon(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: Status.json has no SelectedWeapon on foot; the weapon is unknown and no lens is drawn");
}
inline int formatAds(AdsEvent e, int64_t delayMs, const char* weapon, uint32_t aims, char* out, size_t cap) {
    const char* w = weapon && *weapon ? weapon : "unknown";
    switch (e) {
        case AdsEvent::LtDown: return std::snprintf(out, cap, "scope zoom: ads begin (LT pressed) weapon=%s aim=%u", w, aims);
        case AdsEvent::LtUp: return std::snprintf(out, cap, "scope zoom: ads end (LT released) weapon=%s aim=%u", w, aims);
        case AdsEvent::FlagUp:
            if (delayMs < 0) return std::snprintf(out, cap, "scope zoom: ads game flag up (no LT before it) weapon=%s", w);
            return std::snprintf(out, cap, "scope zoom: ads game flag up delay=%lld ms after LT weapon=%s", (long long)delayMs, w);
        case AdsEvent::FlagDown:
            if (delayMs < 0) return std::snprintf(out, cap, "scope zoom: ads game flag down (no LT release before it) weapon=%s", w);
            return std::snprintf(out, cap, "scope zoom: ads game flag down delay=%lld ms after LT release weapon=%s", (long long)delayMs, w);
        default: break;
    }
    if (cap) out[0] = '\0';
    return 0;
}
// The game's camera FOV as the hook read it after the game's own ADS FOV step (radians in the camera; printed both ways).
inline int formatFov(bool cinema, bool aiming, float rad, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: fov mode=%s aiming=%s value=%.2f deg (%.4f rad)", cinema ? "cinema" : "stereo",
                         aiming ? "yes" : "no", double(radToDeg(rad)), double(rad));
}
inline int formatFovUnread(const char* reason, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: fov unread: %s", reason ? reason : "unknown");
}
inline int formatFovHooked(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: fov hooked (slot +0x51887B8, adaptor +0x1031D90, ADS FOV step +0x10748D0; read only)");
}
inline int formatSnapshot(const char* weapon, bool aiming, const char* eyeName, const Lens& l, uint32_t w, uint32_t h, bool lens, float m,
                          char* out, size_t cap) {
    const char* wp = weapon && *weapon ? weapon : "unknown";
    if (!l.valid)
        return std::snprintf(out, cap, "scope zoom: snapshot weapon=%s aiming=%s eye=%s centre=unknown lens=%s", wp, aiming ? "yes" : "no",
                             eyeName, lens ? "yes" : "no");
    return std::snprintf(out, cap, "scope zoom: snapshot weapon=%s aiming=%s eye=%s centre=(%.1f, %.1f) of %ux%u radius=(%.1f, %.1f) px lens=%s zoom=%.2f",
                         wp, aiming ? "yes" : "no", eyeName, double(l.cx), double(l.cy), w, h, double(l.rx), double(l.ry), lens ? "yes" : "no",
                         double(m));
}
inline int formatSnapshotSkipped(uint32_t n, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: snapshot skipped (%u press%s inside 2 s of the last)", n, n == 1 ? "" : "es");
}
inline int formatLensInert(const char* reason, char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: lens inert: %s; standing without a lens until the key changes", reason ? reason : "unknown");
}
inline int formatLensNeedsEye(char* out, size_t cap) {
    return std::snprintf(out, cap, "scope zoom: no lens: it needs the dominant eye (native_vr_on_foot_aim_eye is off)");
}
struct Summary {
    Key key = Key::Off;
    bool cinema = false;
    uint32_t aims = 0, lensFrames = 0, snapshots = 0, skipped = 0, failures = 0, fovCalls = 0;
    const char* weapon = nullptr;
    Zoom zoom;
    bool fovKnown = false;
    float fovRad = 0;
    uint32_t gpuSamples = 0;
    double gpuMsSum = 0, gpuMsMax = 0;
};
inline int formatSummary(const Summary& s, char* out, size_t cap) {
    char m[24], fov[32], gpu[48];
    if (s.zoom.inTable) std::snprintf(m, sizeof(m), "%.2f", double(s.zoom.m));
    else std::snprintf(m, sizeof(m), "not in table");
    if (s.fovKnown) std::snprintf(fov, sizeof(fov), "%.2f deg", double(radToDeg(s.fovRad)));
    else std::snprintf(fov, sizeof(fov), "unknown");
    if (s.gpuSamples) std::snprintf(gpu, sizeof(gpu), "%.3f ms avg %.3f max (%u)", s.gpuMsSum / s.gpuSamples, s.gpuMsMax, s.gpuSamples);
    else std::snprintf(gpu, sizeof(gpu), "none");
    return std::snprintf(out, cap,
                         "scope zoom: summary 30 s key=%s mode=%s aims=%u lens_frames=%u snapshots=%u skipped=%u failures=%u weapon=%s zoom=%s "
                         "fov_calls=%u fov=%s lens_gpu=%s",
                         keyName(s.key), s.cinema ? "cinema" : "stereo", s.aims, s.lensFrames, s.snapshots, s.skipped, s.failures,
                         s.weapon && *s.weapon ? s.weapon : "unknown", m, s.fovCalls, fov, gpu);
}

}  // namespace szp
}  // namespace edvr
