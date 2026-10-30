// The on-foot head aim probe: the table entries, the getters, the latch and the frame thread's step (head_aim_probe.h says what and why).
#include "head_aim_probe.h"
#include "head_aim_probe_core.h"
#include "journal_watch.h"
#include "../common/config.h"
#include "../common/head_aim_signal.h"
#include "../common/log.h"
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
// Written by the latch, read by the getters.
std::atomic<int> g_latchAngle{0};
std::atomic<float> g_latchRad{0.0f};
std::atomic<uint64_t> g_latchMs{0};
// Written by the getters and the eye's matrix getter, taken by the frame thread and the latch.
std::atomic<uintptr_t> g_census[hap::kSlots];
std::atomic<bool> g_censusMany{false};
std::atomic<uintptr_t> g_cameraEye{0};
std::atomic<uint64_t> g_calls1{0}, g_calls2{0}, g_playerCalls1{0};

// The latch's own state (the provider's thread, under its mutex) and the window both it and the frame thread write.
hap::Ramp g_ramp;
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
    if (me != g_player.load(std::memory_order_relaxed) || g_latchAngle.load(std::memory_order_relaxed) != angle) return 0.0f;
    if (GetTickCount64() - g_latchMs.load(std::memory_order_relaxed) > kLatchFreshMs) return 0.0f;
    return g_latchRad.load(std::memory_order_relaxed);
}

float angle1Detour(void* self) {
    const float v = g_orig1(self);
    g_calls1.fetch_add(1, std::memory_order_relaxed);
    if (reinterpret_cast<uintptr_t>(self) == g_player.load(std::memory_order_relaxed)) g_playerCalls1.fetch_add(1, std::memory_order_relaxed);
    return v + shiftFor(self, 1);
}
float angle2Detour(void* self) {
    const float v = g_orig2(self);
    g_calls2.fetch_add(1, std::memory_order_relaxed);
    return v + shiftFor(self, 2);
}
void* eyeMatrixDetour(void* self) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_cameraReturnAddress)
        g_cameraEye.store(reinterpret_cast<uintptr_t>(self), std::memory_order_relaxed);
    return g_origEye(self);
}

// ---- the latch (the provider's beginFrame, once a runtime frame) -----------------------------------------------------------------------------------
head_aim::Turn latch(uint64_t, uint64_t nowMs) {
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
        uintptr_t cameraHmc = 0;
        if (eye && !readBytes(eye + kEyeToHmc, &cameraHmc, sizeof(cameraHmc))) cameraHmc = 0;
        const hap::Identity id = onFoot ? hap::identify(cameraHmc, c) : hap::Identity{};
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
    g_comp.store(int(comp), std::memory_order_release);
    g_mode.store(int(mode), std::memory_order_release);
    g_allowed.store(hap::shiftAllowed(gate), std::memory_order_release);

    // The shift's edges, as the latch made them.
    const bool shifting = g_latchAngle.load(std::memory_order_relaxed) != 0 && now - g_latchMs.load(std::memory_order_relaxed) <= kLatchFreshMs;
    if (shifting != shiftOn) {
        hap::formatShiftEdge(shifting, mode, comp, line, sizeof(line));
        say(line);
        shiftOn = shifting;
    }

    // The watch line, twice a second on foot.
    if (mode != hap::Mode::Off && onFoot && now - lastWatchMs >= 500) {
        lastWatchMs = now;
        hap::Fields f;
        if (player) readFields(player, f);
        const int angle = g_latchAngle.load(std::memory_order_relaxed);
        const float rad = shifting ? g_latchRad.load(std::memory_order_relaxed) : 0.0f;
        hap::formatWatch(f, angle == 1 ? rad : 0.0f, angle == 2 ? rad : 0.0f, hmcs, many, player != 0, line, sizeof(line));
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
}

}  // namespace edvr
