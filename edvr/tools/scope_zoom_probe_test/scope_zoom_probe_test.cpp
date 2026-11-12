// Build gate for the EDVRonFOOT on-foot scope zoom probe (src\d3d11\scope_zoom_probe_core.h, experimental.native_vr_on_foot_scope_zoom; openspec
// change scope-zoom-probe).
//
// Drives the pure half: the key's reading (off, census, preview, empty, junk, case and padding), the weapon names out of Status.json and the
// journal, the zoom table (the Executioner about 1.70 without the Scope mod and about 4.07 with it, the C-44 not in the table), the journal's
// loadout lines (SuitLoadout replacing, LoadoutEquipModule replacing one slot, other events ignored), the aiming centre and the circle from the
// eye's tangents (a symmetric field in the middle, an asymmetric one off it, the box clipped at the edge and never empty), the ramp of M at
// 0, 0.5 and 1, the aiming tracker (LT edges, the game's flag and its delay), the FOV and snapshot gates, and the text of every line. Nothing here
// touches the clock, the pad, the journal or a file, so --dry-run is the same run as --self-test.
#include "../../src/d3d11/scope_zoom_probe_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edvr::szp;

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
    check(parseKey("off", &valid) == Key::Off && valid, "off is off");
    check(parseKey("census", &valid) == Key::Census && valid, "census is census");
    check(parseKey("preview", &valid) == Key::Preview && valid, "preview is preview");
    check(parseKey(" Preview \r\n", &valid) == Key::Preview && valid, "case and padding do not matter");
    check(parseKey("CENSUS", &valid) == Key::Census && valid, "CENSUS is census");
    check(parseKey("", &valid) == Key::Off && !valid, "empty reads off and is flagged");
    check(parseKey("   ", &valid) == Key::Off && !valid, "blanks read off and are flagged");
    check(parseKey("zoom", &valid) == Key::Off && !valid, "zoom reads off and is flagged");
    check(parseKey("previews", &valid) == Key::Off && !valid, "a longer word is not preview");
    check(parseKey(nullptr, &valid) == Key::Off && valid, "a missing key is off and valid");
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
    z = zoomFor(nullptr, nullptr);
    check(!z.inTable, "no weapon is not in the table");
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
    // An asymmetric field (Quest 3-like, the right eye: more to the outside and down): the centre moves off the middle.
    const float q3[4] = {-0.90f, 1.28f, -1.10f, 1.20f};
    l = lensFor(q3, 2064, 2208);
    check(l.valid && l.cx < 1032.0f - 50.0f, "an asymmetric field: the centre moves toward the smaller side");
    check(near(l.cx, 2064.0f * 0.90f / 2.18f, 0.1f) && near(l.cy, 2208.0f * 1.10f / 2.30f, 0.1f), "the centre is W L/(L+R), H U/(U+D)");
    // The centre at the edge: clipped, never out of the region, never empty.
    const float edge[4] = {-0.0001f, 2.0f, -1.0f, 1.0f};
    l = lensFor(edge, 1000, 1000);
    check(l.valid && l.x0 == 0 && l.x1 > 0 && l.x1 <= 1000, "the centre at the left edge: clipped, not empty");
    const float edge2[4] = {-2.0f, 0.0001f, -1.0f, 0.0001f};
    l = lensFor(edge2, 1000, 1000);
    check(l.valid && l.x1 == 1000 && l.y1 == 1000 && l.x0 < 1000 && l.y0 < 1000, "the centre at the bottom right corner: clipped, not empty");
    const float bad[4] = {NAN, 1.0f, -1.0f, 1.0f};
    check(!lensFor(bad, 1000, 1000).valid, "a NaN tangent: no lens");
    check(!lensFor(sym, 0, 1000).valid, "no width: no lens");
    check(!lensFor(nullptr, 1000, 1000).valid, "no tangents: no lens");
}

void partAds() {
    AdsTrack t;
    AdsOut o = stepAds(t, false, true, false, 1000);
    check(o.ev[0] == AdsEvent::None, "nothing at rest");
    o = stepAds(t, true, true, false, 1100);
    check(o.ev[0] == AdsEvent::LtDown && t.aims == 1, "LT pressed: begin, one aim");
    o = stepAds(t, true, true, false, 1200);
    check(o.ev[0] == AdsEvent::None, "LT held: no repeat");
    o = stepAds(t, true, true, true, 1450);
    check(o.ev[0] == AdsEvent::FlagUp && o.delayMs[0] == 350, "the game's flag 350 ms after LT");
    o = stepAds(t, false, true, true, 2000);
    check(o.ev[0] == AdsEvent::LtUp && o.ev[1] == AdsEvent::None, "LT released: end");
    o = stepAds(t, false, true, false, 2600);
    check(o.ev[0] == AdsEvent::FlagDown && o.delayMs[0] == 600, "the flag down 600 ms after the release");
    // Both edges in one frame.
    AdsTrack u;
    o = stepAds(u, true, true, true, 5000);
    check(o.ev[0] == AdsEvent::LtDown && o.ev[1] == AdsEvent::FlagUp && o.delayMs[1] == 0, "LT and the flag in one frame: both, delay 0");
    // The flag without LT.
    AdsTrack v;
    o = stepAds(v, false, true, true, 7000);
    check(o.ev[0] == AdsEvent::FlagUp && o.delayMs[0] == -1, "the flag without LT: no delay");
    // An unknown flag reads down.
    AdsTrack w;
    stepAds(w, false, true, true, 100);
    o = stepAds(w, false, false, true, 200);
    check(o.ev[0] == AdsEvent::FlagDown, "a flag that stops being known reads down");
}

void partGates() {
    check(fovChanged(false, 0.0f, 0.6f), "the first FOV is a line");
    check(!fovChanged(true, degToRad(35.0f), degToRad(35.4f)), "0.4 degrees is no line");
    check(fovChanged(true, degToRad(35.0f), degToRad(35.6f)), "0.6 degrees is a line");
    check(!fovChanged(true, 0.0f, NAN) && !fovChanged(false, 0.0f, NAN), "NaN never");
    check(fovChanged(true, degToRad(56.3f), -1.0f), "a -1 the stereo camera may hold is a line");
    check(snapshotAllowed(false, 0, 0), "the first snapshot is allowed");
    check(!snapshotAllowed(true, 1000, 2999) && snapshotAllowed(true, 1000, 3000), "two seconds between snapshots");
}

void partLines() {
    char line[512];
    formatKey(Key::Census, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: key=census radius=5.0 deg f_ref=56.3 deg") == 0, "the key line");
    formatKeyUnknown("zoom", line, sizeof(line));
    check(contains(line, "'zoom' is not understood") && contains(line, "reading off"), "the unknown key line");
    formatWeapon(kExecutioner, true, "", zoomFor(kExecutioner, ""), line, sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=wpn_m_sniper_plasma_charged mods=none ads_fov=35.0 deg zoom=1.70") == 0, "the weapon line, bare");
    formatWeapon("wpn_m_assaultrifle_kinetic_fauto", true, "weapon_stability", zoomFor("wpn_m_assaultrifle_kinetic_fauto", nullptr), line,
                 sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=wpn_m_assaultrifle_kinetic_fauto mods=weapon_stability zoom=not in table") == 0,
          "the weapon line, not in the table");
    formatWeapon(nullptr, false, nullptr, Zoom{}, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: weapon=unknown mods=unknown zoom=not in table") == 0, "the weapon line, unknown");
    formatNoSelectedWeapon(line, sizeof(line));
    check(contains(line, "no SelectedWeapon"), "the missing SelectedWeapon line");
    formatAds(AdsEvent::LtDown, -1, kExecutioner, 3, line, sizeof(line));
    check(std::strcmp(line, "scope zoom: ads begin (LT pressed) weapon=wpn_m_sniper_plasma_charged aim=3") == 0, "the ads begin line");
    formatAds(AdsEvent::FlagUp, 350, kExecutioner, 3, line, sizeof(line));
    check(contains(line, "ads game flag up delay=350 ms after LT"), "the flag up line");
    formatAds(AdsEvent::LtUp, -1, nullptr, 3, line, sizeof(line));
    check(contains(line, "ads end (LT released) weapon=unknown"), "the ads end line");
    formatAds(AdsEvent::FlagDown, -1, kExecutioner, 3, line, sizeof(line));
    check(contains(line, "flag down (no LT release before it)"), "the flag down line without LT");
    formatFov(true, true, degToRad(35.0f), line, sizeof(line));
    check(contains(line, "fov mode=cinema aiming=yes value=35.00 deg (0.6109 rad)"), "the fov line");
    formatFovUnread("the game's code at +0x10748D0 is not 334925's", line, sizeof(line));
    check(contains(line, "fov unread: the game's code"), "the fov unread line");
    formatFovHooked(line, sizeof(line));
    check(contains(line, "fov hooked") && contains(line, "read only"), "the fov hooked line");
    const float sym[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
    Lens l = lensFor(sym, 2000, 2000);
    formatSnapshot(kExecutioner, true, "right", l, 2000, 2000, true, 1.70f, line, sizeof(line));
    check(contains(line, "snapshot weapon=wpn_m_sniper_plasma_charged aiming=yes eye=right centre=(1000.0, 1000.0) of 2000x2000") &&
              contains(line, "lens=yes zoom=1.70"),
          "the snapshot line");
    formatSnapshot(nullptr, false, "none", Lens{}, 0, 0, false, 1.0f, line, sizeof(line));
    check(contains(line, "centre=unknown lens=no"), "the snapshot line without a centre");
    formatSnapshotSkipped(2, line, sizeof(line));
    check(contains(line, "skipped (2 presses inside 2 s"), "the skipped line");
    formatLensInert("the eye's format R8G8B8A8_UNORM_SRGB is not handled", line, sizeof(line));
    check(contains(line, "lens inert: the eye's format") && contains(line, "until the key changes"), "the lens inert line");
    formatLensNeedsEye(line, sizeof(line));
    check(contains(line, "needs the dominant eye"), "the needs-eye line");
    Summary s;
    s.key = Key::Preview;
    s.aims = 4;
    s.lensFrames = 600;
    s.snapshots = 2;
    s.weapon = kExecutioner;
    s.zoom = zoomFor(kExecutioner, "");
    s.fovCalls = 1800;
    s.fovKnown = true;
    s.fovRad = degToRad(56.3f);
    s.gpuSamples = 10;
    s.gpuMsSum = 0.5;
    s.gpuMsMax = 0.08;
    formatSummary(s, line, sizeof(line));
    check(contains(line, "summary 30 s key=preview mode=stereo aims=4 lens_frames=600 snapshots=2 skipped=0 failures=0") &&
              contains(line, "zoom=1.70 fov_calls=1800 fov=56.30 deg lens_gpu=0.050 ms avg 0.080 max (10)"),
          "the summary line");
    Summary z;
    formatSummary(z, line, sizeof(line));
    check(contains(line, "key=off") && contains(line, "aims=0 lens_frames=0") && contains(line, "weapon=unknown zoom=not in table") &&
              contains(line, "fov=unknown lens_gpu=none"),
          "the summary with zeros");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: scope_zoom_probe_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKey();
    partNames();
    partZoom();
    partRamp();
    partLoadout();
    partLens();
    partAds();
    partGates();
    partLines();
    std::printf("%s: scope_zoom_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
