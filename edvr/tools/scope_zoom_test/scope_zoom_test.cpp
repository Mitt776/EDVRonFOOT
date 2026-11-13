// Build gate for the EDVRonFOOT on-foot scope zoom (src\d3d11\scope_zoom_core.h, experimental.native_vr_on_foot_scope_zoom; openspec
// change on-foot-scope-zoom).
//
// Drives the pure half: the key's reading (on, off, the probe's census and preview, empty, junk, case and padding), the weapon names out of
// Status.json and the journal, the zoom table (the Executioner capped at about 1.70 without the Scope mod and about 4.07 with it; the C-44, the
// pistol and the fists without a lens), the journal's loadout lines, the aiming centre and the glass from the eye's tangents, the zoom from the
// game's FOV, one aim's bookkeeping, the summary's gate, the zoom's delay, the sway's correction, and the text of every line. Nothing here touches the clock, the
// pad, the journal or a file, so --dry-run is the same run as --self-test.
#include "../../src/d3d11/scope_zoom_core.h"
#include "../../src/common/temporal_math.h"   // the sharp lens's long jitter (native_temporal.cpp, 32 phases while it is drawn)
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edvr::scz;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool contains(const char* s, const char* sub) { return std::strstr(s, sub) != nullptr; }

void partKey() {
    bool valid = false;
    check(parseKey("on", &valid) == Key::On && valid, "on is on");
    check(parseKey("off", &valid) == Key::Off && valid, "off is off");
    check(parseKey(" OFF \r\n", &valid) == Key::Off && valid, "case and padding do not matter");
    check(parseKey("On", &valid) == Key::On && valid, "On is on");
    check(parseKey("preview", &valid) == Key::On && !valid, "the probe's preview reads on and is flagged");
    check(parseKey("census", &valid) == Key::On && !valid, "the probe's census reads on and is flagged");
    check(parseKey("", &valid) == Key::On && !valid, "empty reads on and is flagged");
    check(parseKey("   ", &valid) == Key::On && !valid, "blanks read on and are flagged");
    check(parseKey("offf", &valid) == Key::On && !valid, "a longer word is not off");
    check(parseKey(nullptr, &valid) == Key::On && valid, "a missing key is on and valid");
    check(parseKey("basic", &valid) == Key::Basic && valid, "basic is basic (the old lens, temporary)");
    check(parseKey(" Basic\r\n", &valid) == Key::Basic && valid, "Basic with padding is basic");
    check(parseKey("basics", &valid) == Key::On && !valid, "a longer word is not basic");
    check(std::strcmp(keyName(Key::Basic), "basic") == 0 && std::strcmp(keyName(Key::On), "on") == 0 && std::strcmp(keyName(Key::Off), "off") == 0,
          "the three names");
}

void partNames() {
    char out[64];
    check(normaliseWeapon("$wpn_m_sniper_plasma_charged_name;", 34, out, sizeof(out)) && std::strcmp(out, kExecutioner) == 0,
          "Status.json's SelectedWeapon normalises to the internal name");
    check(normaliseWeapon("Wpn_M_Sniper_Plasma_Charged", 27, out, sizeof(out)) && std::strcmp(out, kExecutioner) == 0,
          "the journal's ModuleName normalises (lower case)");
    check(normaliseWeapon("$humanoid_fists_name;", 21, out, sizeof(out)) && std::strcmp(out, "humanoid_fists") == 0, "fists normalise");
    check(!normaliseWeapon("$_name;", 7, out, sizeof(out)), "nothing left is false");
    check(!normaliseWeapon(nullptr, 0, out, sizeof(out)) && out[0] == '\0', "null is false and empty");
    char small[8];
    check(normaliseWeapon("wpn_m_sniper", 12, small, sizeof(small)) && std::strlen(small) == 7, "a long name is truncated to the buffer");
}

void partZoom() {
    check(near(magnification(35.0f), 1.697f, 0.01f), "M for 35 degrees is about 1.70");
    check(near(magnification(15.0f), 4.065f, 0.01f), "M for 15 degrees is about 4.07");
    check(near(magnification(56.3f), 1.0f, 1e-4f), "M for the reference is 1");
    check(magnification(0.0f) == 0.0f && magnification(-5.0f) == 0.0f && magnification(180.0f) == 0.0f, "an unusable FOV is 0");
    Zoom z = zoomFor(kExecutioner, "");
    check(z.inTable && near(z.adsDeg, 35.0f) && near(z.m, 1.70f, 0.01f), "the bare Executioner: 35 degrees, about 1.70");
    z = zoomFor(kExecutioner, nullptr);
    check(z.inTable && near(z.m, 1.70f, 0.01f), "the Executioner with unknown mods: no Scope assumed");
    z = zoomFor(kExecutioner, "weapon_stability,weapon_scope");
    check(z.inTable && near(z.adsDeg, 15.0f) && near(z.m, 4.07f, 0.01f), "the Executioner with the Scope mod: 15 degrees, about 4.07");
    z = zoomFor("wpn_m_assaultrifle_kinetic_fauto", "");
    check(!z.inTable && z.m == 1.0f, "the Karma C-44 is not in the table");
    z = zoomFor("wpn_s_pistol_plasma_charged", "");
    check(!z.inTable, "the pistol has no lens");
    z = zoomFor("humanoid_fists", nullptr);
    check(!z.inTable, "the fists have no lens");
    z = zoomFor(nullptr, nullptr);
    check(!z.inTable, "no weapon is not in the table");
    check(near(zoomFromFov(degToRad(10.0f), zoomFor(kExecutioner, "weapon_scope").m), 4.07f, 0.01f) &&
              near(zoomFromFov(degToRad(10.0f), zoomFor(kExecutioner, "").m), 1.70f, 0.01f),
          "the game's FOV is capped at 4.07 with the Scope mod and 1.70 without");
    check(modsHaveScope("weapon_Scope") && !modsHaveScope("weapon_stability") && !modsHaveScope(nullptr) && !modsHaveScope("scop"),
          "the Scope mod is found by its name, any case");
}

void partRamp() {
    check(near(rampedM(1.70f, 0.0f), 1.0f, 1e-6f), "blend 0: no zoom");
    check(near(rampedM(1.70f, 0.5f), 1.35f, 1e-5f), "blend 0.5: half way");
    check(near(rampedM(1.70f, 1.0f), 1.70f, 1e-6f), "blend 1: the whole zoom");
    check(near(rampedM(1.70f, 2.0f), 1.70f, 1e-6f) && near(rampedM(1.70f, -1.0f), 1.0f, 1e-6f), "the blend is clamped");
    check(near(rampedM(1.70f, NAN), 1.0f, 1e-6f), "a NaN blend is 0");
    check(near(rampedM(0.5f, 1.0f), 1.0f, 1e-6f), "an M below 1 never shrinks");
    // The zoom from the game's FOV (flight 13.11: 56.25 at the hip, 35.09 aiming the Executioner, about 50.5 with the C-44).
    const float cap = zoomFor(kExecutioner, "").m;
    check(near(zoomFromFov(degToRad(56.25f), cap), 1.0f, 0.005f), "the hip FOV: no zoom");
    check(near(zoomFromFov(degToRad(35.09f), cap), 1.69f, 0.01f), "the Executioner aiming: about 1.69");
    check(near(zoomFromFov(degToRad(45.0f), cap), std::tan(degToRad(28.15f)) / std::tan(degToRad(22.5f)), 1e-4f), "half way up: the game's own M");
    check(near(zoomFromFov(degToRad(20.0f), cap), cap, 1e-5f), "never above the table's M");
    check(zoomFromFov(degToRad(70.0f), cap) == 1.0f && zoomFromFov(-1.0f, cap) == 1.0f && zoomFromFov(NAN, cap) == 1.0f,
          "a wider, negative or NaN FOV: 1");
    check(near(zoomFromFov(degToRad(35.09f), 0.0f), 1.69f, 0.01f), "no cap: the game's M");
}

void partLoadout() {
    const char* suit =
        "{ \"timestamp\":\"2026-11-12T14:40:00Z\", \"event\":\"SuitLoadout\", \"SuitID\":1, \"SuitName\":\"assaultsuit_class2\", \"SuitMods\":[], "
        "\"LoadoutID\":4293000001, \"LoadoutName\":\"Battle\", \"Modules\":[ "
        "{ \"SlotName\":\"PrimaryWeapon1\", \"SuitModuleID\":11, \"ModuleName\":\"wpn_m_sniper_plasma_charged\", "
        "\"ModuleName_Localised\":\"Manticore Executioner\", \"Class\":2, \"WeaponMods\":[  ] }, "
        "{ \"SlotName\":\"PrimaryWeapon2\", \"SuitModuleID\":12, \"ModuleName\":\"wpn_m_assaultrifle_kinetic_fauto\", "
        "\"ModuleName_Localised\":\"Karma C-44\", \"Class\":3, \"WeaponMods\":[ \"weapon_stability\", \"weapon_handling\" ] }, "
        "{ \"SlotName\":\"SecondaryWeapon\", \"SuitModuleID\":13, \"ModuleName\":\"wpn_s_pistol_plasma_charged\", "
        "\"ModuleName_Localised\":\"Manticore Tormentor\", \"Class\":1, \"WeaponMods\":[] } ] }";
    Loadout l;
    check(takeLoadoutLine(l, suit, std::strlen(suit)), "SuitLoadout is taken");
    check(l.count == 3 && l.updates == 1, "three weapon slots");
    const char* mods = nullptr;
    check(modsOf(l, kExecutioner, &mods) && mods && mods[0] == '\0', "the Executioner has no mods");
    check(modsOf(l, "wpn_m_assaultrifle_kinetic_fauto", &mods) && std::strcmp(mods, "weapon_stability,weapon_handling") == 0,
          "the C-44's two mods, comma-joined");
    check(modsOf(l, "wpn_s_pistol_plasma_charged", &mods), "the Tormentor is listed");
    check(!modsOf(l, "wpn_m_launcher_rockets", &mods), "a weapon not in the loadout is not found");
    const char* equip =
        "{ \"timestamp\":\"2026-11-12T14:50:00Z\", \"event\":\"LoadoutEquipModule\", \"LoadoutName\":\"Battle\", \"SuitID\":1, "
        "\"SuitName\":\"assaultsuit_class2\", \"LoadoutID\":4293000001, \"SlotName\":\"PrimaryWeapon1\", "
        "\"ModuleName\":\"wpn_m_sniper_plasma_charged\", \"ModuleName_Localised\":\"Manticore Executioner\", \"Class\":3, "
        "\"WeaponMods\":[ \"weapon_scope\" ], \"SuitModuleID\":21 }";
    check(takeLoadoutLine(l, equip, std::strlen(equip)), "LoadoutEquipModule is taken");
    check(l.count == 3 && l.updates == 2, "the equip replaces its slot, the count stays");
    check(modsOf(l, kExecutioner, &mods) && std::strcmp(mods, "weapon_scope") == 0, "the Executioner now has the Scope mod");
    check(near(zoomFor(kExecutioner, mods).m, 4.07f, 0.01f), "and zooms about 4.07");
    const char* other = "{ \"timestamp\":\"x\", \"event\":\"Disembark\", \"SRV\":false }";
    check(!takeLoadoutLine(l, other, std::strlen(other)) && l.updates == 2, "another event is not taken");
    check(takeLoadoutLine(l, suit, std::strlen(suit)) && l.count == 3 && modsOf(l, kExecutioner, &mods) && mods[0] == '\0',
          "a new SuitLoadout replaces the whole loadout");
    const char* cut = "{ \"event\":\"SuitLoadout\", \"Modules\":[ { \"SlotName\":\"PrimaryWeapon1\", \"ModuleName\":\"wpn_m_sni";
    Loadout l2;
    takeLoadoutLine(l2, cut, std::strlen(cut));
    check(l2.count == 0, "a line cut inside a module's name takes nothing");
}

void partLens() {
    // A symmetric field: the centre is the middle.
    const float sym[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
    Lens l = lensFor(sym, 2000, 2000);
    check(l.valid && near(l.cx, 1000.0f) && near(l.cy, 1000.0f), "a symmetric field: the centre in the middle");
    const float r = std::tan(degToRad(5.0f)) * 1000.0f;   // tan(5) over a pixel's tangent (2/2000)
    check(near(l.rx, r, 0.01f) && near(l.ry, r, 0.01f), "the radius is tan(5 deg) in tangent pixels");
    check(l.x0 >= 0 && l.x1 <= 2000 && l.x1 - l.x0 >= int(2 * r) && l.y1 - l.y0 >= int(2 * r), "the box holds the circle");
    // An asymmetric field ({left, right, down, up}): the centre moves off the middle.
    const float q3[4] = {-0.90f, 1.28f, -1.10f, 1.20f};
    l = lensFor(q3, 2064, 2208);
    check(l.valid && l.cx < 1032.0f - 50.0f, "an asymmetric field: the centre moves toward the smaller side");
    check(near(l.cx, 2064.0f * 0.90f / 2.18f, 0.1f) && near(l.cy, 2208.0f * 1.20f / 2.30f, 0.1f), "the centre is W L/(L+R), H U/(U+D) (U the 4th)");
    // Flight 13.11, Quest 3 right eye 2448x2672 (the tangents rebuilt from the first build's snapshot line): the sights were at about
    // (950, 1270). The eye's axis is at y 1078; with the head aim's 10 degrees down the centre lands on the sights.
    const float flight[4] = {-0.8389f, 1.3759f, -1.4278f, 0.9652f};
    l = lensFor(flight, 2448, 2672);
    check(l.valid && near(l.cx, 927.2f, 1.0f) && near(l.cy, 1077.7f, 2.0f), "flight 13.11: the axis at (927, 1078), not the first build's y 1594");
    l = lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f);
    check(l.valid && std::fabs(l.cy - 1270.0f) < 10.0f && std::fabs(l.cx - 950.0f) < 30.0f, "flight 13.11: 10 degrees down puts the centre on the sights");
    check(near(l.ry, 97.7f, 0.5f) && near(l.rx, 96.7f, 0.5f), "flight 13.11: the radii as logged");
    check(!lensFor(flight, 2448, 2672, kRadiusDeg, 90.0f).valid, "an absurd pitch: no lens");
    // The Executioner's glass, against the corners measured on the 13.11 snapshot (within 1.5 px), and the zoom on the reticle.
    l = lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f, 0.0f, glassFor(kExecutioner));
    const float measured[8][2] = {{892, 1199}, {1008, 1200}, {1050, 1252}, {1049, 1305}, {1015, 1338}, {887, 1337}, {850, 1310}, {846, 1270}};
    bool cornersOk = l.valid && l.corners == kGlassCorners;
    for (int i = 0; i < kGlassCorners && cornersOk; ++i)
        cornersOk = std::fabs(l.px[i] - measured[i][0]) < 1.5f && std::fabs(l.py[i] - measured[i][1]) < 1.5f;
    check(cornersOk, "flight 13.11: the glass's corners land where they were measured");
    check(std::fabs(l.cx - 947.0f) < 1.5f && std::fabs(l.cy - 1271.0f) < 1.5f, "flight 13.11: the zoom is centred on the reticle");
    check(l.x0 <= 846 - 2 && l.x1 >= 1050 + 2 && l.y0 <= 1199 - 2 && l.y1 >= 1338 + 2, "the box holds the glass and its edge");
    check(insideGlass(l, 947.0f, 1271.0f) > 50.0f, "the reticle is deep inside the glass");
    check(insideGlass(l, 947.0f, 1190.0f) < 0.0f && insideGlass(l, 1060.0f, 1271.0f) < 0.0f && insideGlass(l, 947.0f, 1345.0f) < 0.0f &&
              insideGlass(l, 836.0f, 1271.0f) < 0.0f,
          "above, right of, below and left of the glass is outside");
    check(insideGlass(l, 860.0f, 1215.0f) < 0.0f, "the upper left slant cuts its corner off");
    check(glassFor("wpn_m_assaultrifle_kinetic_fauto") == nullptr && glassFor("wpn_s_pistol_plasma_charged") == nullptr &&
              glassFor("humanoid_fists") == nullptr && glassFor(nullptr) == nullptr,
          "only the Executioner has a glass (the C-44, the pistol and the fists none)");
    // The barrel's lag in a turn moves the whole lens with it: 2 degrees right and 1 down, the yaw with the game's sway on top (kLagYawGain,
    // kLagYawLift: further and up).
    const Lens still = l;
    l = lensFor(flight, 2448, 2672, kRadiusDeg, 11.0f, 2.0f, glassFor(kExecutioner));
    const float sx = 2448.0f / 2.2148f, sy = 2672.0f / 2.393f;
    const float dx = kLagYawGain * std::tan(degToRad(2.0f)) * sx;
    const float dy = (std::tan(degToRad(11.0f)) - std::tan(degToRad(10.0f)) + kLagYawLift * std::tan(degToRad(2.0f))) * sy;
    check(near(l.cx - still.cx, dx, 0.5f) && near(l.cy - still.cy, dy, 0.5f) && near(l.px[3] - still.px[3], dx, 0.5f),
          "a lag of 2 degrees right and 1 down moves the zoom and the glass by its tangents, the yaw with the sway");
    // Flight 1's turn: a yaw lag of 5 degrees right moves the lens 16% further than the lag alone and about 13 px up; 5 left, the mirror.
    const Lens r5 = lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f, 5.0f, glassFor(kExecutioner));
    const Lens l5 = lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f, -5.0f, glassFor(kExecutioner));
    const float bare = std::tan(degToRad(5.0f)) * sx;
    check(near((r5.cx - still.cx) / bare, 1.157f, 0.01f) && r5.cy < still.cy - 10.0f && r5.cy > still.cy - 16.0f,
          "5 degrees of yaw lag right: the lens 16% further right and 10-16 px up");
    check(near(l5.cx - still.cx, -(r5.cx - still.cx), 0.01f) && near(l5.cy - still.cy, -(r5.cy - still.cy), 0.01f),
          "5 degrees left: the mirror (left and down)");
    check(!lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f, 70.0f, nullptr).valid, "an absurd yaw: no lens");
    // The centre at the edge: clipped, never out of the region, never empty.
    const float edge[4] = {-0.0001f, 2.0f, -1.0f, 1.0f};
    l = lensFor(edge, 1000, 1000);
    check(l.valid && l.x0 == 0 && l.x1 > 0 && l.x1 <= 1000, "the centre at the left edge: clipped, not empty");
    const float edge2[4] = {-2.0f, 0.0001f, -0.0001f, 1.0f};
    l = lensFor(edge2, 1000, 1000);
    check(l.valid && l.x1 == 1000 && l.y1 == 1000 && l.x0 < 1000 && l.y0 < 1000, "the centre at the bottom right corner: clipped, not empty");
    const float bad[4] = {NAN, 1.0f, -1.0f, 1.0f};
    check(!lensFor(bad, 1000, 1000).valid, "a NaN tangent: no lens");
    check(!lensFor(sym, 0, 1000).valid, "no width: no lens");
    check(!lensFor(nullptr, 1000, 1000).valid, "no tangents: no lens");
}

void partAim() {
    Aim a;
    AimEnd e = stepAim(a, false, 0, 1.0f, 1000);
    check(!e.ended && !a.active && a.aims == 0, "nothing at rest");
    e = stepAim(a, true, 10, 1.0f, 1100);
    check(!e.ended && a.active && a.aims == 1, "LT pressed: an aim begins");
    e = stepAim(a, true, 11, 1.30f, 1200);
    e = stepAim(a, true, 20, 1.70f, 1300);
    e = stepAim(a, true, 30, 1.69f, 1400);
    check(!e.ended && near(a.peakM, 1.70f), "the peak is kept while LT is held");
    e = stepAim(a, false, 30, 1.69f, 1600);
    check(e.ended && e.lensFrames == 20 && near(e.peakM, 1.70f) && e.ms == 500, "LT released: 20 frames with a lens, peak 1.70, 500 ms");
    // An aim without a lens frame says zoom 1, whatever M the door drew last time.
    e = stepAim(a, true, 30, 1.70f, 2000);
    e = stepAim(a, false, 30, 1.70f, 2100);
    check(e.ended && e.lensFrames == 0 && near(e.peakM, 1.0f) && a.aims == 2, "an aim with no lens frame: zoom 1");
    check(!summaryDue(1000, 60999) && summaryDue(1000, 61000), "the summary every 60 s");
}

void partLines() {
    char line[512];
    formatKey(Key::On, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=on f_ref=56.3 deg") == 0, "the key line");
    formatKey(Key::Off, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=off f_ref=56.3 deg") == 0, "the key line, off");
    formatKeyUnknown("preview", line, sizeof(line));
    check(contains(line, "'preview' is not understood (on, basic, off); reading on"), "the unknown key line");
    formatKey(Key::Basic, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=basic f_ref=56.3 deg") == 0, "the key line, basic");
    formatWeapon(kExecutioner, true, "", zoomFor(kExecutioner, ""), line, sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=wpn_m_sniper_plasma_charged mods=none lens=yes zoom_cap=1.70") == 0, "the weapon line, bare");
    formatWeapon(kExecutioner, true, "weapon_scope", zoomFor(kExecutioner, "weapon_scope"), line, sizeof(line));
    // tan(28.15) / tan(7.5) = 4.065, printed 4.06 or 4.07 by float rounding.
    check(contains(line, "scope zoom: weapon=wpn_m_sniper_plasma_charged mods=weapon_scope lens=yes zoom_cap=4.0"),
          "the weapon line, the Scope mod");
    formatWeapon("wpn_m_assaultrifle_kinetic_fauto", true, "weapon_stability", zoomFor("wpn_m_assaultrifle_kinetic_fauto", nullptr), line,
                 sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=wpn_m_assaultrifle_kinetic_fauto mods=weapon_stability lens=no") == 0, "the weapon line, no lens");
    formatWeapon(nullptr, false, nullptr, Zoom{}, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=unknown mods=unknown lens=no") == 0, "the weapon line, unknown");
    formatNoSelectedWeapon(line, sizeof(line));
    check(contains(line, "no SelectedWeapon"), "the missing SelectedWeapon line");
    AimEnd e;
    e.ended = true;
    e.ms = 2150;
    e.lensFrames = 70;
    e.peakM = 1.6985f;
    formatAim(kExecutioner, e, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: aim weapon=wpn_m_sniper_plasma_charged zoom=1.70 lens_frames=70 held=2150 ms") == 0, "the aim line");
    formatFovUnread("the game's code at +0x10748D0 is not 334925's", line, sizeof(line));
    check(contains(line, "fov unread: the game's code"), "the fov unread line");
    formatFovHooked(line, sizeof(line));
    check(contains(line, "fov hooked") && contains(line, "read only"), "the fov hooked line");
    formatLensInert("the eye's format R8G8B8A8_UNORM_SRGB is not handled", line, sizeof(line));
    check(contains(line, "lens inert: the eye's format") && contains(line, "until the key changes"), "the lens inert line");
    formatLensNeedsEye(line, sizeof(line));
    check(contains(line, "needs the dominant eye"), "the needs-eye line");
    Summary s;
    s.aims = 4;
    s.lensFrames = 600;
    s.weapon = kExecutioner;
    s.gpuSamples = 10;
    s.gpuMsSum = 0.5;
    s.gpuMsMax = 0.08;
    formatSummary(s, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: summary 60 s aims=4 lens_frames=600 sharp_frames=0 failures=0 weapon=wpn_m_sniper_plasma_charged "
                            "lens_gpu=0.050 ms avg 0.080 max (10) sharp_gpu=none") == 0,
          "the summary line");
    s.sharpFrames = 580;
    s.sharpSamples = 8;
    s.sharpMsSum = 2.0;
    s.sharpMsMax = 0.31;
    formatSummary(s, line, sizeof(line));
    check(contains(line, "sharp_frames=580 ") && contains(line, "lens_gpu=0.050 ms avg 0.080 max (10) sharp_gpu=0.250 ms avg 0.310 max (8)"),
          "the summary line: the glass's DLSS apart from the lens");
    Summary z;
    formatSummary(z, line, sizeof(line));
    check(contains(line, "aims=0 lens_frames=0 sharp_frames=0 failures=0 weapon=unknown lens_gpu=none sharp_gpu=none"), "the summary with zeros");
    // The aim line with its path; without one, as before.
    e.sharpFrames = 64;
    formatAim(kExecutioner, e, line, sizeof(line), "sharp");
    check(std::strcmp(line, "scope zoom: aim weapon=wpn_m_sniper_plasma_charged zoom=1.70 lens_frames=70 held=2150 ms path=sharp sharp_frames=64") == 0,
          "the aim line with its path");
    // The path lines.
    formatPath(choosePath(Key::On, true, true, false, nullptr), line, sizeof(line));
    check(std::strcmp(line, "scope zoom: lens path=sharp reason=DLSS gathers the glass's area") == 0, "the path line, sharp");
    formatPath(choosePath(Key::On, false, false, false, nullptr), line, sizeof(line));
    check(std::strcmp(line, "scope zoom: lens path=basic reason=the anti-aliasing is not DLSS (fix.temporal_aa)") == 0, "the path line, no DLSS");
    formatPath(choosePath(Key::On, true, true, true, "the glass's DLSS feature would not be created at 120x80"), line, sizeof(line));
    check(contains(line, "lens path=basic reason=the glass's DLSS feature would not be created at 120x80"), "the path line, a refusal");
    SharpArea a;
    a.valid = true; a.w = 232; a.h = 168; a.outW = 394; a.outH = 286; a.s = 1.6985f; a.margin = 10;
    formatSharpFeature(a, 1.6985f, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: glass DLSS 232x168 -> 394x286 (S 1.70, zoom cap 1.70, stretched on top 1.00, margin 10 px)") == 0,
          "the feature line");
}

// The sharp lens's pure half (on-foot-scope-sharpness, tasks 1.1 and 1.2).
void partSharp() {
    const float flight[4] = {-0.8389f, 1.3759f, -1.4278f, 0.9652f};
    const float mBare = zoomFor(kExecutioner, "").m, mScope = zoomFor(kExecutioner, "weapon_scope").m;
    check(near(sharpScale(mBare), mBare) && near(sharpScale(mScope), 3.0f) && sharpScale(1.0f) == 0.0f && sharpScale(NAN) == 0.0f,
          "S: the zoom up to 3, nothing for no zoom or a NaN");
    check(near(sharpRemainder(4.07f, 3.0f), 1.357f, 0.001f) && near(sharpRemainder(1.70f, 1.70f), 1.0f) && near(sharpRemainder(1.2f, 1.70f), 1.0f),
          "the remainder: 4.07 / 3 = 1.357 with the Scope mod; nothing at or below S");
    const Lens l = lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f, 0.0f, glassFor(kExecutioner));
    const SharpArea a = sharpAreaFor(l, 2448, 2672, mBare);
    check(a.valid && a.w % kSharpStepPx == 0 && a.h % kSharpStepPx == 0, "the area: whole steps of 8");
    check(a.outW == uint32_t(std::lround(a.w * a.s)) && a.outH == uint32_t(std::lround(a.h * a.s)) && near(a.s, mBare), "the output: the input times S");
    bool marginOk = true, uvOk = true;
    for (int i = 0; i < l.corners; ++i) {
        const float left = (l.px[i] - a.x) * a.s, right = (a.x + float(a.w) - l.px[i]) * a.s;
        const float top = (l.py[i] - a.y) * a.s, bottom = (a.y + float(a.h) - l.py[i]) * a.s;
        if (left < 16.0f || right < 16.0f || top < 16.0f || bottom < 16.0f) marginOk = false;
        float u = 0, v = 0;
        sharpUv(a, l.px[i], l.py[i], &u, &v);
        if (!(u > 0.0f && u < 1.0f && v > 0.0f && v < 1.0f)) uvOk = false;
    }
    check(marginOk, "every corner of the glass is inside the area with 16 output pixels to spare");
    check(uvOk, "every corner of the glass maps inside the gathered image");
    float u = 0, v = 0;
    sharpUv(a, l.cx, l.cy, &u, &v);
    check(u > 0.3f && u < 0.7f && v > 0.3f && v < 0.7f, "the reticle maps near the gathered image's middle");
    sharpUv(a, a.x + 0.5f * a.w, a.y + 0.5f * a.h, &u, &v);
    check(near(u, 0.5f) && near(v, 0.5f), "the area's middle is the image's middle");
    // At full zoom, where the lens samples the corners from: c + (corner - c) / M, inside too.
    bool srcOk = true;
    for (int i = 0; i < l.corners; ++i) {
        sharpUv(a, l.cx + (l.px[i] - l.cx) / mBare, l.cy + (l.py[i] - l.cy) / mBare, &u, &v);
        if (!(u > 0.0f && u < 1.0f && v > 0.0f && v < 1.0f)) srcOk = false;
    }
    check(srcOk, "at full zoom the glass's corners sample inside the image");
    // The glass moving over the eye (the lag, 1 to 40 px): the same feature, only the corner moves.
    bool same = true, moved = false;
    for (float yaw = 0.05f; yaw <= 2.2f; yaw += 0.05f) {
        const SharpArea b = sharpAreaFor(lensFor(flight, 2448, 2672, kRadiusDeg, 10.0f + yaw * 0.5f, yaw, glassFor(kExecutioner)), 2448, 2672, mBare);
        if (!sameFeature(a, b)) same = false;
        if (b.x != a.x) moved = true;
    }
    check(same && moved, "a glass moving 1-40 px keeps the feature (the sizes), only the corner moves");
    float dx = 0, dy = 0;
    SharpArea b = a;
    b.x += 7; b.y -= 3;
    sharpMvShift(b, a, &dx, &dy);
    check(near(dx, 7.0f) && near(dy, -3.0f), "the motion vectors gain this corner minus the last");
    // With the Scope mod: S 3, a smaller margin in input pixels.
    const SharpArea sc = sharpAreaFor(l, 2448, 2672, mScope);
    check(sc.valid && near(sc.s, 3.0f) && sc.margin == 6 && sc.outW == sc.w * 3 && sc.w <= a.w, "the Scope mod: S 3, margin 6 input px");
    // At the eye's edge: inside the eye, the size kept, never empty.
    const float edge[4] = {-0.0001f, 2.0f, -1.0f, 1.0f};
    const SharpArea e = sharpAreaFor(lensFor(edge, 1000, 1000, kRadiusDeg, 0.0f, 0.0f, glassFor(kExecutioner)), 1000, 1000, mBare);
    check(e.valid && e.x == 0 && e.x + int(e.w) <= 1000 && e.y >= 0 && e.y + int(e.h) <= 1000, "at the eye's left edge: inside, not empty");
    const SharpArea tiny = sharpAreaFor(lensFor(flight, 100, 60, kRadiusDeg, 10.0f, 0.0f, glassFor(kExecutioner)), 100, 60, mBare);
    check(!tiny.valid || (tiny.w <= 100 && tiny.h <= 60 && tiny.x >= 0 && tiny.y >= 0), "an eye smaller than the area: cut to the eye");
    // No area: no glass, NaN, nothing to magnify, an empty eye.
    check(!sharpAreaFor(lensFor(flight, 2448, 2672), 2448, 2672, mBare).valid, "the circle (no glass): no area");
    Lens nan = l;
    nan.px[2] = NAN;
    check(!sharpAreaFor(nan, 2448, 2672, mBare).valid, "a NaN corner: no area");
    check(!sharpAreaFor(l, 2448, 2672, 1.0f).valid && !sharpAreaFor(l, 2448, 2672, NAN).valid, "no zoom or a NaN zoom: no area");
    check(!sharpAreaFor(l, 0, 2672, mBare).valid && !sharpAreaFor(Lens{}, 2448, 2672, mBare).valid, "an empty eye or no lens: no area");
    // The path (decisions 4 and 5): every combination.
    check(choosePath(Key::Off, true, true, false, nullptr).path == LensPath::None, "off: nothing");
    check(choosePath(Key::Off, false, false, true, "x").path == LensPath::None, "off: nothing, whatever else");
    PathChoice c = choosePath(Key::Basic, true, true, false, nullptr);
    check(c.path == LensPath::Basic && !*c.reason, "basic: the old lens, no reason (no line)");
    check(choosePath(Key::Basic, false, false, true, "x").path == LensPath::Basic, "basic without DLSS: the old lens");
    check(choosePath(Key::On, true, true, false, nullptr).path == LensPath::Sharp, "on with DLSS gathering the eye: sharp");
    c = choosePath(Key::On, false, true, false, nullptr);
    check(c.path == LensPath::Basic && contains(c.reason, "not DLSS"), "on without DLSS: basic, the reason");
    c = choosePath(Key::On, true, false, false, nullptr);
    check(c.path == LensPath::Basic && contains(c.reason, "not gathering the aiming eye"), "on, DLSS not on the eye: basic, the reason");
    c = choosePath(Key::On, true, true, true, "refused");
    check(c.path == LensPath::Basic && std::strcmp(c.reason, "refused") == 0, "on, the glass's feature refused: basic, its reason");
    c = choosePath(Key::On, true, true, true, nullptr);
    check(c.path == LensPath::Basic && *c.reason, "a refusal with no reason still says one");
}

// The long jitter (decision 2): the eight's phases are the 32's first eight; the plain call is the eight, bit for bit; the 32 are distinct and
// cover the pixel.
void partJitter() {
    bool firstEight = true, plain = true;
    for (uint32_t n = 0; n < 8; ++n) {
        float ax, ay, bx, by;
        edvr::temporalJitter(n, &ax, &ay);
        edvr::temporalJitter(n, &bx, &by, 32u);
        if (std::memcmp(&ax, &bx, 4) || std::memcmp(&ay, &by, 4)) firstEight = false;
    }
    for (uint32_t n = 0; n < 1000; ++n) {
        float ax, ay, bx, by;
        edvr::temporalJitter(n, &ax, &ay);
        edvr::temporalJitter(n, &bx, &by, edvr::kTemporalJitterCount);
        if (std::memcmp(&ax, &bx, 4) || std::memcmp(&ay, &by, 4)) plain = false;
    }
    check(firstEight, "the 32-phase sequence starts with the eight's phases");
    check(plain, "without the lens the call is the eight-phase one, bit for bit");
    int quarter[4] = {};
    bool distinct = true;
    float px[32], py[32];
    for (uint32_t n = 0; n < 32; ++n) {
        edvr::temporalJitter(n, &px[n], &py[n], 32u);
        ++quarter[(px[n] >= 0.0f ? 1 : 0) + (py[n] >= 0.0f ? 2 : 0)];
        for (uint32_t k = 0; k < n; ++k)
            if (px[k] == px[n] && py[k] == py[n]) distinct = false;
    }
    // Base 2 halves x exactly; base 3 does not halve y at 32, so a quarter holds 6 to 10.
    check(distinct && quarter[0] >= 6 && quarter[1] >= 6 && quarter[2] >= 6 && quarter[3] >= 6, "32 distinct phases, every quarter of the pixel covered");
}
}  // namespace

namespace {
// The zoom held back D on the way up, at once on the way down (design, decision 4): flight 1's curve, the FOV's zoom from 1 at 135 ms to 1.70 at
// 400 ms after LT, sampled every 13 ms.
float riseZoom(double t) { return t <= 135.0 ? 1.0f : t >= 400.0 ? 1.70f : float(1.0 + 0.70 * (t - 135.0) / 265.0); }
void partDelay() {
    check(near(float(kZoomDelayMs), 260.0f, 0.01f), "D is 260 ms (flight 1)");
    ZoomDelay z;
    bool zeroUntilD = true, neverAbove = true;
    float at660 = 0.0f, at390 = 0.0f;
    double t = 0.0;
    for (; t <= 1300.0; t += 13.0) {
        const float now = riseZoom(t);
        const float d = delayedZoom(z, now, t);
        if (t < 135.0 + kZoomDelayMs - 13.0 && d > 1.0f) zeroUntilD = false;
        if (d > now + 1e-6f) neverAbove = false;
        if (t >= 390.0 && at390 == 0.0f) at390 = d;
        if (t >= 400.0 + kZoomDelayMs + 13.0 && at660 == 0.0f) at660 = d;   // the FOV full plus D, a frame's sampling later
    }
    check(zeroUntilD, "no zoom until the FOV's first narrowing plus D");
    check(neverAbove, "never more zoom than the game's FOV gives");
    check(near(at390, riseZoom(390.0 - kZoomDelayMs), 0.03f) && at390 < 1.2f, "on the way up it is the FOV's zoom D ago");
    check(near(at660, 1.70f, 0.001f), "full at the FOV's full plus D");
    // LT let go: the FOV widens and the zoom follows the same frame.
    check(near(delayedZoom(z, 1.40f, t), 1.40f, 0.001f) && near(delayedZoom(z, 1.0f, t + 13.0), 1.0f, 0.001f), "on the way down at once");
    // A gap (the lens not asked for 200 ms): the old aim's zoom does not come back.
    ZoomDelay g;
    for (double s = 0.0; s <= 800.0; s += 13.0) delayedZoom(g, 1.70f, s);
    check(near(delayedZoom(g, 1.70f, 1000.0), 1.0f, 0.001f) && near(delayedZoom(g, 1.70f, 1013.0), 1.0f, 0.001f),
          "after a gap the history starts again: 1 until D has passed");
    float late = 0.0f;
    for (double s = 1026.0; s <= 1300.0; s += 13.0) late = delayedZoom(g, 1.70f, s);
    check(near(late, 1.70f, 0.001f), "and full once D has passed");
    // A clock that goes back starts the history again too; a NaN zoom reads as 1.
    check(near(delayedZoom(g, 1.70f, 500.0), 1.0f, 0.001f), "a clock going back: the history starts again");
    check(near(delayedZoom(g, NAN, 513.0), 1.0f, 0.001f), "a NaN zoom is 1");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: scope_zoom_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKey();
    partNames();
    partZoom();
    partRamp();
    partDelay();
    partLoadout();
    partLens();
    partAim();
    partLines();
    partSharp();
    partJitter();
    std::printf("%s: scope_zoom_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
