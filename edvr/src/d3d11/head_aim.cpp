// The on-foot head aim: the table entries, the getters, the latch and the frame thread's step (head_aim.h says what and why; head_aim_core.h is the pure half).
#include "head_aim.h"
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
// A latch older than this is not the frame the game is building: the getters hand the angle on untouched.
constexpr uint64_t kLatchFreshMs = 250;

using Getter = float (*)(void*);
using EyeMatrix = void* (*)(void*);

bool g_attempted = false, g_installed = false, g_camera = false;
Getter g_orig1 = nullptr, g_orig2 = nullptr;
EyeMatrix g_origEye = nullptr;
uintptr_t g_cameraReturnAddress = 0;

// Published by the frame thread, read by the latch and the getters.
std::atomic<bool> g_onFoot{false};
std::atomic<uintptr_t> g_player{0};
// The feature (head_aim_core.h), published by the frame thread, read by the latch.
std::atomic<int> g_featKey{int(ha::Key::Off)};
std::atomic<bool> g_menu{false}, g_focusKnown{false};
std::atomic<uint32_t> g_focus{0};
// Written by the latch, read by the getters and the frame thread: the latch's clock.
std::atomic<uint64_t> g_latchMs{0};
// The feature's frame as the latch made it (read by the getters, and by the frame thread for its lines): who leads the frame, the yaw added to angle 2,
// the tilt the head asks angle 1 to be (the game's sign) and the level that blends it in, the tilt added in that frame, and the first shut gate.
std::atomic<bool> g_featLead{false};
std::atomic<float> g_featYaw{0.0f}, g_featTarget{0.0f}, g_featLevel{0.0f}, g_featPitchAdded{0.0f};
std::atomic<int> g_featReason{int(ha::Reason::Key)};
std::atomic<bool> g_featHeld{false};   // a menu or the GUI focus holds the angles (the latch's verdict, for the hold line)
// The player's angle 1 as the game built it, from the last getter call on the player (the summary shows it as the stick's tilt).
std::atomic<float> g_lastOrig1{0.0f};
// The barrel against the view in the latch's last frame (headAimSightLag), radians: right, down.
std::atomic<float> g_sightLagYaw{0.0f}, g_sightLagPitch{0.0f};
// The camera counter: whether this game frame has had its latch, and how many camera builds came before it and after it (the latch sets the flag, the
// frame boundary takes all three).
std::atomic<bool> g_latchedThisFrame{false};
std::atomic<uint32_t> g_camBefore{0}, g_camAfter{0};
// The angles in force (what the latch last published: the yaw added and the tilt for the runtime, with the latch's number) and the copy a camera build
// took of them. One mutex, so a pair is always from one latch.
std::mutex g_forceMutex;
ha::Snapshot g_inForce, g_cameraSnap;
uint64_t g_latchNo = 0;   // under the mutex; counts the feature's latches
// The scope zoom's recording (scope_zoom.h; build 1 of on-foot-scope-zoom, read only): the sights' lag of the last latches by their number, and the
// head's angular speed at the last latch. Under g_forceMutex, like the numbers they go with.
struct LagByLatch {
    uint64_t latchNo = 0;
    float yaw = 0.0f, pitch = 0.0f;
};
constexpr int kLagRing = 16;
LagByLatch g_lagRing[kLagRing];
double g_rateYaw = 0.0, g_ratePitch = 0.0;   // radians a second, the head's (unfiltered) yaw right and pitch up
double g_rateLastYaw = 0.0, g_rateLastPitch = 0.0;
LARGE_INTEGER g_rateLastQpc{};
bool g_rateHave = false;
// Written by the getters and the eye's matrix getter, taken by the frame thread and the latch.
std::atomic<uintptr_t> g_census[ha::kSlots];
std::atomic<bool> g_censusMany{false};
std::atomic<uintptr_t> g_cameraEye{0};
// The camera's HMC as the frame thread last read it, and whether a getter has been called on it since (set by the getters, so a crowd past the
// census's slots cannot hide the player).
std::atomic<uintptr_t> g_cameraCandidate{0};
std::atomic<bool> g_cameraSeen{false};
uintptr_t g_base = 0;

// The latch's own state (the provider's thread, under its mutex) and the window both it and the frame thread write.
ha::State g_featState;   // the feature's filter and level: the latch only
std::mutex g_featWindowMutex;
ha::Window g_featWindow;

void noteCensus(uintptr_t p) {
    for (int i = 0; i < ha::kSlots; ++i) {
        uintptr_t seen = g_census[i].load(std::memory_order_relaxed);
        if (seen == p) return;
        if (seen == 0) {
            if (g_census[i].compare_exchange_strong(seen, p, std::memory_order_relaxed)) return;
            if (seen == p) return;
        }
    }
    g_censusMany.store(true, std::memory_order_relaxed);
}

// Every getter call: the HMC is counted, and flagged if it is the camera's. True when it is the player's.
bool noteCall(void* self) {
    const uintptr_t me = reinterpret_cast<uintptr_t>(self);
    noteCensus(me);
    if (me == g_cameraCandidate.load(std::memory_order_relaxed)) g_cameraSeen.store(true, std::memory_order_relaxed);
    return me == g_player.load(std::memory_order_relaxed);
}

// The feature leads the frame the latch last made, and that frame is fresh.
bool featureLeads() {
    return g_featLead.load(std::memory_order_relaxed) && GetTickCount64() - g_latchMs.load(std::memory_order_relaxed) <= kLatchFreshMs;
}

// The player's angle 1 is remembered, and while the feature leads becomes the head's tilt blended in by the level; angle 2 gets the head's turn on top of
// what the game built. Every other HMC, and every call while the feature does not lead, gets the game's own angle.
float angle1Detour(void* self) {
    const float v = g_orig1(self);
    if (noteCall(self)) {
        g_lastOrig1.store(v, std::memory_order_relaxed);
        if (featureLeads()) return ha::blendPitch(v, g_featTarget.load(std::memory_order_relaxed), g_featLevel.load(std::memory_order_relaxed));
    }
    return v;
}
float angle2Detour(void* self) {
    const float v = g_orig2(self);
    if (noteCall(self) && featureLeads()) return v + g_featYaw.load(std::memory_order_relaxed);
    return v;
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
// asked version 8 (only then can it turn the pose back by the tilt); a version 7 runtime gets the gates' answer (reason channel) and no lead.
bool latchFeature(uint64_t nowMs, const float* head, uint32_t version, head_aim::Turn* out) {
    const ha::Key key = ha::Key(g_featKey.load(std::memory_order_acquire));
    if (key == ha::Key::Off && g_featState.level <= 0.0) {
        g_featLead.store(false, std::memory_order_relaxed);
        g_featLevel.store(0.0f, std::memory_order_relaxed);
        g_featReason.store(int(ha::Reason::Key), std::memory_order_relaxed);
        g_featHeld.store(false, std::memory_order_relaxed);
        return false;
    }
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
    ha::Output o = ha::step(g_featState, in);
    // The pitch offset goes onto the head's tilt here, so the blend, the getters and the pose correction below all carry it.
    o.pitchTarget = ha::offsetTarget(o.pitchTarget);
    const bool lead = in.channel8;
    // The scope zoom's lag (head_aim.h headAimSightLag): the barrel's filtered angles against the head's own, times the level.
    float lagYaw = 0.0f, lagPitch = 0.0f;
    if (lead && g_featState.hasAngles) {
        const double yawLag = ha::detail::wrap(g_featState.filtYaw - g_featState.headYaw);
        const double pitchLagDown = g_featState.headPitchUp - g_featState.filtPitchUp;
        lagYaw = float(yawLag * o.level);
        lagPitch = float(pitchLagDown * o.level);
    }
    g_sightLagYaw.store(lagYaw, std::memory_order_relaxed);
    g_sightLagPitch.store(lagPitch, std::memory_order_relaxed);
    float pitchAdded = 0.0f;
    g_latchedThisFrame.store(true, std::memory_order_relaxed);
    if (lead) {
        // What the runtime turns the pose back by: the head's whole tilt times the level, whatever the stick's tilt is.
        pitchAdded = ha::pitchCorrection(o.pitchTarget, o.level);
        {
            // The answer first (the snapshot is from the previous latch's angles or older), then the new angles go into force under the next number.
            std::lock_guard<std::mutex> lock(g_forceMutex);
            const ha::Answer ans = ha::chooseAnswer(g_cameraSnap, g_latchNo, o.yawAdded, pitchAdded);
            out->yawRad = ans.yaw;
            out->pitchRad = ans.pitch;
            ++g_latchNo;
            g_inForce.valid = true;
            g_inForce.latchNo = g_latchNo;
            g_inForce.yaw = o.yawAdded;
            g_inForce.pitch = pitchAdded;
            // The scope zoom's recording: this latch's lag under its number, and the head's speed since the last latch.
            LagByLatch& r = g_lagRing[g_latchNo % kLagRing];
            r.latchNo = g_latchNo;
            r.yaw = lagYaw;
            r.pitch = lagPitch;
            if (g_featState.hasAngles) {
                LARGE_INTEGER qpc{}, freq{};
                QueryPerformanceCounter(&qpc);
                QueryPerformanceFrequency(&freq);
                if (g_rateHave && freq.QuadPart > 0 && qpc.QuadPart > g_rateLastQpc.QuadPart) {
                    const double dt = double(qpc.QuadPart - g_rateLastQpc.QuadPart) / double(freq.QuadPart);
                    g_rateYaw = ha::detail::wrap(g_featState.headYaw - g_rateLastYaw) / dt;
                    g_ratePitch = (g_featState.headPitchUp - g_rateLastPitch) / dt;
                }
                g_rateHave = true;
                g_rateLastQpc = qpc;
                g_rateLastYaw = g_featState.headYaw;
                g_rateLastPitch = g_featState.headPitchUp;
            }
        }
        g_featYaw.store(o.yawAdded, std::memory_order_relaxed);
        g_featTarget.store(o.pitchTarget, std::memory_order_relaxed);
        g_featLevel.store(o.level, std::memory_order_relaxed);
        g_featPitchAdded.store(pitchAdded, std::memory_order_relaxed);
        g_latchMs.store(nowMs, std::memory_order_relaxed);
        g_featLead.store(true, std::memory_order_release);
        out->probe = 0;   // the channel's probe field (native_frame.h) stays in the layout; nothing sets it any more
        out->feature = o.level > 0.0f ? 1u : 0u;   // "acts", for the runtime's line: a feature that only waits at level 0 keeps it quiet
    } else {
        g_featLead.store(false, std::memory_order_relaxed);
        g_featLevel.store(0.0f, std::memory_order_relaxed);
    }
    g_featReason.store(int(o.reason), std::memory_order_relaxed);
    g_featHeld.store(lead && o.held, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_featWindowMutex);
        ha::noteFrame(g_featWindow, o, pitchAdded, version);
    }
    return lead;
}

head_aim::Turn latch(uint64_t, uint64_t nowMs, const float* head, uint32_t requestVersion) {
    head_aim::Turn turn;
    if (latchFeature(nowMs, head, requestVersion, &turn)) return turn;
    g_latchMs.store(nowMs, std::memory_order_release);   // the latch is alive, whatever it answered: the frame thread's edges read this clock
    return head_aim::Turn{};
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

void headAimInstall(bool flatProfile) {
    if (g_attempted) return;
    g_attempted = true;
    char line[200];
    if (flatProfile) {
        ha::formatInert("flat profile", line, sizeof(line));
        say(line);
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_base = base;
    if (!slotHolds(base + kSlotAngle1, base + kAngle1) || !slotHolds(base + kSlotAngle2, base + kAngle2) ||
        !bytesAre(base + kAngle1, kAngle1Bytes, sizeof(kAngle1Bytes)) || !bytesAre(base + kAngle2, kAngle2Bytes, sizeof(kAngle2Bytes))) {
        ha::formatInert("the angle getters or their table entries are not 334925's", line, sizeof(line));
        say(line);
        return;
    }
    const bool camera = slotHolds(base + kSlotEyeMatrix, base + kEyeMatrix) && bytesAre(base + kEyeMatrix, kEyeMatrixBytes, sizeof(kEyeMatrixBytes)) &&
                        bytesAre(base + kCameraCall, kCameraCallBytes, sizeof(kCameraCallBytes));
    g_orig1 = reinterpret_cast<Getter>(base + kAngle1);
    g_orig2 = reinterpret_cast<Getter>(base + kAngle2);
    if (!exchangeSlot(base + kSlotAngle1, reinterpret_cast<void*>(&angle1Detour))) {
        ha::formatInert("the table page would not open for writing", line, sizeof(line));
        say(line);
        return;
    }
    if (!exchangeSlot(base + kSlotAngle2, reinterpret_cast<void*>(&angle2Detour))) {
        exchangeSlot(base + kSlotAngle1, reinterpret_cast<void*>(base + kAngle1));
        ha::formatInert("the table page would not open for writing", line, sizeof(line));
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
    ha::formatHooked(g_camera, line, sizeof(line));
    say(line);
}

void headAimSightLag(float* yawRightRad, float* pitchDownRad) {
    if (yawRightRad) *yawRightRad = g_sightLagYaw.load(std::memory_order_relaxed);
    if (pitchDownRad) *pitchDownRad = g_sightLagPitch.load(std::memory_order_relaxed);
}

void headAimRecordSample(HeadAimRecord* out) {
    if (!out) return;
    *out = HeadAimRecord{};
    std::lock_guard<std::mutex> lock(g_forceMutex);
    out->latchLast = g_latchNo;
    out->latchCam = g_cameraSnap.valid ? g_cameraSnap.latchNo : 0;
    out->yawRateRad = float(g_rateYaw);
    out->pitchRateRad = float(g_ratePitch);
    const LagByLatch& r = g_lagRing[out->latchCam % kLagRing];
    if (out->latchCam && r.latchNo == out->latchCam) {
        out->camLagKnown = true;
        out->camLagYawRad = r.yaw;
        out->camLagPitchRad = r.pitch;
    }
}

void headAimFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool nativeOnFoot = true;
    static uint64_t lastIdentifyMs = 0;
    static uintptr_t player = 0;
    static int lastSource = -1;
    static int hmcs = 0;
    static bool many = false;
    static ha::CameraCheck camera;
    static uintptr_t cameraHmc = 0;
    static ha::Key featKey = ha::Key::Off;
    static bool saidFeatKeys = false, featOn = false, featHeld = false;
    static std::string badKey;   // the unknown value already said
    static uint64_t featWindowBeganMs = 0;
    static ha::SyncWindow syncWindow;
    char line[400];

    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        nativeOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);

        // The key, live. A missing or empty key reads on; an unknown value (the old `raw` too) reads on and is said once per value.
        const std::string rawFeat = cfg.getString("experimental.native_vr_on_foot_head_aim", "on");
        bool validFeat = true;
        const ha::Key fk = ha::parseKey(rawFeat.c_str(), &validFeat);
        if (!validFeat && rawFeat != badKey) { ha::formatUnknownKey("native_vr_on_foot_head_aim", rawFeat.c_str(), "on", line, sizeof(line)); say(line); }
        badKey = validFeat ? std::string() : rawFeat;
        if (!saidFeatKeys || fk != featKey) {
            ha::formatKeys(fk, line, sizeof(line));
            say(line);
            saidFeatKeys = true;
        }
        featKey = fk;
    }

    ha::Gate gate;
    gate.nativeOnFoot = nativeOnFoot;
    gate.journalKnown = journalOnFootKnown();
    gate.onFoot = gate.journalKnown && journalOnFoot();
    const bool onFoot = ha::onFootFlag(gate);
    const uint64_t now = GetTickCount64();

    // Once a second: who the player is, from the window's census and the camera's eye.
    if (g_installed && now - lastIdentifyMs >= 1000) {
        lastIdentifyMs = now;
        ha::Census c;
        for (int i = 0; i < ha::kSlots; ++i) c.slot[i] = g_census[i].exchange(0, std::memory_order_relaxed);
        c.many = g_censusMany.exchange(false, std::memory_order_relaxed);
        const uintptr_t eye = g_cameraEye.exchange(0, std::memory_order_relaxed);
        const uintptr_t candidate = cameraHmc;
        const bool candidateSeen = g_cameraSeen.exchange(false, std::memory_order_relaxed);
        cameraHmc = 0;
        if (eye && !readBytes(eye + kEyeToHmc, &cameraHmc, sizeof(cameraHmc))) cameraHmc = 0;
        uintptr_t eyeTable = 0, hmcTable = 0;
        camera = ha::CameraCheck{};
        camera.eye = eye != 0;
        camera.eyeTable = eye && readBytes(eye, &eyeTable, sizeof(eyeTable)) && eyeTable == g_base + kEyeTable;
        camera.hmc = cameraHmc != 0;
        camera.hmcTable = cameraHmc && readBytes(cameraHmc, &hmcTable, sizeof(hmcTable)) && hmcTable == g_base + kHmcTable;
        camera.seen = cameraHmc && candidateSeen && candidate == cameraHmc;
        g_cameraCandidate.store(cameraHmc, std::memory_order_relaxed);
        const ha::Identity id = onFoot ? ha::identify(cameraHmc, camera.seen, c) : ha::Identity{};
        hmcs = ha::countOf(c);
        many = c.many;
        player = id.player;
        if (onFoot && featKey != ha::Key::Off && int(id.source) != lastSource) {
            ha::formatIdentity(id, hmcs, many, line, sizeof(line));
            say(line);
            lastSource = int(id.source);
        }
        if (!onFoot) lastSource = -1;
    }
    g_player.store(player, std::memory_order_relaxed);
    g_onFoot.store(onFoot, std::memory_order_relaxed);
    // The feature's gates that only the frame thread can read: a menu layer held on foot, and the journal's GuiFocus (unknown reads open).
    uint32_t focus = 0;
    const bool focusKnown = journalGuiFocus(&focus);
    g_menu.store(onFootMenuHeld(), std::memory_order_relaxed);
    g_focus.store(focus, std::memory_order_relaxed);
    g_focusKnown.store(focusKnown, std::memory_order_relaxed);
    g_featKey.store(int(featKey), std::memory_order_release);

    // The game frame's beat: this boundary closes a game frame, so take its latch flag and camera counts (always, so a frame never carries over).
    {
        const bool latched = g_latchedThisFrame.exchange(false, std::memory_order_relaxed);
        const uint32_t before = g_camBefore.exchange(0, std::memory_order_relaxed), after = g_camAfter.exchange(0, std::memory_order_relaxed);
        if (featKey != ha::Key::Off && onFoot) ha::noteSyncFrame(syncWindow, before, after, latched);
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
    // A hold on a menu or the GUI focus keeps the level, so it has its own edges: "hold (reason)" at the start, "on" when the way back to the live angles
    // begins (only if the gates have opened; a hold cut short by another gate ends with that gate's "off" line).
    const bool heldNow = g_featHeld.load(std::memory_order_relaxed) && featFresh;
    if (heldNow != featHeld) {
        const ha::Reason why = ha::Reason(g_featReason.load(std::memory_order_relaxed));
        if (heldNow) ha::formatHold(why, line, sizeof(line));
        else if (featNow && why == ha::Reason::None) ha::formatEdge(true, why, line, sizeof(line));
        else line[0] = '\0';
        if (line[0]) say(line);
        featHeld = heldNow;
    }

    // The summary, every 30 s on foot with the key not off, zeros included (channel=7 or frames=0 then say why nothing acted).
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
            ha::formatSync(syncWindow, line, sizeof(line));
            say(line);
        }
        syncWindow = ha::SyncWindow{};
    }
}

}  // namespace edvr
