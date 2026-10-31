// The on-foot head aim probe and, on the same hooks, the head aim feature: the table entries, the getters, the latch and the frame thread's step
// (head_aim_probe.h says what and why).
#include "head_aim_probe.h"
#include "head_aim_probe_core.h"
#include "head_aim_core.h"
#include "journal_watch.h"
#include "on_foot_hud.h"
#include "../common/config.h"
#include "../common/head_aim_signal.h"
#include "../common/log.h"
#include "../common/native_frame.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#pragma intrinsic(_ReturnAddress)

namespace edvr {

namespace {

// ---- 334925: the entries, what they must point at, and the bytes there ----------------------------------------------------------------------------
constexpr uintptr_t kSlotAngle1 = 0x5206B58, kAngle1 = 0x1A45930;   // HMC vf+0xB0
constexpr uintptr_t kSlotAngle2 = 0x5206B70, kAngle2 = 0x1A49040;   // HMC vf+0xC8
constexpr uintptr_t kSlotEyeMatrix = 0x5200058, kEyeMatrix = 0xF893D0;   // the character eye's vf+0x20
constexpr uintptr_t kCameraCall = 0x10743C2, kCameraReturn = 0x10743CB;   // mov rdx,[rax]; mov rcx,rax; call [rdx+0x20]
constexpr uintptr_t kEyeToHmc = 0xD0;   // [eye - 0x58 + 0x128]
constexpr uintptr_t kEyeTable = 0x5200038, kHmcTable = 0x5206AA8;   // what the eye's and the HMC's first quadword must be
const uint8_t kAngle1Bytes[] = {0xF3, 0x0F, 0x10, 0x81, 0xF0, 0x06, 0x00, 0x00, 0xF3, 0x0F, 0x5C, 0x81, 0x10, 0x07, 0x00, 0x00, 0xC3};
const uint8_t kAngle2Bytes[] = {0xF3, 0x0F, 0x10, 0x81, 0x0C, 0x07, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0x81, 0xFC, 0x06, 0x00, 0x00, 0xC3};
const uint8_t kEyeMatrixBytes[] = {0x48, 0x8D, 0x81, 0x68, 0x02, 0x00, 0x00, 0xC3};
const uint8_t kCameraCallBytes[] = {0x48, 0x8B, 0x10, 0x48, 0x8B, 0xC8, 0xFF, 0x52, 0x20};
// A latch older than this is not the frame the game is building: the getters hand the angle on unshifted.
constexpr uint64_t kLatchFreshMs = 250;

using Getter = float (*)(void*);
using EyeMatrix = void* (*)(void*);

bool g_attempted = false, g_installed = false, g_camera = false;
Getter g_orig1 = nullptr, g_orig2 = nullptr;
EyeMatrix g_origEye = nullptr;
uintptr_t g_cameraReturnAddress = 0;

// Published by the frame thread, read by the latch and the getters.
std::atomic<int> g_mode{int(hap::Mode::Off)}, g_comp{int(hap::Comp::Off)};
std::atomic<bool> g_allowed{false}, g_onFoot{false};
std::atomic<uintptr_t> g_player{0};
// The feature (head_aim_core.h), published by the frame thread, read by the latch.
std::atomic<int> g_featKey{int(ha::Key::Off)}, g_featComp{int(ha::PitchComp::Plus)};
std::atomic<bool> g_menu{false}, g_focusKnown{false};
std::atomic<uint32_t> g_focus{0};
// Written by the latch, read by the getters.
std::atomic<int> g_latchAngle{0};
std::atomic<float> g_latchRad{0.0f};
std::atomic<uint64_t> g_latchMs{0};
// The feature's frame as the latch made it (read by the getters, and by the frame thread for its lines): who leads the frame, the yaw added to angle 2,
// the tilt the head asks angle 1 to be (the game's sign) and the level that blends it in, the tilt added in that frame, and the first shut gate.
std::atomic<bool> g_featLead{false};
std::atomic<float> g_featYaw{0.0f}, g_featTarget{0.0f}, g_featLevel{0.0f}, g_featPitchAdded{0.0f};
std::atomic<int> g_featReason{int(ha::Reason::Key)};
// The player's angle 1 as the game built it, from the last getter call on the player (the latch needs it for the tilt added).
std::atomic<float> g_lastOrig1{0.0f};
// The sync keys (published by the frame thread, read by the latch), and the camera counter: whether this game frame has had its latch, and how many camera
// builds came before it and after it (the latch sets the flag, the frame boundary takes all three).
std::atomic<int> g_featSync{int(ha::Sync::Latch)}, g_featStick{int(ha::StickComp::Off)};
std::atomic<bool> g_latchedThisFrame{false};
std::atomic<uint32_t> g_camBefore{0}, g_camAfter{0};
// The angles in force (what the latch last published: the yaw added and the tilt for the runtime, with the latch's number) and the copy a camera build
// took of them. One mutex, so a pair is always from one latch.
std::mutex g_forceMutex;
ha::Snapshot g_inForce, g_cameraSnap;
uint64_t g_latchNo = 0;   // under the mutex; counts the feature's latches
// Written by the getters and the eye's matrix getter, taken by the frame thread and the latch.
std::atomic<uintptr_t> g_census[hap::kSlots];
std::atomic<bool> g_censusMany{false};
std::atomic<uintptr_t> g_cameraEye{0};
// The camera's HMC as the frame thread last read it, and whether a getter has been called on it since (set by the getters, so a crowd past the
// census's slots cannot hide the player).
std::atomic<uintptr_t> g_cameraCandidate{0};
std::atomic<bool> g_cameraSeen{false};
uintptr_t g_base = 0;
std::atomic<uint64_t> g_calls1{0}, g_calls2{0}, g_playerCalls1{0};

// The latch's own state (the provider's thread, under its mutex) and the window both it and the frame thread write.
hap::Ramp g_ramp;
ha::State g_featState;   // the feature's filter and level: the latch only
std::mutex g_featWindowMutex;
ha::Window g_featWindow;
uint64_t g_lastLatchMs = 0;
LARGE_INTEGER g_qpcFrequency{}, g_qpcStart{};
std::mutex g_windowMutex;
hap::Window g_window;

void noteCensus(uintptr_t p) {
    for (int i = 0; i < hap::kSlots; ++i) {
        uintptr_t seen = g_census[i].load(std::memory_order_relaxed);
        if (seen == p) return;
        if (seen == 0) {
            if (g_census[i].compare_exchange_strong(seen, p, std::memory_order_relaxed)) return;
            if (seen == p) return;
        }
    }
    g_censusMany.store(true, std::memory_order_relaxed);
}

float shiftFor(void* self, int angle) {
    const uintptr_t me = reinterpret_cast<uintptr_t>(self);
    noteCensus(me);
    if (me == g_cameraCandidate.load(std::memory_order_relaxed)) g_cameraSeen.store(true, std::memory_order_relaxed);
    if (me != g_player.load(std::memory_order_relaxed) || g_latchAngle.load(std::memory_order_relaxed) != angle) return 0.0f;
    if (GetTickCount64() - g_latchMs.load(std::memory_order_relaxed) > kLatchFreshMs) return 0.0f;
    return g_latchRad.load(std::memory_order_relaxed);
}

// The feature leads the frame the latch last made, and that frame is fresh.
bool featureLeads() {
    return g_featLead.load(std::memory_order_relaxed) && GetTickCount64() - g_latchMs.load(std::memory_order_relaxed) <= kLatchFreshMs;
}

// The probe's shift is 0 while the feature leads (the latch latches no angle then). The player's angle 1 is remembered for the latch, and under the
// feature becomes the head's tilt blended in by the level; angle 2 gets the head's turn on top of what the game built.
float angle1Detour(void* self) {
    const float v = g_orig1(self);
    g_calls1.fetch_add(1, std::memory_order_relaxed);
    const bool player = reinterpret_cast<uintptr_t>(self) == g_player.load(std::memory_order_relaxed);
    if (player) g_playerCalls1.fetch_add(1, std::memory_order_relaxed);
    const float shift = shiftFor(self, 1);
    if (player) {
        g_lastOrig1.store(v, std::memory_order_relaxed);
        if (featureLeads()) return ha::blendPitch(v, g_featTarget.load(std::memory_order_relaxed), g_featLevel.load(std::memory_order_relaxed));
    }
    return v + shift;
}
float angle2Detour(void* self) {
    const float v = g_orig2(self);
    g_calls2.fetch_add(1, std::memory_order_relaxed);
    const float shift = shiftFor(self, 2);
    if (reinterpret_cast<uintptr_t>(self) == g_player.load(std::memory_order_relaxed) && featureLeads()) return v + g_featYaw.load(std::memory_order_relaxed);
    return v + shift;
}
void* eyeMatrixDetour(void* self) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_cameraReturnAddress) {
        g_cameraEye.store(reinterpret_cast<uintptr_t>(self), std::memory_order_relaxed);
        // A camera moment: counted against this frame's latch, and the angles in force are copied while the feature leads.
        (g_latchedThisFrame.load(std::memory_order_relaxed) ? g_camAfter : g_camBefore).fetch_add(1, std::memory_order_relaxed);
        if (featureLeads()) {
            std::lock_guard<std::mutex> lock(g_forceMutex);
            g_cameraSnap = g_inForce;
        }
    }
    return g_origEye(self);
}

// ---- the latch (the provider's beginFrame, once a runtime frame) -----------------------------------------------------------------------------------
// The feature's step for this frame, when the key is not off or its level still runs out. Fills `out` and the atomics the getters read when the runtime
// asked version 8 (only then can it turn the pose back by the tilt); a version 7 runtime gets the gates' answer (reason channel) and the probe leads.
bool latchFeature(uint64_t nowMs, const float* head, uint32_t version, head_aim::Turn* out) {
    const ha::Key key = ha::Key(g_featKey.load(std::memory_order_acquire));
    if (key == ha::Key::Off && g_featState.level <= 0.0) {
        g_featLead.store(false, std::memory_order_relaxed);
        g_featLevel.store(0.0f, std::memory_order_relaxed);
        g_featReason.store(int(ha::Reason::Key), std::memory_order_relaxed);
        return false;
    }
    const ha::PitchComp comp = ha::PitchComp(g_featComp.load(std::memory_order_acquire));
    const bool onFoot = g_onFoot.load(std::memory_order_relaxed);
    ha::Inputs in;
    in.key = key;
    in.nativeOnFoot = in.journalKnown = in.journalOnFoot = onFoot;   // the frame thread's verdict on all three: a failure reads "ship"
    in.player = g_player.load(std::memory_order_relaxed) != 0;
    in.channel8 = version >= EDVR_NATIVE_FRAME_VERSION_8;
    in.menu = g_menu.load(std::memory_order_relaxed);
    in.focusKnown = g_focusKnown.load(std::memory_order_relaxed);
    in.focus = g_focus.load(std::memory_order_relaxed);
    in.head = head;
    in.nowMs = nowMs;
    const ha::Output o = ha::step(g_featState, in);
    const ha::Sync sync = ha::Sync(g_featSync.load(std::memory_order_acquire));
    const ha::StickComp stick = ha::StickComp(g_featStick.load(std::memory_order_acquire));
    const bool lead = in.channel8;
    float pitchAdded = 0.0f;
    g_latchedThisFrame.store(true, std::memory_order_relaxed);
    if (lead) {
        // What the runtime turns the pose back by: off = what angle 1 got on top of the original, on = the head's whole tilt (stick_comp).
        pitchAdded = ha::pitchCorrection(stick, g_lastOrig1.load(std::memory_order_relaxed), o.pitchTarget, o.level);
        const float newPitch = ha::runtimePitch(comp, pitchAdded);
        {
            // The answer first (the snapshot is from the previous latch's angles or older), then the new angles go into force under the next number.
            std::lock_guard<std::mutex> lock(g_forceMutex);
            const ha::Answer ans = ha::chooseAnswer(sync, g_cameraSnap, g_latchNo, o.yawAdded, newPitch);
            out->yawRad = ans.yaw;
            out->pitchRad = ans.pitch;
            ++g_latchNo;
            g_inForce.valid = true;
            g_inForce.latchNo = g_latchNo;
            g_inForce.yaw = o.yawAdded;
            g_inForce.pitch = newPitch;
        }
        g_featYaw.store(o.yawAdded, std::memory_order_relaxed);
        g_featTarget.store(o.pitchTarget, std::memory_order_relaxed);
        g_featLevel.store(o.level, std::memory_order_relaxed);
        g_featPitchAdded.store(pitchAdded, std::memory_order_relaxed);
        g_latchAngle.store(0, std::memory_order_relaxed);   // the probe's shifts are ignored while the feature leads
        g_latchRad.store(0.0f, std::memory_order_relaxed);
        g_latchMs.store(nowMs, std::memory_order_relaxed);
        g_featLead.store(true, std::memory_order_release);
        out->probe = 0;
        out->feature = o.level > 0.0f ? 1u : 0u;   // "acts", for the runtime's line: a feature that only waits at level 0 keeps it quiet
    } else {
        g_featLead.store(false, std::memory_order_relaxed);
        g_featLevel.store(0.0f, std::memory_order_relaxed);
    }
    g_featReason.store(int(o.reason), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_featWindowMutex);
        ha::noteFrame(g_featWindow, o, pitchAdded, version);
    }
    return lead;
}

head_aim::Turn latch(uint64_t, uint64_t nowMs, const float* head, uint32_t requestVersion) {
    head_aim::Turn featureTurn;
    if (latchFeature(nowMs, head, requestVersion, &featureTurn)) return featureTurn;
    const hap::Mode mode = hap::Mode(g_mode.load(std::memory_order_acquire));
    const hap::Comp comp = hap::Comp(g_comp.load(std::memory_order_acquire));
    const bool allowed = g_allowed.load(std::memory_order_acquire);
    const float dt = g_lastLatchMs && nowMs > g_lastLatchMs ? float(nowMs - g_lastLatchMs) / 1000.0f : 0.0f;
    g_lastLatchMs = nowMs;
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const double t = g_qpcFrequency.QuadPart > 0 ? double(c.QuadPart - g_qpcStart.QuadPart) / double(g_qpcFrequency.QuadPart) : 0.0;
    const hap::Shift s = hap::step(g_ramp, mode, allowed, dt, t);
    g_latchAngle.store(s.angle, std::memory_order_relaxed);
    g_latchRad.store(s.rad, std::memory_order_relaxed);
    g_latchMs.store(nowMs, std::memory_order_release);
    const float turn = hap::turnOf(comp, s);
    const uint64_t playerCalls = g_playerCalls1.exchange(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_windowMutex);
        ++g_window.latches;
        if (s.angle) {
            ++g_window.shifted;
            hap::notePerFrame(g_window, playerCalls);
            const double shiftDeg = std::fabs(double(s.rad)) * hap::kDegPerRad, turnDeg = std::fabs(double(turn)) * hap::kDegPerRad;
            if (shiftDeg > g_window.maxShiftDeg) g_window.maxShiftDeg = shiftDeg;
            if (turnDeg > g_window.maxTurnDeg) g_window.maxTurnDeg = turnDeg;
        }
    }
    head_aim::Turn out;
    out.yawRad = turn;
    out.probe = mode != hap::Mode::Off && g_onFoot.load(std::memory_order_relaxed) ? 1u : 0u;
    return out;
}

// ---- reading the game's memory (the frame thread) ------------------------------------------------------------------------------------------------
bool readBytes(uintptr_t at, void* out, size_t n) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool readFields(uintptr_t hmc, hap::Fields& f) {
    float raw[11] = {};
    // +0x6F0 .. +0x718 in one read, then +0x748 .. +0x76B.
    float tail[9] = {};
    if (!hmc || !readBytes(hmc + 0x6F0, raw, sizeof(raw)) || !readBytes(hmc + 0x748, tail, sizeof(tail))) return false;
    f.read = true;
    f.f6f0 = raw[0];   // +0x6F0
    f.f6fc = raw[3];   // +0x6FC
    f.f70c = raw[7];   // +0x70C
    f.f710 = raw[8];   // +0x710
    f.f748 = tail[0];  // +0x748
    f.f754 = tail[3];  // +0x754
    f.f764 = tail[7];  // +0x764
    f.f768 = tail[8];  // +0x768
    return true;
}

bool bytesAre(uintptr_t at, const uint8_t* want, size_t n) {
    uint8_t got[32] = {};
    return n <= sizeof(got) && readBytes(at, got, n) && std::memcmp(got, want, n) == 0;
}
bool slotHolds(uintptr_t slot, uintptr_t want) {
    uintptr_t v = 0;
    return readBytes(slot, &v, sizeof(v)) && v == want;
}
bool exchangeSlot(uintptr_t slot, void* replacement) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), replacement);
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*), old, &ignored);
    return true;
}

void say(const char* line) { Log::get().note("%s", line); }

}  // namespace

void headAimProbeInstall(bool flatProfile) {
    if (g_attempted) return;
    g_attempted = true;
    char line[200];
    if (flatProfile) {
        hap::formatInert("flat profile", line, sizeof(line));
        say(line);
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_base = base;
    if (!slotHolds(base + kSlotAngle1, base + kAngle1) || !slotHolds(base + kSlotAngle2, base + kAngle2) ||
        !bytesAre(base + kAngle1, kAngle1Bytes, sizeof(kAngle1Bytes)) || !bytesAre(base + kAngle2, kAngle2Bytes, sizeof(kAngle2Bytes))) {
        hap::formatInert("the angle getters or their table entries are not 334925's", line, sizeof(line));
        say(line);
        return;
    }
    const bool camera = slotHolds(base + kSlotEyeMatrix, base + kEyeMatrix) && bytesAre(base + kEyeMatrix, kEyeMatrixBytes, sizeof(kEyeMatrixBytes)) &&
                        bytesAre(base + kCameraCall, kCameraCallBytes, sizeof(kCameraCallBytes));
    QueryPerformanceFrequency(&g_qpcFrequency);
    QueryPerformanceCounter(&g_qpcStart);
    g_orig1 = reinterpret_cast<Getter>(base + kAngle1);
    g_orig2 = reinterpret_cast<Getter>(base + kAngle2);
    if (!exchangeSlot(base + kSlotAngle1, reinterpret_cast<void*>(&angle1Detour))) {
        hap::formatInert("the table page would not open for writing", line, sizeof(line));
        say(line);
        return;
    }
    if (!exchangeSlot(base + kSlotAngle2, reinterpret_cast<void*>(&angle2Detour))) {
        exchangeSlot(base + kSlotAngle1, reinterpret_cast<void*>(base + kAngle1));
        hap::formatInert("the table page would not open for writing", line, sizeof(line));
        say(line);
        return;
    }
    if (camera) {
        g_origEye = reinterpret_cast<EyeMatrix>(base + kEyeMatrix);
        g_cameraReturnAddress = base + kCameraReturn;
        g_camera = exchangeSlot(base + kSlotEyeMatrix, reinterpret_cast<void*>(&eyeMatrixDetour));
    }
    g_installed = true;
    head_aim::registerLatch(&latch);
    hap::formatHooked(g_camera, line, sizeof(line));
    say(line);
}

void headAimProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static hap::Mode mode = hap::Mode::Off;
    static hap::Comp comp = hap::Comp::Off;
    static bool nativeOnFoot = true;
    static uint64_t lastIdentifyMs = 0, lastWatchMs = 0, windowBeganMs = 0;
    static uintptr_t player = 0;
    static int lastSource = -1;
    static bool shiftOn = false;
    static int hmcs = 0;
    static bool many = false;
    static hap::CameraCheck camera;
    static uintptr_t cameraHmc = 0;
    static ha::Key featKey = ha::Key::Off;
    static ha::PitchComp featComp = ha::PitchComp::Plus;
    static bool saidFeatKeys = false, featOn = false;
    static ha::Sync featSync = ha::Sync::Latch;
    static ha::StickComp featStick = ha::StickComp::Off;
    static std::string badKey, badComp, badSync, badStick;   // the unknown values already said
    static uint64_t featWindowBeganMs = 0;
    static ha::SyncWindow syncWindow;
    char line[400];

    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string rawMode = cfg.getString("experimental.native_vr_on_foot_head_aim_probe", "off");
        const std::string rawComp = cfg.getString("experimental.native_vr_on_foot_head_aim_probe_compensate", "off");
        bool validMode = true, validComp = true;
        const hap::Mode m = hap::parseMode(rawMode.c_str(), &validMode);
        const hap::Comp c = hap::parseComp(rawComp.c_str(), &validComp);
        static bool saidKeys = false;
        if (!validMode) { hap::formatUnknownKey("native_vr_on_foot_head_aim_probe", rawMode.c_str(), line, sizeof(line)); say(line); }
        if (!validComp) { hap::formatUnknownKey("native_vr_on_foot_head_aim_probe_compensate", rawComp.c_str(), line, sizeof(line)); say(line); }
        if (!saidKeys || m != mode || c != comp) {
            hap::formatKeys(m, c, line, sizeof(line));
            say(line);
            saidKeys = true;
        }
        mode = m;
        comp = c;
        nativeOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);

        // The feature's keys, live like the probe's. Unknown values read off / plus, said once per value.
        const std::string rawFeat = cfg.getString("experimental.native_vr_on_foot_head_aim", "off");
        const std::string rawPitch = cfg.getString("experimental.native_vr_on_foot_head_aim_pitch_comp", "plus");
        const std::string rawSync = cfg.getString("experimental.native_vr_on_foot_head_aim_sync", "latch");
        const std::string rawStick = cfg.getString("experimental.native_vr_on_foot_head_aim_stick_comp", "off");
        bool validFeat = true, validPitch = true, validSync = true, validStick = true;
        const ha::Key fk = ha::parseKey(rawFeat.c_str(), &validFeat);
        const ha::PitchComp fc = ha::parsePitchComp(rawPitch.c_str(), &validPitch);
        const ha::Sync fs = ha::parseSync(rawSync.c_str(), &validSync);
        const ha::StickComp fst = ha::parseStickComp(rawStick.c_str(), &validStick);
        if (!validFeat && rawFeat != badKey) { ha::formatUnknownKey("native_vr_on_foot_head_aim", rawFeat.c_str(), "off", line, sizeof(line)); say(line); }
        if (!validPitch && rawPitch != badComp) { ha::formatUnknownKey("native_vr_on_foot_head_aim_pitch_comp", rawPitch.c_str(), "plus", line, sizeof(line)); say(line); }
        if (!validSync && rawSync != badSync) { ha::formatUnknownKey("native_vr_on_foot_head_aim_sync", rawSync.c_str(), "latch", line, sizeof(line)); say(line); }
        if (!validStick && rawStick != badStick) { ha::formatUnknownKey("native_vr_on_foot_head_aim_stick_comp", rawStick.c_str(), "off", line, sizeof(line)); say(line); }
        badKey = validFeat ? std::string() : rawFeat;
        badComp = validPitch ? std::string() : rawPitch;
        badSync = validSync ? std::string() : rawSync;
        badStick = validStick ? std::string() : rawStick;
        if (!saidFeatKeys || fk != featKey || fc != featComp || fs != featSync || fst != featStick) {
            ha::formatKeys(fk, fc, fs, fst, line, sizeof(line));
            say(line);
            saidFeatKeys = true;
        }
        featKey = fk;
        featComp = fc;
        featSync = fs;
        featStick = fst;
    }

    hap::Gate gate;
    gate.nativeOnFoot = nativeOnFoot;
    gate.journalKnown = journalOnFootKnown();
    gate.onFoot = gate.journalKnown && journalOnFoot();
    gate.installed = g_installed;
    const bool onFoot = hap::onFootFlag(gate);
    const uint64_t now = GetTickCount64();

    // Once a second: who the player is, from the window's census and the camera's eye.
    if (g_installed && now - lastIdentifyMs >= 1000) {
        lastIdentifyMs = now;
        hap::Census c;
        for (int i = 0; i < hap::kSlots; ++i) c.slot[i] = g_census[i].exchange(0, std::memory_order_relaxed);
        c.many = g_censusMany.exchange(false, std::memory_order_relaxed);
        const uintptr_t eye = g_cameraEye.exchange(0, std::memory_order_relaxed);
        const uintptr_t candidate = cameraHmc;
        const bool candidateSeen = g_cameraSeen.exchange(false, std::memory_order_relaxed);
        cameraHmc = 0;
        if (eye && !readBytes(eye + kEyeToHmc, &cameraHmc, sizeof(cameraHmc))) cameraHmc = 0;
        uintptr_t eyeTable = 0, hmcTable = 0;
        camera = hap::CameraCheck{};
        camera.eye = eye != 0;
        camera.eyeTable = eye && readBytes(eye, &eyeTable, sizeof(eyeTable)) && eyeTable == g_base + kEyeTable;
        camera.hmc = cameraHmc != 0;
        camera.hmcTable = cameraHmc && readBytes(cameraHmc, &hmcTable, sizeof(hmcTable)) && hmcTable == g_base + kHmcTable;
        camera.seen = cameraHmc && candidateSeen && candidate == cameraHmc;
        g_cameraCandidate.store(cameraHmc, std::memory_order_relaxed);
        const hap::Identity id = onFoot ? hap::identify(cameraHmc, camera.seen, c) : hap::Identity{};
        hmcs = hap::countOf(c);
        many = c.many;
        player = id.player;
        if (onFoot && mode != hap::Mode::Off && int(id.source) != lastSource) {
            hap::formatIdentity(id, hmcs, many, line, sizeof(line));
            say(line);
            lastSource = int(id.source);
        }
        if (!onFoot) lastSource = -1;
    }
    gate.player = player != 0;
    g_player.store(player, std::memory_order_relaxed);
    g_onFoot.store(onFoot, std::memory_order_relaxed);
    // The feature's gates that only the frame thread can read: a menu layer held on foot, and the journal's GuiFocus (unknown reads open).
    uint32_t focus = 0;
    const bool focusKnown = journalGuiFocus(&focus);
    g_menu.store(onFootMenuHeld(), std::memory_order_relaxed);
    g_focus.store(focus, std::memory_order_relaxed);
    g_focusKnown.store(focusKnown, std::memory_order_relaxed);
    g_featComp.store(int(featComp), std::memory_order_release);
    g_featSync.store(int(featSync), std::memory_order_release);
    g_featStick.store(int(featStick), std::memory_order_release);
    g_featKey.store(int(featKey), std::memory_order_release);
    g_comp.store(int(comp), std::memory_order_release);
    g_mode.store(int(mode), std::memory_order_release);
    g_allowed.store(hap::shiftAllowed(gate), std::memory_order_release);

    // The game frame's beat: this boundary closes a game frame, so take its latch flag and camera counts (always, so a frame never carries over).
    {
        const bool latched = g_latchedThisFrame.exchange(false, std::memory_order_relaxed);
        const uint32_t before = g_camBefore.exchange(0, std::memory_order_relaxed), after = g_camAfter.exchange(0, std::memory_order_relaxed);
        if (featKey != ha::Key::Off && onFoot) ha::noteSyncFrame(syncWindow, before, after, latched);
    }

    // The shift's edges, as the latch made them.
    const bool shifting = g_latchAngle.load(std::memory_order_relaxed) != 0 && now - g_latchMs.load(std::memory_order_relaxed) <= kLatchFreshMs;
    if (shifting != shiftOn) {
        hap::formatShiftEdge(shifting, mode, comp, line, sizeof(line));
        say(line);
        shiftOn = shifting;
    }

    // The feature's edges, as the latch made them: the level leaving 0 and coming back to it (the reason of the first shut gate at the fall).
    const bool featFresh = now - g_latchMs.load(std::memory_order_relaxed) <= kLatchFreshMs;
    const bool featNow = g_featLevel.load(std::memory_order_relaxed) > 0.0f && featFresh;
    if (featNow != featOn) {
        // A latch gone quiet (no runtime asking) is the channel's doing.
        ha::formatEdge(featNow, featFresh ? ha::Reason(g_featReason.load(std::memory_order_relaxed)) : ha::Reason::Channel, line, sizeof(line));
        say(line);
        featOn = featNow;
    }

    // The watch line, twice a second on foot.
    if (mode != hap::Mode::Off && onFoot && now - lastWatchMs >= 500) {
        lastWatchMs = now;
        hap::Fields f;
        const char* src = "none";
        if (player && readFields(player, f)) src = "player";
        else if (camera.hmcTable && readFields(cameraHmc, f)) src = "camera";
        const int angle = g_latchAngle.load(std::memory_order_relaxed);
        const float rad = shifting ? g_latchRad.load(std::memory_order_relaxed) : 0.0f;
        char tail[160];
        hap::formatCamera(camera, tail, sizeof(tail));
        // While the feature leads the angles shown are the ones the game got from it, not the probe's shift.
        float shown1 = angle == 1 ? rad : 0.0f, shown2 = angle == 2 ? rad : 0.0f;
        if (featFresh && g_featLead.load(std::memory_order_relaxed)) {
            shown1 = g_featPitchAdded.load(std::memory_order_relaxed);
            shown2 = g_featYaw.load(std::memory_order_relaxed);
        }
        hap::formatWatch(f, shown1, shown2, hmcs, many, player != 0, line, sizeof(line), src, tail);
        say(line);
    }

    // The summary, every 30 s on foot with the key not off, zeros included.
    if (!windowBeganMs) windowBeganMs = now;
    if (now - windowBeganMs >= 30000) {
        windowBeganMs = now;
        hap::Window w;
        {
            std::lock_guard<std::mutex> lock(g_windowMutex);
            w = g_window;
            g_window = hap::Window{};
        }
        w.calls1 = g_calls1.exchange(0, std::memory_order_relaxed);
        w.calls2 = g_calls2.exchange(0, std::memory_order_relaxed);
        if (mode != hap::Mode::Off && onFoot) {
            hap::formatSummary(w, line, sizeof(line));
            say(line);
        }
    }

    // The feature's summary, every 30 s on foot with its key not off, zeros included (channel=7 or frames=0 then say why nothing acted).
    if (!featWindowBeganMs) featWindowBeganMs = now;
    if (now - featWindowBeganMs >= 30000) {
        featWindowBeganMs = now;
        ha::Window fw;
        {
            std::lock_guard<std::mutex> lock(g_featWindowMutex);
            fw = g_featWindow;
            g_featWindow = ha::Window{};
        }
        fw.stickDeg = double(g_lastOrig1.load(std::memory_order_relaxed)) * ha::kDegPerRad;
        if (featKey != ha::Key::Off && onFoot) {
            ha::formatSummary(fw, line, sizeof(line));
            say(line);
            ha::formatSync(syncWindow, featSync, line, sizeof(line));
            say(line);
        }
        syncWindow = ha::SyncWindow{};
    }
}

}  // namespace edvr
