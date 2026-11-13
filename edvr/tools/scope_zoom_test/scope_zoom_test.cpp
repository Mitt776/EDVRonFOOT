// Build gate for the EDVRonFOOT on-foot scope zoom (src\d3d11\scope_zoom_core.h, experimental.native_vr_on_foot_scope_zoom; openspec
// change on-foot-scope-zoom).
//
// Drives the pure half: the key's reading (on, off, the probe's census and preview, empty, junk, case and padding), the weapon names out of
// Status.json and the journal, the zoom table (the Executioner capped at about 1.70 without the Scope mod and about 4.07 with it; the C-44, the
// pistol and the fists without a lens), the journal's loadout lines, the aiming centre and the glass from the eye's tangents, the zoom from the
// game's FOV, one aim's bookkeeping, the summary's gate, the recording's crop, and the text of every line. Nothing here touches the clock, the
// pad, the journal or a file, so --dry-run is the same run as --self-test.
#include "../../src/d3d11/scope_zoom_core.h"
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
    // The barrel's lag in a turn moves the whole lens with it: 2 degrees right and 1 down.
    const Lens still = l;
    l = lensFor(flight, 2448, 2672, kRadiusDeg, 11.0f, 2.0f, glassFor(kExecutioner));
    const float dx = std::tan(degToRad(2.0f)) * 2448.0f / 2.2148f;
    const float dy = (std::tan(degToRad(11.0f)) - std::tan(degToRad(10.0f))) * 2672.0f / 2.393f;
    check(near(l.cx - still.cx, dx, 0.5f) && near(l.cy - still.cy, dy, 0.5f) && near(l.px[3] - still.px[3], dx, 0.5f),
          "a lag of 2 degrees right and 1 down moves the zoom and the glass by its tangents");
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

void partRecord() {
    int x = -1, y = -1;
    check(cropOrigin(947.0f, 1271.0f, 2448, 2672, kCropPx, &x, &y) && x == 947 - 384 && y == 1271 - 384, "the crop around the centre");
    check(cropOrigin(100.0f, 2600.0f, 2448, 2672, kCropPx, &x, &y) && x == 0 && y == 2672 - 768, "the crop clamped into the region");
    check(!cropOrigin(500.0f, 500.0f, 700, 2000, kCropPx, &x, &y), "a region narrower than the crop: none");
    check(!cropOrigin(NAN, 500.0f, 2448, 2672, kCropPx, &x, &y), "a NaN centre: none");
    char line[512];
    RecordFrame r;
    r.n = 3;
    r.tMs = 41.25;
    r.ltMs = 170.0;
    r.fovDeg = 50.5f;
    r.m = 1.15f;
    r.lens = true;
    r.lagYawDeg = 0.25f;
    r.lagPitchDeg = -0.1f;
    r.latchLast = 1201;
    r.latchCam = 1200;
    r.camLagKnown = true;
    r.camLagYawDeg = 0.2f;
    r.camLagPitchDeg = -0.05f;
    r.yawRateDps = 45.0f;
    r.pitchRateDps = -2.0f;
    r.cx = 947.5f;
    r.cy = 1271.0f;
    r.cropX = 563;
    r.cropY = 887;
    r.crop = 3;
    formatRecordFrame(r, line, sizeof(line));
    check(std::strcmp(line, "scope zoom rec,n=3,t=41.2,lt=170.0,fov=50.500,m=1.1500,lens=1,lag=0.250,-0.100,latch=1201,cam_latch=1200,"
                            "cam_lag=0.200,-0.050,rate=45.0,-2.0,centre=947.5,1271.0,crop_at=563,887,crop=3") == 0,
          "the recording's frame line");
    r.camLagKnown = false;
    r.crop = -1;
    formatRecordFrame(r, line, sizeof(line));
    check(contains(line, "cam_lag=na,na") && contains(line, "crop=-1"), "the frame line without the camera's lag and without a crop");
    formatRecordBegin("D:\\logs\\scope_zoom\\201538", "right", 2448, 2672, 24, false, false, line, sizeof(line));
    check(contains(line, "recording begun (1500 ms) eye=right region=2448x2672 format=24 flip=0,0 crops=768x768 into D:"), "the begin line");
    formatRecordEnd(54, 52, 2, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: recording ended: 54 frames, 52 crops, 2 skipped") == 0, "the end line");
}

void partLines() {
    char line[512];
    formatKey(Key::On, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=on f_ref=56.3 deg") == 0, "the key line");
    formatKey(Key::Off, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=off f_ref=56.3 deg") == 0, "the key line, off");
    formatKeyUnknown("preview", line, sizeof(line));
    check(contains(line, "'preview' is not understood (on, off); reading on"), "the unknown key line");
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
    check(std::strcmp(line, "scope zoom: summary 60 s aims=4 lens_frames=600 failures=0 weapon=wpn_m_sniper_plasma_charged "
                            "lens_gpu=0.050 ms avg 0.080 max (10)") == 0,
          "the summary line");
    Summary z;
    formatSummary(z, line, sizeof(line));
    check(contains(line, "aims=0 lens_frames=0 failures=0 weapon=unknown lens_gpu=none"), "the summary with zeros");
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
    partLoadout();
    partLens();
    partAim();
    partRecord();
    partLines();
    std::printf("%s: scope_zoom_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
