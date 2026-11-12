// The on-foot scope zoom probe: the frame thread's lines, the read-only FOV hook, the PAUSE snapshot and the lens (scope_zoom_probe.h says what and
// why; scope_zoom_probe_core.h is the pure half).
#include "scope_zoom_probe.h"
#include "scope_zoom_probe_core.h"
#include "on_foot_aim_eye_core.h"
#include "journal_watch.h"
#include "xinput_watch.h"
#include "temporal_pass.h"        // temporalPassArmEyeDump: the snapshot
#include "vr_camera_census.h"     // nativeTemporalEyeGeometry: the eye's tangents
#include "shader_swap.h"
#include "gpu_timing.h"
#include "temporal_shader_bytecode.h"   // kScopeZoomLensBytecode (scope_zoom_shader.h, compiled at build time)
#include "../common/aim_eye_signal.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace edvr {

namespace {

void say(const char* line) { Log::get().note("%s", line); }

// ---- the state the frame thread publishes for the door (render thread) ---------------------------------------------------------------------------
std::atomic<bool> g_keyOn{false};       // the key is census or preview: the door measures the eye's size
std::atomic<bool> g_lensOn{false};      // draw the lens this frame
std::atomic<int> g_lensEye{1};          // the measured eye, 0 left, 1 right: the dominant eye, the right one when there is none
std::atomic<float> g_lensM{1.0f};       // the ramped M
std::mutex g_tanMutex;                  // the dominant eye's tangents, as the frame thread last read them
float g_tans[4] = {};
bool g_tansKnown = false;
// Set by the door, read and cleared by the frame thread.
std::atomic<bool> g_inert{false};       // the lens failed: stand down until the key changes
std::atomic<uint32_t> g_lensFrames{0};
std::atomic<uint32_t> g_regionW{0}, g_regionH{0};   // the dominant eye's region at the door, for the snapshot's centre
std::mutex g_inertMutex;
std::string g_inertReason;

// ---- the FOV hook (334925; scope-zoom-probe design, decision 5, and the report's "Зонд зума: разведка FOV") ------------------------------------
constexpr uintptr_t kSlotFov = 0x51887B8, kAdaptor = 0x1031D90, kFovStep = 0x10748D0;
constexpr uintptr_t kCameraFromArg = 0x150, kCameraFov = 0x30;
const uint8_t kAdaptorBytes[] = {0x33, 0xC0, 0x48, 0x8D, 0x8A, 0xB0, 0xFE, 0xFF, 0xFF, 0x48, 0x85, 0xD2, 0x48, 0x0F, 0x44, 0xC8, 0xE9, 0x2B, 0x2B, 0x04, 0x00};
const uint8_t kFovStepBytes[] = {0x48, 0x8B, 0xC4, 0x53, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x78, 0xC8};

using Adaptor = uintptr_t (*)(void*, void*, void*, void*);
Adaptor g_origAdaptor = nullptr;
bool g_fovInstalled = false, g_fovTried = false;
uintptr_t g_base = 0;
std::atomic<uint32_t> g_fovCalls{0};
std::atomic<uint32_t> g_fovBits{0};
std::atomic<uint32_t> g_fovSeq{0};

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

// The game's call, then the camera's FOV as the step left it. Nothing of the game's is written.
uintptr_t fovDetour(void* a, void* b, void* c, void* d) {
    const uintptr_t r = g_origAdaptor(a, b, c, d);
    g_fovCalls.fetch_add(1, std::memory_order_relaxed);
    if (b) {
        float v = 0.0f;
        if (readBytes(reinterpret_cast<uintptr_t>(b) - kCameraFromArg + kCameraFov, &v, sizeof(v))) {
            uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            if (g_fovBits.exchange(bits, std::memory_order_relaxed) != bits) g_fovSeq.fetch_add(1, std::memory_order_release);
        }
    }
    return r;
}

void fovInstall() {
    if (g_fovInstalled || g_fovTried) return;
    g_fovTried = true;
    char line[200];
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!slotHolds(g_base + kSlotFov, g_base + kAdaptor) || !bytesAre(g_base + kAdaptor, kAdaptorBytes, sizeof(kAdaptorBytes)) ||
        !bytesAre(g_base + kFovStep, kFovStepBytes, sizeof(kFovStepBytes))) {
        szp::formatFovUnread("the game's code at +0x51887B8 / +0x1031D90 / +0x10748D0 is not 334925's; nothing hooked", line, sizeof(line));
        say(line);
        return;
    }
    g_origAdaptor = reinterpret_cast<Adaptor>(g_base + kAdaptor);
    if (!exchangeSlot(g_base + kSlotFov, reinterpret_cast<void*>(&fovDetour))) {
        szp::formatFovUnread("the table page would not open for writing; nothing hooked", line, sizeof(line));
        say(line);
        return;
    }
    g_fovInstalled = true;
    szp::formatFovHooked(line, sizeof(line));
    say(line);
}
// Back to the game's own entry, when the key goes off. A call already inside the detour finishes on the pinned DLL.
void fovUninstall() {
    if (g_fovInstalled && slotHolds(g_base + kSlotFov, reinterpret_cast<uintptr_t>(&fovDetour))) {
        exchangeSlot(g_base + kSlotFov, reinterpret_cast<void*>(g_base + kAdaptor));
        say("scope zoom: fov hook removed (the key is off)");
    }
    g_fovInstalled = false;
    g_fovTried = false;
}

// ---- the frame thread's own state ---------------------------------------------------------------------------------------------------------------
struct Frame {
    uint32_t generation = ~0u;
    bool saidKey = false;
    szp::Key key = szp::Key::Off;
    std::string rawKey;
    bool nativeOnFoot = true;
    oae::Key aimEyeKey = oae::Key::Right;
    // The weapon, as last said.
    bool weaponSaid = false;
    char weapon[48] = {};
    char mods[96] = {};
    bool modsKnown = false;
    bool noWeaponSaid = false;
    szp::Zoom zoom;
    // Aims, the FOV, the lines.
    szp::AdsTrack ads;
    bool lt = false;
    bool fovHad = false;
    float fovRad = 0.0f;
    uint32_t fovSeqSeen = 0;
    bool needsEyeSaid = false;
    bool inertSaid = false;
    // The snapshot.
    bool snapAny = false;
    uint64_t snapMs = 0;
    uint32_t skippedSinceLast = 0;
    // The summary window.
    uint64_t windowMs = 0;
    uint32_t aimsAtWindow = 0, lensAtWindow = 0, fovCallsAtWindow = 0;
    uint32_t snapshots = 0, skipped = 0, failures = 0;
    bool onFoot = false;
    float blend = 0.0f;
    int eye = -1;
};
Frame g_f;

// The lens's GPU price, a ring like the sharpener's: never awaited (the door fills it, the door polls it).
struct QuerySlot {
    GpuTimer timer;
    bool inUse = false;
};
constexpr int kQueryRing = 8;
QuerySlot g_qring[kQueryRing];
std::atomic<uint32_t> g_gpuSamples{0};
std::atomic<uint64_t> g_gpuMicroSum{0}, g_gpuMicroMax{0};

void sayInertOnce(Frame& f) {
    if (f.inertSaid || !g_inert.load(std::memory_order_acquire)) return;
    f.inertSaid = true;
    ++f.failures;
    std::string reason;
    {
        std::lock_guard<std::mutex> lock(g_inertMutex);
        reason = g_inertReason;
    }
    char line[300];
    szp::formatLensInert(reason.c_str(), line, sizeof(line));
    say(line);
}

void readTangents(int eye) {
    float fr[4] = {};
    const bool ok = eye >= 0 && nativeTemporalEyeGeometry(static_cast<uint32_t>(eye), nullptr, fr, nullptr);
    std::lock_guard<std::mutex> lock(g_tanMutex);
    g_tansKnown = ok;
    if (ok) std::memcpy(g_tans, fr, sizeof(g_tans));
}

const char* eyeName(int eye) { return eye == 0 ? "left" : eye == 1 ? "right" : "none"; }

}  // namespace

void scopeZoomProbeFrameBoundary() {
    Frame& f = g_f;
    char line[400];
    const Config& cfg = Config::get();
    if (cfg.generation() != f.generation) {
        f.generation = cfg.generation();
        const std::string raw = cfg.getString("experimental.native_vr_on_foot_scope_zoom", "off");
        bool valid = true;
        const szp::Key now = szp::parseKey(raw.c_str(), &valid);
        if (!f.saidKey || now != f.key || (!valid && raw != f.rawKey)) {
            if (!valid) {
                szp::formatKeyUnknown(raw.c_str(), line, sizeof(line));
                say(line);
            }
            if (!f.saidKey || now != f.key) {
                szp::formatKey(now, line, sizeof(line));
                say(line);
                // A new key: the lens may try again, and the once-lines may be said again.
                g_inert.store(false, std::memory_order_release);
                f.inertSaid = false;
                f.needsEyeSaid = false;
                f.noWeaponSaid = false;
                f.weaponSaid = false;
            }
            f.saidKey = true;
        }
        f.rawKey = raw;
        f.key = now;
        f.nativeOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);
        f.aimEyeKey = oae::parseKey(cfg.getString("experimental.native_vr_on_foot_aim_eye", "right").c_str());
    }

    g_keyOn.store(f.key != szp::Key::Off, std::memory_order_release);
    if (f.key == szp::Key::Off) {
        g_lensOn.store(false, std::memory_order_release);
        if (g_fovInstalled || g_fovTried) fovUninstall();
        f.onFoot = false;
        return;
    }
    fovInstall();

    const bool onFoot = journalOnFootKnown() && journalOnFoot();
    const uint64_t now = GetTickCount64();
    if (!onFoot) {
        g_lensOn.store(false, std::memory_order_release);
        if (f.onFoot) {
            // Off foot: the next disembark says its weapon again.
            f.weaponSaid = false;
            f.noWeaponSaid = false;
            f.weapon[0] = '\0';
        }
        f.onFoot = false;
        return;
    }
    if (!f.onFoot) {
        f.onFoot = true;
        if (!f.windowMs) {
            f.windowMs = now;
            f.aimsAtWindow = f.ads.aims;
            f.lensAtWindow = g_lensFrames.load(std::memory_order_relaxed);
            f.fovCallsAtWindow = g_fovCalls.load(std::memory_order_relaxed);
        }
    }
    const bool cinema = !f.nativeOnFoot;

    // The weapon and its mods, a line on any change.
    char weapon[48] = {};
    const bool weaponKnown = journalSelectedWeapon(weapon, sizeof(weapon));
    if (!weaponKnown && journalStatusSamples() > 0 && !f.noWeaponSaid) {
        f.noWeaponSaid = true;
        szp::formatNoSelectedWeapon(line, sizeof(line));
        say(line);
    }
    char mods[96] = {};
    const bool modsKnown = weaponKnown && journalWeaponMods(weapon, mods, sizeof(mods));
    if (weaponKnown && (!f.weaponSaid || std::strcmp(weapon, f.weapon) != 0 || modsKnown != f.modsKnown || std::strcmp(mods, f.mods) != 0)) {
        f.weaponSaid = true;
        std::memcpy(f.weapon, weapon, sizeof(f.weapon));
        std::memcpy(f.mods, mods, sizeof(f.mods));
        f.modsKnown = modsKnown;
        f.zoom = szp::zoomFor(f.weapon, modsKnown ? f.mods : nullptr);
        szp::formatWeapon(f.weapon, modsKnown, f.mods, f.zoom, line, sizeof(line));
        say(line);
    }
    if (!weaponKnown) {
        f.weapon[0] = '\0';
        f.zoom = szp::Zoom{};
    }

    // LT and the game's flag.
    xinputWatchTick();
    XinputBinding ltBinding;
    ltBinding.trigger = 1;   // the left trigger: HumanoidZoomButton on this pad
    ltBinding.valid = true;
    f.lt = xinputHeld(ltBinding);
    const szp::AdsOut ev = szp::stepAds(f.ads, f.lt, journalAimDownSightKnown(), journalAimDownSight(), now);
    for (int i = 0; i < 2; ++i) {
        if (ev.ev[i] == szp::AdsEvent::None) continue;
        szp::formatAds(ev.ev[i], ev.delayMs[i], f.weapon, f.ads.aims, line, sizeof(line));
        say(line);
    }

    // The game's FOV, a line when it moved by more than half a degree.
    const uint32_t seq = g_fovSeq.load(std::memory_order_acquire);
    if (seq != f.fovSeqSeen) {
        f.fovSeqSeen = seq;
        const uint32_t bits = g_fovBits.load(std::memory_order_relaxed);
        float rad = 0.0f;
        std::memcpy(&rad, &bits, sizeof(rad));
        if (szp::fovChanged(f.fovHad, f.fovRad, rad)) {
            f.fovHad = true;
            f.fovRad = rad;
            szp::formatFov(cinema, f.lt || f.ads.flag, rad, line, sizeof(line));
            say(line);
        }
    }

    // The dominant eye and its blend (the aiming eye publishes them every frame), and the lens's decision.
    const aim_eye::Signal sig = aim_eye::read(now);
    f.eye = sig.eye == aim_eye::kEyeLeft ? 0 : sig.eye == aim_eye::kEyeRight ? 1 : -1;
    f.blend = sig.blend;
    readTangents(f.eye >= 0 ? f.eye : 1);
    sayInertOnce(f);
    bool lens = f.key == szp::Key::Preview && !cinema && f.zoom.inTable && !g_inert.load(std::memory_order_acquire);
    if (lens && f.aimEyeKey == oae::Key::Off) {
        if (f.lt && !f.needsEyeSaid) {
            f.needsEyeSaid = true;
            szp::formatLensNeedsEye(line, sizeof(line));
            say(line);
        }
        lens = false;
    }
    lens = lens && f.eye >= 0 && f.blend > 0.0f;
    g_lensEye.store(f.eye >= 0 ? f.eye : 1, std::memory_order_relaxed);
    g_lensM.store(szp::rampedM(f.zoom.m, f.blend), std::memory_order_relaxed);
    g_lensOn.store(lens, std::memory_order_release);

    // The summary, every 30 s on foot, zeros too.
    if (now - f.windowMs >= szp::kSummaryMs) {
        szp::Summary s;
        s.key = f.key;
        s.cinema = cinema;
        s.aims = f.ads.aims - f.aimsAtWindow;
        const uint32_t lensNow = g_lensFrames.load(std::memory_order_relaxed);
        s.lensFrames = lensNow - f.lensAtWindow;
        s.snapshots = f.snapshots;
        s.skipped = f.skipped;
        s.failures = f.failures;
        const uint32_t callsNow = g_fovCalls.load(std::memory_order_relaxed);
        s.fovCalls = callsNow - f.fovCallsAtWindow;
        s.weapon = f.weapon;
        s.zoom = f.zoom;
        s.fovKnown = f.fovHad;
        s.fovRad = f.fovRad;
        s.gpuSamples = g_gpuSamples.exchange(0, std::memory_order_relaxed);
        s.gpuMsSum = double(g_gpuMicroSum.exchange(0, std::memory_order_relaxed)) / 1000.0;
        s.gpuMsMax = double(g_gpuMicroMax.exchange(0, std::memory_order_relaxed)) / 1000.0;
        szp::formatSummary(s, line, sizeof(line));
        say(line);
        f.windowMs = now;
        f.aimsAtWindow = f.ads.aims;
        f.lensAtWindow = lensNow;
        f.fovCallsAtWindow = callsNow;
        f.snapshots = f.skipped = f.failures = 0;
    }
}

void scopeZoomProbeOnPause() {
    Frame& f = g_f;
    if (f.key == szp::Key::Off || !f.onFoot) return;
    const uint64_t now = GetTickCount64();
    char line[400];
    if (!szp::snapshotAllowed(f.snapAny, f.snapMs, now)) {
        ++f.skipped;
        ++f.skippedSinceLast;
        szp::formatSnapshotSkipped(f.skippedSinceLast, line, sizeof(line));
        say(line);
        return;
    }
    f.snapAny = true;
    f.snapMs = now;
    f.skippedSinceLast = 0;
    ++f.snapshots;
    temporalPassArmEyeDump();
    // The centre on the dominant eye's region as the door last saw it (the right eye's when there is no dominant eye).
    float tans[4] = {};
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(g_tanMutex);
        known = g_tansKnown;
        std::memcpy(tans, g_tans, sizeof(tans));
    }
    const uint32_t w = g_regionW.load(std::memory_order_relaxed), h = g_regionH.load(std::memory_order_relaxed);
    const szp::Lens l = known ? szp::lensFor(tans, w, h) : szp::Lens{};
    szp::formatSnapshot(f.weapon, f.lt || f.ads.flag, eyeName(f.eye >= 0 ? f.eye : 1), l, w, h, g_lensOn.load(std::memory_order_relaxed),
                        g_lensM.load(std::memory_order_relaxed), line, sizeof(line));
    say(line);
}

// ---- the lens (the render thread, in native_sharpen's door) --------------------------------------------------------------------------------------
namespace {

struct LensGpu {
    ID3D11Device* owner = nullptr;
    ID3D11ComputeShader* cs = nullptr;
    bool csTried = false;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    void* srcRes = nullptr;
    ID3D11ShaderResourceView* srcSrv = nullptr;
    ID3D11Texture2D* out = nullptr;
    ID3D11UnorderedAccessView* outUav = nullptr;
    uint32_t outW = 0, outH = 0;
    DXGI_FORMAT outFmt = DXGI_FORMAT_UNKNOWN;
};
LensGpu g_gpu;
FaultBudget g_lensBudget("scopeZoomLens", 4);

struct LensParams {
    float centreRadius[4];
    float params[4];
    int32_t box[4];
    int32_t origin[4];
    float texInv[4];
};
static_assert(sizeof(LensParams) == 80, "five 16-byte rows");

template <class T>
void rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}
void releaseGpu() {
    for (QuerySlot& q : g_qring) { q.timer.reset(); q.inUse = false; }
    rel(g_gpu.srcSrv);
    g_gpu.srcRes = nullptr;
    rel(g_gpu.outUav);
    rel(g_gpu.out);
    g_gpu.outW = g_gpu.outH = 0;
    g_gpu.outFmt = DXGI_FORMAT_UNKNOWN;
    rel(g_gpu.sampler);
    rel(g_gpu.cb);
    rel(g_gpu.cs);
    g_gpu.csTried = false;
    rel(g_gpu.owner);
}

void fail(const char* reason) {
    {
        std::lock_guard<std::mutex> lock(g_inertMutex);
        g_inertReason = reason;
    }
    g_inert.store(true, std::memory_order_release);
    g_lensOn.store(false, std::memory_order_release);
}

// The family's plain typed view, as the sharpener reads and writes it (sharpen_pass.cpp); sRGB and anything unmeasured are refused.
DXGI_FORMAT viewFormatOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

void pollTimers(ID3D11DeviceContext* ctx) {
    if (!gpuTimingOwns(ctx)) return;
    for (QuerySlot& q : g_qring) {
        if (!q.inUse) continue;
        double ms = 0.0;
        const GpuTimerPoll r = q.timer.poll(ctx, ms);
        if (r == GpuTimerPoll::Pending) continue;
        q.inUse = false;
        if (r != GpuTimerPoll::Ready) continue;
        const uint64_t us = static_cast<uint64_t>(ms * 1000.0 + 0.5);
        g_gpuSamples.fetch_add(1, std::memory_order_relaxed);
        g_gpuMicroSum.fetch_add(us, std::memory_order_relaxed);
        uint64_t prev = g_gpuMicroMax.load(std::memory_order_relaxed);
        while (us > prev && !g_gpuMicroMax.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
        }
    }
}
int acquireTimer(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!gpuTimingAccepts(ctx) && !gpuTimingBind(dev, ctx)) return -1;
    for (int i = 0; i < kQueryRing; ++i) {
        if (g_qring[i].inUse) continue;
        if (g_qring[i].timer.begin(dev, ctx)) { g_qring[i].inUse = true; return i; }
        return -1;
    }
    return -1;
}

ID3D11Texture2D* lensInner(ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV) {
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    if (sd.SampleDesc.Count != 1 || sd.ArraySize != 1 || sd.MipLevels != 1) { fail("the eye is a multisampled, array or mipped texture"); return nullptr; }
    const DXGI_FORMAT viewFmt = viewFormatOf(sd.Format);
    if (viewFmt == DXGI_FORMAT_UNKNOWN) {
        char why[96];
        std::snprintf(why, sizeof(why), "the eye's format (DXGI %d) is not one the lens handles", int(sd.Format));
        fail(why);
        return nullptr;
    }
    if (!(sd.BindFlags & D3D11_BIND_SHADER_RESOURCE)) { fail("the eye refuses a shader view"); return nullptr; }
    if (region[2] <= region[0] || region[3] <= region[1] || region[2] > sd.Width || region[3] > sd.Height) { fail("the eye's region is not usable"); return nullptr; }
    const uint32_t w = region[2] - region[0], h = region[3] - region[1];

    float tans[4] = {};
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(g_tanMutex);
        known = g_tansKnown;
        std::memcpy(tans, g_tans, sizeof(tans));
    }
    if (!known) { fail("the eye's tangents are not known (the temporal channel is closed)"); return nullptr; }
    szp::Lens l = szp::lensFor(tans, w, h);
    if (!l.valid) { fail("the eye's tangents give no circle"); return nullptr; }
    // A region stored mirrored: the centre mirrors with it (the radii and the box's size do not).
    if (flipU) {
        l.cx = float(w) - l.cx;
        const int x0 = int(w) - l.x1, x1 = int(w) - l.x0;
        l.x0 = x0; l.x1 = x1;
    }
    if (flipV) {
        l.cy = float(h) - l.cy;
        const int y0 = int(h) - l.y1, y1 = int(h) - l.y0;
        l.y0 = y0; l.y1 = y1;
    }

    ID3D11Device* dev = nullptr;
    src->GetDevice(&dev);
    if (!dev) { fail("the eye has no device"); return nullptr; }
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (!ctx) { dev->Release(); fail("the device has no immediate context"); return nullptr; }
    if (g_gpu.owner != dev) {
        releaseGpu();
        g_gpu.owner = dev;
        dev->AddRef();
    }
    ID3D11Texture2D* result = nullptr;
    do {
        pollTimers(ctx);
        if (!g_gpu.cs && !g_gpu.csTried) {
            g_gpu.csTried = true;
            g_gpu.cs = shaderSwapCreateCs(ctx, kScopeZoomLensBytecode, sizeof(kScopeZoomLensBytecode), "scope_zoom_lens_cs", "scope zoom lens");
        }
        if (!g_gpu.cs) { fail("the lens shader could not be created"); break; }
        if (!g_gpu.cb) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(LensParams);
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_gpu.cb)) || !g_gpu.cb) { fail("the parameter buffer could not be created"); break; }
        }
        if (!g_gpu.sampler) {
            D3D11_SAMPLER_DESC sm{};
            sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sm.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(dev->CreateSamplerState(&sm, &g_gpu.sampler)) || !g_gpu.sampler) { fail("the sampler could not be created"); break; }
        }
        if (g_gpu.srcRes != static_cast<void*>(src) || !g_gpu.srcSrv) {
            rel(g_gpu.srcSrv);
            g_gpu.srcRes = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
            vd.Format = viewFmt;
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = 1;
            if (FAILED(dev->CreateShaderResourceView(src, &vd, &g_gpu.srcSrv)) || !g_gpu.srcSrv) { fail("the eye's shader view could not be created"); break; }
            g_gpu.srcRes = src;
        }
        if (!g_gpu.out || g_gpu.outW != w || g_gpu.outH != h || g_gpu.outFmt != sd.Format) {
            rel(g_gpu.outUav);
            rel(g_gpu.out);
            UINT support = 0;
            if (FAILED(dev->CheckFormatSupport(viewFmt, &support)) || !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) {
                fail("the GPU cannot write the eye's format through a typed unordered view");
                break;
            }
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w;
            td.Height = h;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = sd.Format;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = viewFmt;
            ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_gpu.out)) || !g_gpu.out ||
                FAILED(dev->CreateUnorderedAccessView(g_gpu.out, &ud, &g_gpu.outUav)) || !g_gpu.outUav) {
                rel(g_gpu.outUav);
                rel(g_gpu.out);
                fail("the lens's eye texture could not be created");
                break;
            }
            g_gpu.outW = w;
            g_gpu.outH = h;
            g_gpu.outFmt = sd.Format;
        }

        LensParams p{};
        p.centreRadius[0] = l.cx;
        p.centreRadius[1] = l.cy;
        p.centreRadius[2] = l.rx;
        p.centreRadius[3] = l.ry;
        const float m = g_lensM.load(std::memory_order_relaxed);
        p.params[0] = m > 1.0f ? 1.0f / m : 1.0f;
        p.params[1] = szp::kEdgePx;
        p.box[0] = l.x0; p.box[1] = l.y0; p.box[2] = l.x1; p.box[3] = l.y1;
        p.origin[0] = int32_t(region[0]); p.origin[1] = int32_t(region[1]); p.origin[2] = int32_t(w); p.origin[3] = int32_t(h);
        p.texInv[0] = 1.0f / float(sd.Width);
        p.texInv[1] = 1.0f / float(sd.Height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(g_gpu.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) { fail("the parameter buffer would not map"); break; }
        std::memcpy(mapped.pData, &p, sizeof(p));
        ctx->Unmap(g_gpu.cb, 0);

        const int qs = acquireTimer(dev, ctx);
        D3D11_BOX box{};
        box.left = region[0];
        box.top = region[1];
        box.right = region[2];
        box.bottom = region[3];
        box.front = 0;
        box.back = 1;
        ctx->CopySubresourceRegion(g_gpu.out, 0, 0, 0, 0, src, 0, &box);

        // The compute stage is saved and put back around the dispatch, as the sharpener does.
        ID3D11ComputeShader* savedCs = nullptr;
        ID3D11ShaderResourceView* savedSrv = nullptr;
        ID3D11UnorderedAccessView* savedUav = nullptr;
        ID3D11Buffer* savedCb = nullptr;
        ID3D11SamplerState* savedSampler = nullptr;
        ctx->CSGetShader(&savedCs, nullptr, nullptr);
        ctx->CSGetShaderResources(0, 1, &savedSrv);
        ctx->CSGetUnorderedAccessViews(0, 1, &savedUav);
        ctx->CSGetConstantBuffers(0, 1, &savedCb);
        ctx->CSGetSamplers(0, 1, &savedSampler);
        ctx->CSSetShader(g_gpu.cs, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &g_gpu.cb);
        ctx->CSSetSamplers(0, 1, &g_gpu.sampler);
        ctx->CSSetShaderResources(0, 1, &g_gpu.srcSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &g_gpu.outUav, nullptr);
        ctx->Dispatch(UINT(l.x1 - l.x0 + 7) / 8, UINT(l.y1 - l.y0 + 7) / 8, 1);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11UnorderedAccessView* nullUav = nullptr;
        ctx->CSSetShaderResources(0, 1, &nullSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
        ctx->CSSetShader(savedCs, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &savedSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &savedUav, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &savedCb);
        ctx->CSSetSamplers(0, 1, &savedSampler);
        rel(savedCs);
        rel(savedSrv);
        rel(savedUav);
        rel(savedCb);
        rel(savedSampler);
        if (qs >= 0) g_qring[qs].timer.end(ctx);
        g_lensFrames.fetch_add(1, std::memory_order_relaxed);
        result = g_gpu.out;
    } while (false);
    ctx->Release();
    dev->Release();
    return result;
}

}  // namespace

ID3D11Texture2D* scopeZoomLens(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV) {
    if (!g_keyOn.load(std::memory_order_acquire)) return nullptr;
    if (!src || !region || int(eye) != g_lensEye.load(std::memory_order_relaxed)) return nullptr;
    // The measured eye's size, for the snapshot's centre (census and preview alike).
    if (region[2] > region[0] && region[3] > region[1]) {
        g_regionW.store(region[2] - region[0], std::memory_order_relaxed);
        g_regionH.store(region[3] - region[1], std::memory_order_relaxed);
    }
    if (!g_lensOn.load(std::memory_order_acquire) || g_inert.load(std::memory_order_acquire)) return nullptr;
    ID3D11Texture2D* out = nullptr;
    if (!guardedBudget(g_lensBudget, [&] { out = lensInner(src, region, flipU, flipV); })) {
        fail("the lens faulted");
        return nullptr;
    }
    return out;
}

}  // namespace edvr
