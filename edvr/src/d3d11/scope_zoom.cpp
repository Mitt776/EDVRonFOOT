// The on-foot scope zoom: the frame thread's lines, the read-only FOV hook, the lens, and (TEMPORARY, build 1 of on-foot-scope-zoom) the PAUSE
// recording (scope_zoom.h says what and why; scope_zoom_core.h is the pure half).
#include "scope_zoom.h"
#include "scope_zoom_core.h"
#include "on_foot_aim_eye_core.h"
#include "head_aim_core.h"        // kPitchOffsetDeg: where the sights sit below the eye's axis
#include "head_aim.h"             // headAimSightLag: the barrel's lag behind the head in a turn; headAimRecordSample: the recording's numbers
#include "journal_watch.h"
#include "xinput_watch.h"
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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace edvr {

namespace {

void say(const char* line) { Log::get().note("%s", line); }

double qpcMs() {
    LARGE_INTEGER c{}, f{};
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? double(c.QuadPart) * 1000.0 / double(f.QuadPart) : 0.0;
}

// ---- the state the frame thread publishes for the door (render thread) ---------------------------------------------------------------------------
std::atomic<bool> g_keyOn{false};       // the key is on
std::atomic<bool> g_lensOn{false};      // draw the lens this frame
std::atomic<int> g_lensEye{1};          // the dominant eye, 0 left, 1 right; the right one when there is none
std::atomic<float> g_lensM{1.0f};       // the M the door last drew (from the game's FOV), for the aim's line
std::atomic<float> g_tableM{1.0f};      // the table's M for the weapon in hand: the cap on the M the game's FOV gives
std::atomic<const scz::Glass*> g_glass{nullptr};   // the weapon's glass, or null
std::atomic<float> g_pitchDownDeg{0.0f};   // how far below the eye's axis the sights sit: the head aim's pitch offset while it is on
std::atomic<double> g_ltDownMs{0.0};    // qpcMs() at the LT press while LT is held, 0 while it is up (the recording's clock)
std::mutex g_tanMutex;                  // the dominant eye's tangents, as the frame thread last read them
float g_tans[4] = {};
bool g_tansKnown = false;
// Set by the door, read and cleared by the frame thread.
std::atomic<bool> g_inert{false};       // the lens failed: stand down until the key changes
std::atomic<uint32_t> g_lensFrames{0};
std::mutex g_inertMutex;
std::string g_inertReason;
// The recording (TEMPORARY, build 1): armed by PAUSE on the frame thread, run by the door.
std::atomic<bool> g_recArm{false};
std::atomic<bool> g_recRunning{false};

// ---- the FOV hook (334925; scope-zoom-probe design, decision 5, and the report's "Зонд зума: разведка FOV") ------------------------------------
constexpr uintptr_t kSlotFov = 0x51887B8, kAdaptor = 0x1031D90, kFovStep = 0x10748D0;
constexpr uintptr_t kCameraFromArg = 0x150, kCameraFov = 0x30;
const uint8_t kAdaptorBytes[] = {0x33, 0xC0, 0x48, 0x8D, 0x8A, 0xB0, 0xFE, 0xFF, 0xFF, 0x48, 0x85, 0xD2, 0x48, 0x0F, 0x44, 0xC8, 0xE9, 0x2B, 0x2B, 0x04, 0x00};
const uint8_t kFovStepBytes[] = {0x48, 0x8B, 0xC4, 0x53, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x78, 0xC8};

using Adaptor = uintptr_t (*)(void*, void*, void*, void*);
Adaptor g_origAdaptor = nullptr;
bool g_fovInstalled = false, g_fovTried = false;
uintptr_t g_base = 0;
std::atomic<uint32_t> g_fovBits{0};

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
    if (b) {
        float v = 0.0f;
        if (readBytes(reinterpret_cast<uintptr_t>(b) - kCameraFromArg + kCameraFov, &v, sizeof(v))) {
            uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            g_fovBits.store(bits, std::memory_order_relaxed);
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
        scz::formatFovUnread("the game's code at +0x51887B8 / +0x1031D90 / +0x10748D0 is not 334925's; nothing hooked, no lens", line, sizeof(line));
        say(line);
        return;
    }
    g_origAdaptor = reinterpret_cast<Adaptor>(g_base + kAdaptor);
    if (!exchangeSlot(g_base + kSlotFov, reinterpret_cast<void*>(&fovDetour))) {
        scz::formatFovUnread("the table page would not open for writing; nothing hooked, no lens", line, sizeof(line));
        say(line);
        return;
    }
    g_fovInstalled = true;
    scz::formatFovHooked(line, sizeof(line));
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
    g_fovBits.store(0, std::memory_order_relaxed);
}

// ---- the frame thread's own state ---------------------------------------------------------------------------------------------------------------
struct Frame {
    uint32_t generation = ~0u;
    bool saidKey = false;
    scz::Key key = scz::Key::Off;
    std::string rawKey;
    bool nativeOnFoot = true;
    oae::Key aimEyeKey = oae::Key::Right;
    // The weapon, as last said.
    bool weaponSaid = false;
    char weapon[48] = {};
    char mods[96] = {};
    bool modsKnown = false;
    bool noWeaponSaid = false;
    scz::Zoom zoom;
    // The aim and the lines.
    scz::Aim aim;
    bool lt = false;
    bool needsEyeSaid = false;
    bool inertSaid = false;
    // The summary window.
    uint64_t windowMs = 0;
    uint32_t aimsAtWindow = 0, lensAtWindow = 0;
    uint32_t failures = 0;
    bool onFoot = false;
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
    scz::formatLensInert(reason.c_str(), line, sizeof(line));
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

// The lens's geometry this moment: the sights at the head aim's offset plus its lag (head_aim.h headAimSightLag), the weapon's glass.
// `withLag` false: the offset alone (the recording's still crop).
scz::Lens lensNow(const float tans[4], uint32_t w, uint32_t h, bool withLag = true) {
    float lagYaw = 0.0f, lagPitch = 0.0f;
    if (withLag && g_pitchDownDeg.load(std::memory_order_relaxed) != 0.0f) headAimSightLag(&lagYaw, &lagPitch);
    return scz::lensFor(tans, w, h, scz::kRadiusDeg, g_pitchDownDeg.load(std::memory_order_relaxed) + scz::radToDeg(lagPitch),
                        scz::radToDeg(lagYaw), g_glass.load(std::memory_order_relaxed));
}

float fovNowRad() {
    const uint32_t bits = g_fovBits.load(std::memory_order_relaxed);
    float fov = 0.0f;
    std::memcpy(&fov, &bits, sizeof(fov));
    return fov;
}
// The zoom the game is at now, capped at the table's (1 when the FOV is not known or not narrowed).
float zoomNow() { return scz::zoomFromFov(fovNowRad(), g_tableM.load(std::memory_order_relaxed)); }

}  // namespace

void scopeZoomFrameBoundary() {
    Frame& f = g_f;
    char line[400];
    const Config& cfg = Config::get();
    if (cfg.generation() != f.generation) {
        f.generation = cfg.generation();
        const std::string raw = cfg.getString("experimental.native_vr_on_foot_scope_zoom", "on");
        bool valid = true;
        const scz::Key now = scz::parseKey(raw.c_str(), &valid);
        if (!f.saidKey || now != f.key || (!valid && raw != f.rawKey)) {
            if (!valid) {
                scz::formatKeyUnknown(raw.c_str(), line, sizeof(line));
                say(line);
            }
            if (!f.saidKey || now != f.key) {
                scz::formatKey(now, line, sizeof(line));
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
        // The head aim puts the barrel, and so the sights, kPitchOffsetDeg below the head's tilt (head_aim_core.h); without it the barrel
        // follows the stick and the eye's axis is the best guess (flight 13.11: the sights 192 px, 9.8 deg, below the axis with it on).
        const bool headAim = ha::parseKey(cfg.getString("experimental.native_vr_on_foot_head_aim", "on").c_str()) == ha::Key::On;
        g_pitchDownDeg.store(headAim ? float(ha::kPitchOffsetDeg) : 0.0f, std::memory_order_relaxed);
    }

    g_keyOn.store(f.key != scz::Key::Off, std::memory_order_release);
    if (f.key == scz::Key::Off) {
        g_lensOn.store(false, std::memory_order_release);
        g_recArm.store(false, std::memory_order_relaxed);
        g_ltDownMs.store(0.0, std::memory_order_relaxed);
        if (g_fovInstalled || g_fovTried) fovUninstall();
        f.onFoot = false;
        f.aim.active = false;
        return;
    }
    fovInstall();

    const bool onFoot = journalOnFootKnown() && journalOnFoot();
    const uint64_t now = GetTickCount64();
    if (!onFoot) {
        g_lensOn.store(false, std::memory_order_release);
        g_ltDownMs.store(0.0, std::memory_order_relaxed);
        if (f.onFoot) {
            // Off foot: the next disembark says its weapon again.
            f.weaponSaid = false;
            f.noWeaponSaid = false;
            f.weapon[0] = '\0';
            f.aim.active = false;
        }
        f.onFoot = false;
        return;
    }
    if (!f.onFoot) {
        f.onFoot = true;
        if (!f.windowMs) {
            f.windowMs = now;
            f.aimsAtWindow = f.aim.aims;
            f.lensAtWindow = g_lensFrames.load(std::memory_order_relaxed);
        }
    }
    const bool cinema = !f.nativeOnFoot;

    // The weapon and its mods, a line on any change.
    char weapon[48] = {};
    const bool weaponKnown = journalSelectedWeapon(weapon, sizeof(weapon));
    if (!weaponKnown && journalStatusSamples() > 0 && !f.noWeaponSaid) {
        f.noWeaponSaid = true;
        scz::formatNoSelectedWeapon(line, sizeof(line));
        say(line);
    }
    char mods[96] = {};
    const bool modsKnown = weaponKnown && journalWeaponMods(weapon, mods, sizeof(mods));
    if (weaponKnown && (!f.weaponSaid || std::strcmp(weapon, f.weapon) != 0 || modsKnown != f.modsKnown || std::strcmp(mods, f.mods) != 0)) {
        f.weaponSaid = true;
        std::memcpy(f.weapon, weapon, sizeof(f.weapon));
        std::memcpy(f.mods, mods, sizeof(f.mods));
        f.modsKnown = modsKnown;
        f.zoom = scz::zoomFor(f.weapon, modsKnown ? f.mods : nullptr);
        scz::formatWeapon(f.weapon, modsKnown, f.mods, f.zoom, line, sizeof(line));
        say(line);
    }
    if (!weaponKnown) {
        f.weapon[0] = '\0';
        f.zoom = scz::Zoom{};
    }

    // LT: the aim's edges, and the recording's clock.
    xinputWatchTick();
    XinputBinding ltBinding;
    ltBinding.trigger = 1;   // the left trigger: HumanoidZoomButton on this pad
    ltBinding.valid = true;
    const bool lt = xinputHeld(ltBinding);
    if (lt && !f.lt) g_ltDownMs.store(qpcMs(), std::memory_order_relaxed);
    if (!lt) g_ltDownMs.store(0.0, std::memory_order_relaxed);
    f.lt = lt;
    // One line per aim, for a weapon with a lens only (an aim of the C-44 is no business of this module's).
    const scz::AimEnd end = scz::stepAim(f.aim, lt && f.zoom.inTable, g_lensFrames.load(std::memory_order_relaxed),
                                         g_lensM.load(std::memory_order_relaxed), now);
    if (end.ended) {
        scz::formatAim(f.weapon, end, line, sizeof(line));
        say(line);
    }

    // The dominant eye (the aiming eye publishes it every frame), and the lens's decision.
    const aim_eye::Signal sig = aim_eye::read(now);
    f.eye = sig.eye == aim_eye::kEyeLeft ? 0 : sig.eye == aim_eye::kEyeRight ? 1 : -1;
    readTangents(f.eye >= 0 ? f.eye : 1);
    sayInertOnce(f);
    const scz::Glass* glass = scz::glassFor(f.weapon);
    bool lens = !cinema && f.zoom.inTable && glass && !g_inert.load(std::memory_order_acquire);
    if (lens && f.aimEyeKey == oae::Key::Off) {
        if (f.lt && !f.needsEyeSaid) {
            f.needsEyeSaid = true;
            scz::formatLensNeedsEye(line, sizeof(line));
            say(line);
        }
        lens = false;
    }
    // The zoom itself follows the game's own FOV in the door (scz::zoomFromFov), so the lens grows as the weapon comes up; here only whether it may.
    lens = lens && f.eye >= 0;
    g_lensEye.store(f.eye >= 0 ? f.eye : 1, std::memory_order_relaxed);
    g_tableM.store(f.zoom.inTable ? f.zoom.m : 1.0f, std::memory_order_relaxed);
    g_glass.store(glass, std::memory_order_relaxed);
    g_lensOn.store(lens, std::memory_order_release);

    // The summary, every 60 s on foot, said only for a window with aims in it.
    if (scz::summaryDue(f.windowMs, now)) {
        const uint32_t lensFrames = g_lensFrames.load(std::memory_order_relaxed);
        scz::Summary s;
        s.aims = f.aim.aims - f.aimsAtWindow;
        s.lensFrames = lensFrames - f.lensAtWindow;
        s.failures = f.failures;
        s.weapon = f.weapon;
        s.gpuSamples = g_gpuSamples.exchange(0, std::memory_order_relaxed);
        s.gpuMsSum = double(g_gpuMicroSum.exchange(0, std::memory_order_relaxed)) / 1000.0;
        s.gpuMsMax = double(g_gpuMicroMax.exchange(0, std::memory_order_relaxed)) / 1000.0;
        if (s.aims) {
            scz::formatSummary(s, line, sizeof(line));
            say(line);
        }
        f.windowMs = now;
        f.aimsAtWindow = f.aim.aims;
        f.lensAtWindow = lensFrames;
        f.failures = 0;
    }
}

bool scopeZoomOnPause() {
    const Frame& f = g_f;
    if (f.key == scz::Key::Off || !f.onFoot) return false;
    if (g_recRunning.load(std::memory_order_acquire) || g_recArm.load(std::memory_order_relaxed)) {
        say("scope zoom: PAUSE ignored: a recording is already under way");
        return true;
    }
    g_recArm.store(true, std::memory_order_release);
    say("scope zoom: recording armed (PAUSE); it begins at the dominant eye's next frame");
    return true;
}

// ---- the recording (TEMPORARY, build 1 of on-foot-scope-zoom; the render thread, in native_sharpen's door) ---------------------------------------
namespace {

constexpr int kRecRing = 4;            // staging copies in flight
constexpr uint32_t kRecMapAfter = 3;   // frames before a copy is asked for without waiting
constexpr uint32_t kRecGiveUp = 8;     // frames after which a copy still not ready is dropped

struct RecSlot {
    ID3D11Texture2D* tex = nullptr;
    bool busy = false;
    uint32_t frame = 0;   // the recording frame it holds
};
struct RecCrop {
    uint32_t no = 0;
    std::vector<uint8_t> bgr;   // bottom-up BMP rows, kCropPx wide, 24 bits
};
struct Rec {
    bool running = false;
    double startMs = 0;
    uint32_t frames = 0, crops = 0, skipped = 0;
    std::wstring dir;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ID3D11Device* dev = nullptr;
    RecSlot ring[kRecRing];
    int cropX = 0, cropY = 0;
    bool cropOk = false;
    std::vector<std::unique_ptr<RecCrop>> done;
    std::vector<std::string> lines;
};
Rec g_rec;

float halfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1u, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    float v;
    if (e == 0) v = float(m) / 1024.0f * 6.103515625e-5f;
    else if (e == 31) v = m ? 0.0f : 65504.0f;
    else v = (1.0f + float(m) / 1024.0f) * std::pow(2.0f, float(e) - 15.0f);
    return s ? -v : v;
}
uint8_t toByte(float v, bool linear) {
    if (!(v > 0.0f)) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (linear) v = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(v * 255.0f + 0.5f);
}
// One mapped crop into BMP rows; false for a format it does not read (the lens's own list).
bool convertCrop(const D3D11_MAPPED_SUBRESOURCE& ms, DXGI_FORMAT fmt, std::vector<uint8_t>& out) {
    const uint32_t n = uint32_t(scz::kCropPx), rowBytes = (n * 3u + 3u) & ~3u;
    out.assign(size_t(rowBytes) * n, 0);
    for (uint32_t y = 0; y < n; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(ms.pData) + size_t(y) * ms.RowPitch;
        uint8_t* dst = out.data() + size_t(n - 1u - y) * rowBytes;
        for (uint32_t x = 0; x < n; ++x) {
            float r = 0, g = 0, b = 0;
            bool linear = false;
            switch (fmt) {
                case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                case DXGI_FORMAT_R8G8B8A8_UNORM: r = src[x * 4] / 255.0f; g = src[x * 4 + 1] / 255.0f; b = src[x * 4 + 2] / 255.0f; break;
                case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                case DXGI_FORMAT_B8G8R8A8_UNORM: b = src[x * 4] / 255.0f; g = src[x * 4 + 1] / 255.0f; r = src[x * 4 + 2] / 255.0f; break;
                case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                case DXGI_FORMAT_R10G10B10A2_UNORM: {
                    uint32_t v = 0;
                    std::memcpy(&v, src + x * 4, 4);
                    r = float(v & 1023u) / 1023.0f;
                    g = float((v >> 10) & 1023u) / 1023.0f;
                    b = float((v >> 20) & 1023u) / 1023.0f;
                    break;
                }
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                case DXGI_FORMAT_R16G16B16A16_FLOAT: {
                    uint16_t hv[3];
                    std::memcpy(hv, src + x * 8, 6);
                    r = halfToFloat(hv[0]); g = halfToFloat(hv[1]); b = halfToFloat(hv[2]);
                    linear = true;
                    break;
                }
                case DXGI_FORMAT_R16G16B16A16_UNORM: {
                    uint16_t hv[3];
                    std::memcpy(hv, src + x * 8, 6);
                    r = hv[0] / 65535.0f; g = hv[1] / 65535.0f; b = hv[2] / 65535.0f;
                    break;
                }
                default: return false;
            }
            dst[x * 3 + 0] = toByte(b, linear);
            dst[x * 3 + 1] = toByte(g, linear);
            dst[x * 3 + 2] = toByte(r, linear);
        }
    }
    return true;
}

bool writeBmp(const std::wstring& path, const std::vector<uint8_t>& rows) {
    const uint32_t n = uint32_t(scz::kCropPx), bytes = uint32_t(rows.size());
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + bytes;
    ih.biSize = sizeof(ih);
    ih.biWidth = LONG(n);
    ih.biHeight = LONG(n);
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = bytes;
    DWORD wrote = 0;
    bool ok = WriteFile(f, &fh, sizeof(fh), &wrote, nullptr) != 0;
    if (ok) ok = WriteFile(f, &ih, sizeof(ih), &wrote, nullptr) != 0;
    if (ok) ok = WriteFile(f, rows.data(), bytes, &wrote, nullptr) != 0;
    CloseHandle(f);
    return ok;
}

void recRelease() {
    for (RecSlot& s : g_rec.ring) {
        if (s.tex) s.tex->Release();
        s = RecSlot{};
    }
    if (g_rec.dev) g_rec.dev->Release();
    g_rec.dev = nullptr;
}

// The end: the line, then the files on a thread of their own (the door never writes a file).
void recFinish() {
    char line[300];
    scz::formatRecordEnd(g_rec.frames, g_rec.crops, g_rec.skipped, line, sizeof(line));
    say(line);
    recRelease();
    auto crops = std::make_shared<std::vector<std::unique_ptr<RecCrop>>>(std::move(g_rec.done));
    auto lines = std::make_shared<std::vector<std::string>>(std::move(g_rec.lines));
    const std::wstring dir = g_rec.dir;
    g_rec.done.clear();
    g_rec.lines.clear();
    g_rec.running = false;
    g_recRunning.store(false, std::memory_order_release);
    try {
        std::thread([crops, lines, dir] {
            uint32_t written = 0;
            for (const auto& c : *crops) {
                wchar_t name[64];
                _snwprintf_s(name, 64, _TRUNCATE, L"\\crop_%03u.bmp", c->no);
                if (writeBmp(dir + name, c->bgr)) ++written;
            }
            const std::wstring txt = dir + L"\\frames.txt";
            if (FILE* f = _wfopen(txt.c_str(), L"wb")) {
                for (const std::string& l : *lines) std::fprintf(f, "%s\n", l.c_str());
                std::fclose(f);
            }
            Log::get().note("scope zoom: recording written: %u of %u crops and frames.txt into %ls", written, unsigned(crops->size()), dir.c_str());
        }).detach();
    } catch (...) {
        say("scope zoom: recording NOT written: the writer thread would not start");
    }
}

// Asks the copies in flight for their pixels, never waiting.
void recPoll(ID3D11DeviceContext* ctx) {
    for (RecSlot& s : g_rec.ring) {
        if (!s.busy || g_rec.frames - s.frame < kRecMapAfter) continue;
        D3D11_MAPPED_SUBRESOURCE ms{};
        const HRESULT hr = ctx->Map(s.tex, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
            if (g_rec.frames - s.frame >= kRecGiveUp) { s.busy = false; ++g_rec.skipped; }
            continue;
        }
        if (FAILED(hr) || !ms.pData) { s.busy = false; ++g_rec.skipped; continue; }
        auto c = std::make_unique<RecCrop>();
        c->no = s.frame;
        const bool ok = convertCrop(ms, g_rec.format, c->bgr);
        ctx->Unmap(s.tex, 0);
        s.busy = false;
        if (ok) { g_rec.done.push_back(std::move(c)); ++g_rec.crops; }
        else ++g_rec.skipped;
    }
}

void recBegin(ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV, int eye) {
    char line[400];
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    const uint32_t w = region[2] - region[0], h = region[3] - region[1];
    float tans[4] = {};
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(g_tanMutex);
        known = g_tansKnown;
        std::memcpy(tans, g_tans, sizeof(tans));
    }
    // The crop stands still around the aiming centre without the lag: the head aim's offset, the glass's reticle when a weapon has one.
    const scz::Lens still = known ? lensNow(tans, w, h, false) : scz::Lens{};
    const float cx = still.valid ? still.cx : 0.5f * float(w), cy = still.valid ? still.cy : 0.5f * float(h);
    g_rec = Rec{};
    if (!scz::cropOrigin(cx, cy, w, h, scz::kCropPx, &g_rec.cropX, &g_rec.cropY)) {
        say("scope zoom: recording not begun: the eye is smaller than the crop");
        return;
    }
    src->GetDevice(&g_rec.dev);
    if (!g_rec.dev) { say("scope zoom: recording not begun: the eye has no device"); return; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = UINT(scz::kCropPx);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = sd.Format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (RecSlot& s : g_rec.ring) {
        if (FAILED(g_rec.dev->CreateTexture2D(&td, nullptr, &s.tex)) || !s.tex) {
            recRelease();
            say("scope zoom: recording not begun: the staging copies could not be created");
            return;
        }
    }
    SYSTEMTIME stm{};
    GetLocalTime(&stm);
    wchar_t stamp[16];
    _snwprintf_s(stamp, 16, _TRUNCATE, L"%02u%02u%02u", unsigned(stm.wHour), unsigned(stm.wMinute), unsigned(stm.wSecond));
    const std::wstring root = Log::get().dir() + L"\\scope_zoom";
    CreateDirectoryW(root.c_str(), nullptr);
    g_rec.dir = root + L"\\" + stamp;
    CreateDirectoryW(g_rec.dir.c_str(), nullptr);
    g_rec.format = sd.Format;
    g_rec.cropOk = true;
    g_rec.running = true;
    g_rec.startMs = qpcMs();
    g_rec.lines.reserve(80);
    g_recRunning.store(true, std::memory_order_release);
    char dirUtf8[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_rec.dir.c_str(), -1, dirUtf8, int(sizeof(dirUtf8)), nullptr, nullptr);
    scz::formatRecordBegin(dirUtf8, eyeName(eye), w, h, int(sd.Format), flipU, flipV, line, sizeof(line));
    say(line);
    std::snprintf(line, sizeof(line), "region=%ux%u format=%d flip=%d,%d crop_at=%d,%d crop=%d", w, h, int(sd.Format), flipU ? 1 : 0, flipV ? 1 : 0,
                  g_rec.cropX, g_rec.cropY, scz::kCropPx);
    g_rec.lines.emplace_back(line);
}

void recFrame(ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV) {
    ID3D11DeviceContext* ctx = nullptr;
    g_rec.dev->GetImmediateContext(&ctx);
    if (!ctx) { recFinish(); return; }
    recPoll(ctx);
    const double now = qpcMs();
    const double t = now - g_rec.startMs;
    if (t < double(scz::kRecordMs)) {
        const uint32_t w = region[2] - region[0], h = region[3] - region[1];
        scz::RecordFrame r;
        r.n = g_rec.frames;
        r.tMs = t;
        const double lt = g_ltDownMs.load(std::memory_order_relaxed);
        r.ltMs = lt > 0.0 ? now - lt : -1.0;
        const float fov = fovNowRad();
        r.fovDeg = std::isfinite(fov) ? scz::radToDeg(fov) : 0.0f;
        r.m = zoomNow();
        r.lens = g_lensOn.load(std::memory_order_relaxed) && r.m > scz::kLensMinM;
        float lagYaw = 0.0f, lagPitch = 0.0f;
        if (g_pitchDownDeg.load(std::memory_order_relaxed) != 0.0f) headAimSightLag(&lagYaw, &lagPitch);
        r.lagYawDeg = scz::radToDeg(lagYaw);
        r.lagPitchDeg = scz::radToDeg(lagPitch);
        HeadAimRecord ha{};
        headAimRecordSample(&ha);
        r.latchLast = ha.latchLast;
        r.latchCam = ha.latchCam;
        r.camLagKnown = ha.camLagKnown;
        r.camLagYawDeg = scz::radToDeg(ha.camLagYawRad);
        r.camLagPitchDeg = scz::radToDeg(ha.camLagPitchRad);
        r.yawRateDps = scz::radToDeg(ha.yawRateRad);
        r.pitchRateDps = scz::radToDeg(ha.pitchRateRad);
        float tans[4] = {};
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(g_tanMutex);
            known = g_tansKnown;
            std::memcpy(tans, g_tans, sizeof(tans));
        }
        const scz::Lens l = known ? lensNow(tans, w, h) : scz::Lens{};
        r.cx = l.valid ? l.cx : -1.0f;
        r.cy = l.valid ? l.cy : -1.0f;
        r.cropX = g_rec.cropX;
        r.cropY = g_rec.cropY;
        // The crop: a free staging copy, or a skip (all in flight).
        RecSlot* slot = nullptr;
        for (RecSlot& s : g_rec.ring)
            if (!s.busy) { slot = &s; break; }
        if (slot) {
            // A mirrored region holds the crop mirrored; the box is taken where the mirror puts it (the line's flip says so).
            const int x = flipU ? int(w) - g_rec.cropX - scz::kCropPx : g_rec.cropX;
            const int y = flipV ? int(h) - g_rec.cropY - scz::kCropPx : g_rec.cropY;
            D3D11_BOX box{};
            box.left = region[0] + UINT(x);
            box.top = region[1] + UINT(y);
            box.right = box.left + UINT(scz::kCropPx);
            box.bottom = box.top + UINT(scz::kCropPx);
            box.front = 0;
            box.back = 1;
            ctx->CopySubresourceRegion(slot->tex, 0, 0, 0, 0, src, 0, &box);
            slot->busy = true;
            slot->frame = g_rec.frames;
            r.crop = int(g_rec.frames);
        } else {
            ++g_rec.skipped;
        }
        char line[400];
        scz::formatRecordFrame(r, line, sizeof(line));
        say(line);
        g_rec.lines.emplace_back(line);
        ++g_rec.frames;
    } else {
        // Past the end: only the copies in flight are waited for (they age out by kRecGiveUp), then the files.
        ++g_rec.frames;
        bool busy = false;
        for (const RecSlot& s : g_rec.ring) busy = busy || s.busy;
        if (!busy) recFinish();
    }
    ctx->Release();
}

}  // namespace

void scopeZoomRecord(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV) {
    if (!g_recArm.load(std::memory_order_acquire) && !g_rec.running) return;
    if (!src || !region || int(eye) != g_lensEye.load(std::memory_order_relaxed)) return;
    if (region[2] <= region[0] || region[3] <= region[1]) return;
    const bool ran = guarded("scope zoom/recording", [&] {
        if (!g_rec.running) {
            if (!g_recArm.exchange(false, std::memory_order_acq_rel)) return;
            if (!g_keyOn.load(std::memory_order_acquire)) return;
            recBegin(src, region, flipU, flipV, int(eye));
            if (!g_rec.running) return;
        }
        D3D11_TEXTURE2D_DESC sd{};
        src->GetDesc(&sd);
        if (sd.Format != g_rec.format) { say("scope zoom: recording cut short: the eye's format changed"); recFinish(); return; }
        recFrame(src, region, flipU, flipV);
    });
    if (!ran) {
        g_rec.running = false;
        g_recRunning.store(false, std::memory_order_release);
        say("scope zoom: recording faulted and was dropped");
    }
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
    float corner[scz::kGlassCorners][4];
};
static_assert(sizeof(LensParams) == 208, "thirteen 16-byte rows");

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
    // The zoom the game is at (its FOV, narrowing as the weapon comes up): nothing to draw until it narrows.
    const float m = zoomNow();
    if (!(m > scz::kLensMinM)) return nullptr;
    g_lensM.store(m, std::memory_order_relaxed);
    scz::Lens l = lensNow(tans, w, h);
    if (!l.valid) { fail("the eye's tangents give no lens"); return nullptr; }
    if (!l.corners) return nullptr;   // no glass this moment (the weapon changed under an aim): nothing to draw, not a failure
    // A region stored mirrored: the centre and the corners mirror with it (the radii and the box's size do not).
    if (flipU) {
        l.cx = float(w) - l.cx;
        for (int i = 0; i < l.corners; ++i) l.px[i] = float(w) - l.px[i];
        const int x0 = int(w) - l.x1, x1 = int(w) - l.x0;
        l.x0 = x0; l.x1 = x1;
    }
    if (flipV) {
        l.cy = float(h) - l.cy;
        for (int i = 0; i < l.corners; ++i) l.py[i] = float(h) - l.py[i];
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
        p.params[0] = 1.0f / m;
        p.params[1] = scz::kEdgePx;
        p.params[2] = float(l.corners);
        for (int i = 0; i < l.corners; ++i) {
            p.corner[i][0] = l.px[i];
            p.corner[i][1] = l.py[i];
        }
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
    if (!g_lensOn.load(std::memory_order_acquire)) return nullptr;
    if (!src || !region || int(eye) != g_lensEye.load(std::memory_order_relaxed)) return nullptr;
    if (g_inert.load(std::memory_order_acquire)) return nullptr;
    ID3D11Texture2D* out = nullptr;
    if (!guardedBudget(g_lensBudget, [&] { out = lensInner(src, region, flipU, flipV); })) {
        fail("the lens faulted");
        return nullptr;
    }
    return out;
}

}  // namespace edvr
