// The on-foot scope zoom: the frame thread's lines, the read-only FOV hook, and the lens (scope_zoom.h says what and why;
// scope_zoom_core.h is the pure half).
#include "scope_zoom.h"
#include "scope_zoom_core.h"
#include "on_foot_aim_eye_core.h"
#include "head_aim_core.h"        // kPitchOffsetDeg: where the sights sit below the eye's axis
#include "head_aim.h"             // headAimSightLag: the barrel's lag behind the head in a turn
#include "journal_watch.h"
#include "xinput_watch.h"
#include "vr_camera_census.h"     // nativeTemporalEyeGeometry: the eye's tangents
#include "shader_swap.h"
#include "gpu_timing.h"
#include "temporal_shader_bytecode.h"   // kScopeZoomLensBytecode, kScopeZoomMvBytecode (scope_zoom_shader.h, compiled at build time)
#include "dlaa.h"                       // dlssAreaEvaluate: the glass's own DLSS feature
#include "sharpen_pass.h"               // edvrSharpen: the glass's gathered image, sharpened in slot 2 like the eye
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
std::mutex g_tanMutex;                  // the dominant eye's tangents, as the frame thread last read them
float g_tans[4] = {};
bool g_tansKnown = false;
// Set by the door, read and cleared by the frame thread.
std::atomic<bool> g_inert{false};       // the lens failed: stand down until the key changes
std::atomic<uint32_t> g_lensFrames{0};
std::mutex g_inertMutex;
std::string g_inertReason;
// The sharp lens (on-foot-scope-sharpness). Set by the frame thread: gather the glass (the key is on and the path is sharp while a lens may
// be drawn). Set by the door: the lens was drawn on its last call. Set by the temporal pass: when DLSS last gathered the aiming eye 1:1 (tick
// ms). Set by the temporal pass on a refusal of the glass's feature, cleared by the frame thread on a key change (the reason beside it).
std::atomic<bool> g_sharpWanted{false};
std::atomic<bool> g_lensDrawing{false};
std::atomic<uint64_t> g_eyeGatheredMs{0};
std::atomic<bool> g_sharpFailed{false};
std::string g_sharpReason;              // under g_inertMutex
std::atomic<uint32_t> g_sharpFrames{0}; // lens frames drawn from the gathered image

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
    // The sharp lens: fix.temporal_aa is dlss; the path last said (under on), and the path of the aim in progress.
    bool dlss = false;
    bool pathSaid = false;
    scz::LensPath saidPath = scz::LensPath::None;
    std::string saidReason;
    scz::LensPath aimPath = scz::LensPath::None;
    bool sharpFailCounted = false;
    uint32_t sharpAtWindow = 0;
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


// The lens's geometry this moment: the sights at the head aim's offset plus its lag (head_aim.h headAimSightLag), the weapon's glass.
scz::Lens lensNow(const float tans[4], uint32_t w, uint32_t h) {
    float lagYaw = 0.0f, lagPitch = 0.0f;
    if (g_pitchDownDeg.load(std::memory_order_relaxed) != 0.0f) headAimSightLag(&lagYaw, &lagPitch);
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
// The zoom the lens draws: the game's, held back on the way up until the glass is at the eye (scz::delayedZoom). The render thread's only.
scz::ZoomDelay g_zoomDelay;
float lensZoomNow() { return scz::delayedZoom(g_zoomDelay, zoomNow(), qpcMs()); }

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
                // A new key: the lens may try again, and the once-lines may be said again; so may the glass's DLSS.
                g_inert.store(false, std::memory_order_release);
                g_sharpFailed.store(false, std::memory_order_release);
                f.sharpFailCounted = false;
                f.pathSaid = false;
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
        // The sharp lens needs NVIDIA's DLSS as the anti-aliasing (a restart key; read here for the reason's line only -- whether DLSS
        // actually gathers the eye is the temporal pass's report, g_eyeGatheredMs).
        {
            std::string aa = cfg.getString("fix.temporal_aa", "off");
            for (char& ch : aa) ch = (ch >= 'A' && ch <= 'Z') ? char(ch - 'A' + 'a') : ch;
            while (!aa.empty() && (aa.back() == ' ' || aa.back() == '\t' || aa.back() == '\r')) aa.pop_back();
            size_t b = 0;
            while (b < aa.size() && (aa[b] == ' ' || aa[b] == '\t')) ++b;
            f.dlss = aa.compare(b, std::string::npos, "dlss") == 0;
        }
        f.aimEyeKey = oae::parseKey(cfg.getString("experimental.native_vr_on_foot_aim_eye", "right").c_str());
        // The head aim puts the barrel, and so the sights, kPitchOffsetDeg below the head's tilt (head_aim_core.h); without it the barrel
        // follows the stick and the eye's axis is the best guess (flight 13.11: the sights 192 px, 9.8 deg, below the axis with it on).
        const bool headAim = ha::parseKey(cfg.getString("experimental.native_vr_on_foot_head_aim", "on").c_str()) == ha::Key::On;
        g_pitchDownDeg.store(headAim ? float(ha::kPitchOffsetDeg) : 0.0f, std::memory_order_relaxed);
    }

    g_keyOn.store(f.key != scz::Key::Off, std::memory_order_release);
    if (f.key == scz::Key::Off) {
        g_lensOn.store(false, std::memory_order_release);
        g_sharpWanted.store(false, std::memory_order_release);
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
        g_sharpWanted.store(false, std::memory_order_release);
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
            f.sharpAtWindow = g_sharpFrames.load(std::memory_order_relaxed);
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

    // LT: the aim's edges.
    xinputWatchTick();
    XinputBinding ltBinding;
    ltBinding.trigger = 1;   // the left trigger: HumanoidZoomButton on this pad
    ltBinding.valid = true;
    const bool lt = xinputHeld(ltBinding);
    f.lt = lt;
    // One line per aim, for a weapon with a lens only (an aim of the C-44 is no business of this module's).
    const scz::AimEnd end = scz::stepAim(f.aim, lt && f.zoom.inTable, g_lensFrames.load(std::memory_order_relaxed),
                                         g_lensM.load(std::memory_order_relaxed), now, g_sharpFrames.load(std::memory_order_relaxed));
    if (end.ended) {
        scz::formatAim(f.weapon, end, line, sizeof(line), scz::pathName(f.aimPath));
        say(line);
        f.aimPath = scz::LensPath::None;
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

    // The lens's path (on-foot-scope-sharpness, decisions 4 and 5): sharp where DLSS gathers the aiming eye and the glass's own feature has not
    // been refused, basic otherwise. A line on a change, said while aiming a weapon with a lens under on.
    {
        const uint64_t gathered = g_eyeGatheredMs.load(std::memory_order_relaxed);
        const bool eyeGathered = gathered && now >= gathered && now - gathered < 1000;
        const bool failed = g_sharpFailed.load(std::memory_order_acquire);
        std::string failReason;
        if (failed) {
            std::lock_guard<std::mutex> lock(g_inertMutex);
            failReason = g_sharpReason;
        }
        if (failed && !f.sharpFailCounted) {
            f.sharpFailCounted = true;
            ++f.failures;
        }
        const scz::PathChoice pc = scz::choosePath(f.key, f.dlss, eyeGathered, failed, failReason.c_str());
        g_sharpWanted.store(lens && pc.path == scz::LensPath::Sharp, std::memory_order_release);
        if (lens && f.lt) {
            if (f.aim.active && (f.aimPath == scz::LensPath::None || pc.path == scz::LensPath::Basic)) f.aimPath = pc.path;
            if (f.key == scz::Key::On && (!f.pathSaid || pc.path != f.saidPath || f.saidReason != pc.reason)) {
                f.pathSaid = true;
                f.saidPath = pc.path;
                f.saidReason = pc.reason;
                scz::formatPath(pc, line, sizeof(line));
                say(line);
            }
        }
    }
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
        const uint32_t sharpFrames = g_sharpFrames.load(std::memory_order_relaxed);
        s.sharpFrames = sharpFrames - f.sharpAtWindow;
        dlssAreaTakePrice(&s.sharpSamples, &s.sharpMsSum, &s.sharpMsMax);
        if (s.aims) {
            scz::formatSummary(s, line, sizeof(line));
            say(line);
        }
        f.windowMs = now;
        f.aimsAtWindow = f.aim.aims;
        f.lensAtWindow = lensFrames;
        f.sharpAtWindow = sharpFrames;
        f.failures = 0;
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
    float sharp[4];
};
static_assert(sizeof(LensParams) == 224, "fourteen 16-byte rows");

// The sharp lens's GPU state (the render thread's): the area's vectors pass, the glass's gathered image, and what the door may sample this
// frame. Made on the first gather, kept while the sizes hold.
struct SharpGpu {
    ID3D11Device* owner = nullptr;
    ID3D11ComputeShader* mvCs = nullptr;
    bool mvCsTried = false;
    ID3D11Buffer* mvCb = nullptr;
    void* mvSrcRes = nullptr;
    ID3D11ShaderResourceView* mvSrcSrv = nullptr;
    ID3D11Texture2D* mvTex = nullptr;            // the area's vectors, R16G16_FLOAT, area-sized
    ID3D11UnorderedAccessView* mvUav = nullptr;
    uint32_t mvW = 0, mvH = 0;
    ID3D11Texture2D* out = nullptr;              // the glass's gathered image, R8G8B8A8_UNORM, the area's output size
    uint32_t outW = 0, outH = 0;
    void* viewRes = nullptr;                     // the texture the lens's t1 views (the image, or its sharpened copy)
    ID3D11ShaderResourceView* viewSrv = nullptr;
    // The history: the last area gathered (its corner for the vectors' shift, its sizes for the feature).
    bool history = false;
    scz::SharpArea last;
    // This frame's gathered image, for the door: the area and the eye's size it was gathered in. Cleared by the door.
    bool ready = false;
    scz::SharpArea area;
    uint32_t eyeW = 0, eyeH = 0;
};
SharpGpu g_sharp;
FaultBudget g_sharpBudget("scopeZoomSharp", 4);

struct MvParams {
    int32_t area[4];
    float shift[4];
    int32_t limit[4];
};
static_assert(sizeof(MvParams) == 48, "three 16-byte rows");

template <class T>
void rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}
void releaseSharp() {
    rel(g_sharp.viewSrv);
    g_sharp.viewRes = nullptr;
    rel(g_sharp.out);
    g_sharp.outW = g_sharp.outH = 0;
    rel(g_sharp.mvUav);
    rel(g_sharp.mvTex);
    g_sharp.mvW = g_sharp.mvH = 0;
    rel(g_sharp.mvSrcSrv);
    g_sharp.mvSrcRes = nullptr;
    rel(g_sharp.mvCb);
    rel(g_sharp.mvCs);
    g_sharp.mvCsTried = false;
    g_sharp.history = false;
    g_sharp.ready = false;
    rel(g_sharp.owner);
}

// The glass's DLSS refused or faulted: the lens goes on as basic until the key changes (the frame thread says why, once); the eye is untouched.
void sharpFail(const char* reason) {
    {
        std::lock_guard<std::mutex> lock(g_inertMutex);
        g_sharpReason = reason ? reason : "the glass's DLSS failed";
    }
    g_sharpFailed.store(true, std::memory_order_release);
    g_sharp.history = false;
    g_sharp.ready = false;
    dlssAreaRelease();
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

ID3D11Texture2D* lensInner(ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV, float sharpness) {
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
    // This frame's gathered glass, taken once: valid only for an unmirrored eye of the size it was gathered in (the DLAA eye is the door's
    // eye pixel for pixel then: the temporal pass never treats a mirrored eye, and 1:1 keeps the size).
    const bool sharpReady = g_sharp.ready && !flipU && !flipV && g_sharp.eyeW == w && g_sharp.eyeH == h && g_sharp.out;
    const scz::SharpArea sharpArea = g_sharp.area;
    g_sharp.ready = false;

    float tans[4] = {};
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(g_tanMutex);
        known = g_tansKnown;
        std::memcpy(tans, g_tans, sizeof(tans));
    }
    if (!known) { fail("the eye's tangents are not known (the temporal channel is closed)"); return nullptr; }
    // The zoom the game is at (its FOV, narrowing as the weapon comes up), D late on the way up: nothing to draw until it narrows.
    const float m = lensZoomNow();
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

        // The gathered glass, sharpened with the eye's strength (sharpen_pass slot 2) so the glass and the world around it match; a refusal of
        // the sharpening leaves the image as gathered. Viewed at t1.
        bool useSharp = false;
        if (sharpReady) {
            ID3D11Texture2D* img = g_sharp.out;
            if (sharpness > 0.0f) {
                if (void* raw = edvrSharpen(g_sharp.out, 2, nullptr, sharpness)) img = static_cast<ID3D11Texture2D*>(raw);
            }
            if (g_sharp.viewRes != static_cast<void*>(img) || !g_sharp.viewSrv) {
                rel(g_sharp.viewSrv);
                g_sharp.viewRes = nullptr;
                D3D11_TEXTURE2D_DESC idesc{};
                img->GetDesc(&idesc);
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
                vd.Format = viewFormatOf(idesc.Format);
                vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                vd.Texture2D.MipLevels = 1;
                if (vd.Format != DXGI_FORMAT_UNKNOWN && SUCCEEDED(dev->CreateShaderResourceView(img, &vd, &g_sharp.viewSrv)) && g_sharp.viewSrv)
                    g_sharp.viewRes = img;
                else
                    rel(g_sharp.viewSrv);
            }
            useSharp = g_sharp.viewSrv != nullptr;
        }

        LensParams p{};
        if (useSharp) {
            p.params[3] = 1.0f;
            p.sharp[0] = float(sharpArea.x);
            p.sharp[1] = float(sharpArea.y);
            p.sharp[2] = 1.0f / float(sharpArea.w);
            p.sharp[3] = 1.0f / float(sharpArea.h);
        }
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
        ID3D11ShaderResourceView* savedSrv[2] = {};
        ID3D11UnorderedAccessView* savedUav = nullptr;
        ID3D11Buffer* savedCb = nullptr;
        ID3D11SamplerState* savedSampler = nullptr;
        ctx->CSGetShader(&savedCs, nullptr, nullptr);
        ctx->CSGetShaderResources(0, 2, savedSrv);
        ctx->CSGetUnorderedAccessViews(0, 1, &savedUav);
        ctx->CSGetConstantBuffers(0, 1, &savedCb);
        ctx->CSGetSamplers(0, 1, &savedSampler);
        ctx->CSSetShader(g_gpu.cs, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &g_gpu.cb);
        ctx->CSSetSamplers(0, 1, &g_gpu.sampler);
        ID3D11ShaderResourceView* srvs[2] = {g_gpu.srcSrv, useSharp ? g_sharp.viewSrv : nullptr};
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, &g_gpu.outUav, nullptr);
        ctx->Dispatch(UINT(l.x1 - l.x0 + 7) / 8, UINT(l.y1 - l.y0 + 7) / 8, 1);
        ID3D11ShaderResourceView* nullSrv[2] = {};
        ID3D11UnorderedAccessView* nullUav = nullptr;
        ctx->CSSetShaderResources(0, 2, nullSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
        ctx->CSSetShader(savedCs, nullptr, 0);
        ctx->CSSetShaderResources(0, 2, savedSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &savedUav, nullptr);
        ctx->CSSetConstantBuffers(0, 1, &savedCb);
        ctx->CSSetSamplers(0, 1, &savedSampler);
        rel(savedCs);
        rel(savedSrv[0]);
        rel(savedSrv[1]);
        rel(savedUav);
        rel(savedCb);
        rel(savedSampler);
        if (qs >= 0) g_qring[qs].timer.end(ctx);
        g_lensFrames.fetch_add(1, std::memory_order_relaxed);
        if (useSharp) g_sharpFrames.fetch_add(1, std::memory_order_relaxed);
        result = g_gpu.out;
    } while (false);
    ctx->Release();
    dev->Release();
    return result;
}

// The glass's gather (the temporal pass, right after the aiming eye's DLAA): the area from this moment's lens (sharpAreaFor), the eye's vectors
// shifted by the area's move into the area's own texture, then the glass's DLSS feature into the gathered image. A refusal goes to sharpFail.
void gatherInner(ID3D11DeviceContext* ctx, ID3D11Texture2D* colour, ID3D11Texture2D* depth, ID3D11Texture2D* motion, uint32_t w, uint32_t h,
                 float jx, float jy, bool reset, float frameMs) {
    float tans[4] = {};
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(g_tanMutex);
        known = g_tansKnown;
        std::memcpy(tans, g_tans, sizeof(tans));
    }
    if (!known) return;   // the door's lens says so, once
    const scz::Lens l = lensNow(tans, w, h);
    const float mFull = g_tableM.load(std::memory_order_relaxed);
    const scz::SharpArea area = scz::sharpAreaFor(l, w, h, mFull);
    if (!area.valid) { g_sharp.history = false; return; }   // no glass this moment: nothing to gather, not a failure

    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) { sharpFail("the eye's context has no device"); return; }
    if (g_sharp.owner != dev) {
        releaseSharp();
        dlssAreaRelease();
        g_sharp.owner = dev;
        dev->AddRef();
    }
    do {
        if (!g_sharp.mvCs && !g_sharp.mvCsTried) {
            g_sharp.mvCsTried = true;
            g_sharp.mvCs = shaderSwapCreateCs(ctx, kScopeZoomMvBytecode, sizeof(kScopeZoomMvBytecode), "scope_zoom_mv_cs", "scope zoom area vectors");
        }
        if (!g_sharp.mvCs) { sharpFail("the glass's vectors shader could not be created"); break; }
        if (!g_sharp.mvCb) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(MvParams);
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_sharp.mvCb)) || !g_sharp.mvCb) { sharpFail("the glass's vectors buffer could not be created"); break; }
        }
        if (g_sharp.mvSrcRes != static_cast<void*>(motion) || !g_sharp.mvSrcSrv) {
            rel(g_sharp.mvSrcSrv);
            g_sharp.mvSrcRes = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
            vd.Format = DXGI_FORMAT_R16G16_FLOAT;
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = 1;
            if (FAILED(dev->CreateShaderResourceView(motion, &vd, &g_sharp.mvSrcSrv)) || !g_sharp.mvSrcSrv) {
                sharpFail("the eye's motion vectors refuse a shader view");
                break;
            }
            g_sharp.mvSrcRes = motion;
        }
        if (!g_sharp.mvTex || g_sharp.mvW != area.w || g_sharp.mvH != area.h) {
            rel(g_sharp.mvUav);
            rel(g_sharp.mvTex);
            D3D11_TEXTURE2D_DESC td{};
            td.Width = area.w;
            td.Height = area.h;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R16G16_FLOAT;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_sharp.mvTex)) || !g_sharp.mvTex ||
                FAILED(dev->CreateUnorderedAccessView(g_sharp.mvTex, nullptr, &g_sharp.mvUav)) || !g_sharp.mvUav) {
                rel(g_sharp.mvUav);
                rel(g_sharp.mvTex);
                sharpFail("the glass's vectors texture could not be created");
                break;
            }
            g_sharp.mvW = area.w;
            g_sharp.mvH = area.h;
        }
        if (!g_sharp.out || g_sharp.outW != area.outW || g_sharp.outH != area.outH) {
            rel(g_sharp.viewSrv);
            g_sharp.viewRes = nullptr;
            rel(g_sharp.out);
            D3D11_TEXTURE2D_DESC td{};
            td.Width = area.outW;
            td.Height = area.outH;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // the eyes' DLAA output's format (dlaa.h)
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_sharp.out)) || !g_sharp.out) {
                rel(g_sharp.out);
                sharpFail("the glass's gathered image could not be created");
                break;
            }
            g_sharp.outW = area.outW;
            g_sharp.outH = area.outH;
        }
        // The history holds while the feature does; a move of the area is the vectors' shift.
        const bool fresh = reset || !g_sharp.history || !scz::sameFeature(area, g_sharp.last);
        float dx = 0.0f, dy = 0.0f;
        if (!fresh) scz::sharpMvShift(area, g_sharp.last, &dx, &dy);
        D3D11_TEXTURE2D_DESC md{};
        motion->GetDesc(&md);
        MvParams mp{};
        mp.area[0] = area.x; mp.area[1] = area.y; mp.area[2] = int32_t(area.w); mp.area[3] = int32_t(area.h);
        mp.shift[0] = dx; mp.shift[1] = dy;
        mp.limit[0] = int32_t(md.Width); mp.limit[1] = int32_t(md.Height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(g_sharp.mvCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) { sharpFail("the glass's vectors buffer would not map"); break; }
        std::memcpy(mapped.pData, &mp, sizeof(mp));
        ctx->Unmap(g_sharp.mvCb, 0);
        {
            ID3D11ComputeShader* savedCs = nullptr;
            ID3D11ShaderResourceView* savedSrv = nullptr;
            ID3D11UnorderedAccessView* savedUav = nullptr;
            ID3D11Buffer* savedCb = nullptr;
            ctx->CSGetShader(&savedCs, nullptr, nullptr);
            ctx->CSGetShaderResources(0, 1, &savedSrv);
            ctx->CSGetUnorderedAccessViews(0, 1, &savedUav);
            ctx->CSGetConstantBuffers(0, 1, &savedCb);
            ctx->CSSetShader(g_sharp.mvCs, nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, &g_sharp.mvCb);
            ctx->CSSetShaderResources(0, 1, &g_sharp.mvSrcSrv);
            ctx->CSSetUnorderedAccessViews(0, 1, &g_sharp.mvUav, nullptr);
            ctx->Dispatch((area.w + 7) / 8, (area.h + 7) / 8, 1);
            ID3D11ShaderResourceView* nullSrv = nullptr;
            ID3D11UnorderedAccessView* nullUav = nullptr;
            ctx->CSSetShaderResources(0, 1, &nullSrv);
            ctx->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
            ctx->CSSetShader(savedCs, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &savedSrv);
            ctx->CSSetUnorderedAccessViews(0, 1, &savedUav, nullptr);
            ctx->CSSetConstantBuffers(0, 1, &savedCb);
            rel(savedCs);
            rel(savedSrv);
            rel(savedUav);
            rel(savedCb);
        }
        bool created = false;
        const char* why = nullptr;
        if (!dlssAreaEvaluate(ctx, colour, depth, g_sharp.mvTex, g_sharp.out, uint32_t(area.x), uint32_t(area.y), area.w, area.h, area.outW,
                              area.outH, jx, jy, fresh, frameMs, &created, &why)) {
            sharpFail(why);
            break;
        }
        if (created) {
            char line[240];
            scz::formatSharpFeature(area, mFull, line, sizeof(line));
            say(line);
        }
        g_sharp.last = area;
        g_sharp.history = true;
        g_sharp.area = area;
        g_sharp.eyeW = w;
        g_sharp.eyeH = h;
        g_sharp.ready = true;
    } while (false);
    dev->Release();
}

}  // namespace

ID3D11Texture2D* scopeZoomLens(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV, float sharpness) {
    if (!g_lensOn.load(std::memory_order_acquire)) {
        g_lensDrawing.store(false, std::memory_order_relaxed);
        return nullptr;
    }
    if (!src || !region || int(eye) != g_lensEye.load(std::memory_order_relaxed)) return nullptr;
    if (g_inert.load(std::memory_order_acquire)) {
        g_lensDrawing.store(false, std::memory_order_relaxed);
        return nullptr;
    }
    ID3D11Texture2D* out = nullptr;
    if (!guardedBudget(g_lensBudget, [&] { out = lensInner(src, region, flipU, flipV, sharpness); })) {
        fail("the lens faulted");
        out = nullptr;
    }
    g_lensDrawing.store(out != nullptr, std::memory_order_relaxed);
    return out;
}

void scopeZoomGatherGlass(ID3D11DeviceContext* ctx, int eye, ID3D11Texture2D* colour, ID3D11Texture2D* depth, ID3D11Texture2D* motion,
                          uint32_t w, uint32_t h, uint32_t outW, uint32_t outH, float jx, float jy, bool reset, float frameMs) {
    if (!g_keyOn.load(std::memory_order_acquire)) return;
    if (eye != g_lensEye.load(std::memory_order_relaxed) || !w || !h || outW != w || outH != h) return;
    // DLSS gathered the aiming eye 1:1: the path's choice may be sharp.
    g_eyeGatheredMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_sharp.ready = false;
    if (!g_sharpWanted.load(std::memory_order_acquire) || !g_lensDrawing.load(std::memory_order_relaxed) ||
        g_sharpFailed.load(std::memory_order_acquire) || g_inert.load(std::memory_order_acquire) || !ctx || !colour || !depth || !motion) {
        g_sharp.history = false;   // the next gather starts the glass's history again
        return;
    }
    if (!guardedBudget(g_sharpBudget, [&] { gatherInner(ctx, colour, depth, motion, w, h, jx, jy, reset, frameMs); })) sharpFail("the glass's DLSS faulted");
}

bool scopeZoomWantsLongJitter() {
    return g_sharpWanted.load(std::memory_order_relaxed) && g_lensDrawing.load(std::memory_order_relaxed);
}

}  // namespace edvr
