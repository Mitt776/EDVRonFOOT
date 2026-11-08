#include "aim_dot.h"
#include "aim_dot_core.h"
#include "on_foot_hud_core.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>

namespace edvr {
namespace detail {
std::atomic<bool> g_aimDotHideWants{false};
std::atomic<bool> g_aimDotHideErases{false};
}

namespace {
FaultBudget g_budget("aimDotHide", 8);

// The draw thread's state is one thread's at a time: the draw calls and the boundary's release take this try-flag (a second thread in
// the window skips its turn, as .42's probe did). The boundary's own state and the counters are atomics.
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
struct Busy {
    bool held = !g_busy.test_and_set(std::memory_order_acquire);
    ~Busy() {
        if (held) g_busy.clear(std::memory_order_release);
    }
};

std::atomic<int> g_mode{int(adh::Mode::Hide)};
std::atomic<bool> g_inert{false};
std::atomic<bool> g_holding{false};  // the draw thread holds a texture: the boundary gives it back with show, inert or off foot
std::atomic<uint64_t> g_seq{1};      // the frame's number, from the boundary
std::atomic<uint32_t> g_atlasW{0}, g_atlasH{0};
// The draw thread counts, the boundary takes them every 30 s.
std::atomic<uint64_t> g_cAtlasFrames{0}, g_cWeaponFrames{0}, g_cHidden{0}, g_cRewrites{0}, g_cFailures{0};
std::atomic<uint64_t> g_cPlain{0}, g_cOther{0}, g_cUnread{0}, g_cQuiet{0};
std::atomic<int> g_centreLines{0};  // the decision's lines in this summary window

void say(const char* line) { Log::get().note("%s", line); }

struct State {
    const void* atlasId = nullptr;     // identity only, as the HUD's census
    ID3D11Texture2D* atlas = nullptr;  // a reference, with hide
    uint32_t w = 0, h = 0, fmt = 0;
    adh::Rect centre;
    adh::Pattern pattern;  // the plain dot's for this size (.62): .42's at 2076x1787, scaled elsewhere; not native = a trial size
    uint32_t saidW = 0, saidH = 0;
    uint32_t patternSaidW = 0, patternSaidH = 0;  // the size last given a `pattern for` line
    ID3D11Texture2D* zero = nullptr;  // the centre's size of zeros, the atlas's format
    uint32_t zw = 0, zh = 0, zfmt = 0;
    // The centre's rows read back before the erase, the atlas's whole width (.60: the dot moves along them): a ring of staging
    // copies, each with the frame it was taken in (0: free), read without waiting, the oldest first. `plain`: the newest read found
    // the plain dot, at `plainAt` (the window's left edge in the atlas); until one is read it is not.
    static constexpr int kRing = 3;
    ID3D11Texture2D* ring[kRing] = {};
    uint64_t ringFrame[kRing] = {};
    uint32_t rw = 0, rh = 0, rfmt = 0;
    int ringNext = 0;
    bool plain = false, plainSaid = false;
    uint32_t plainAt = 0;
    adh::CentreRead said;  // the read last said in a decision line (.58: a line on any change of it)
    uint64_t nowAt = 0;    // when the last `centre now` line was written (GetTickCount64)
    // This frame's.
    uint64_t frame = 0;
    bool atlasDrawn = false, erased = false, rewrote = false, copied = false;
    uint32_t cue = 0;  // one bit per kWeaponCueCounts entry drawn into the atlas
    // Across frames: the weapon as last said (a line on each change).
    bool weapon = false, weaponSaid = false;

    void releaseAll() {
        for (ID3D11Texture2D** t : {&atlas, &zero, &ring[0], &ring[1], &ring[2]})
            if (*t) {
                (*t)->Release();
                *t = nullptr;
            }
        atlasId = nullptr;
        w = h = fmt = 0;
        zw = zh = zfmt = 0;
        rw = rh = rfmt = 0;
        for (uint64_t& f : ringFrame) f = 0;
        ringNext = 0;
        plain = false;
        plainAt = 0;
        g_holding.store(false, std::memory_order_relaxed);
    }
};
State g_s;

// One line, the module stands until the key changes; everything it holds is given back.
void standDown(const char* why) {
    char line[400];
    adh::formatInert(why, line, sizeof(line));
    say(line);
    g_inert.store(true, std::memory_order_relaxed);
    detail::g_aimDotHideWants.store(false, std::memory_order_relaxed);
    detail::g_aimDotHideErases.store(false, std::memory_order_relaxed);
    g_cFailures.fetch_add(1, std::memory_order_relaxed);
    g_s.releaseAll();
}

// A new frame met by a draw: the frame that ended (if it drew the atlas) says the weapon when it changed; this one starts clean.
void stepFrame() {
    const uint64_t seq = g_seq.load(std::memory_order_relaxed);
    if (seq == g_s.frame) return;
    if (g_s.atlasDrawn) {
        const bool weapon = g_s.cue != 0;
        if (weapon) g_cWeaponFrames.fetch_add(1, std::memory_order_relaxed);
        if (!g_s.weaponSaid || weapon != g_s.weapon) {
            char line[200];
            if (weapon)
                adh::formatWeaponOn(g_s.cue, line, sizeof(line));
            else
                adh::formatWeaponOff(line, sizeof(line));
            say(line);
            g_s.weapon = weapon;
            g_s.weaponSaid = true;
        }
    }
    g_s.frame = seq;
    g_s.atlasDrawn = g_s.erased = g_s.rewrote = g_s.copied = false;
    g_s.cue = 0;
}

// The atlas's size, its centre, a line when found or resized (with show too: the size is the census's, no call to the device).
void noteSize(uint32_t w, uint32_t h, uint32_t fmt) {
    g_s.w = w;
    g_s.h = h;
    g_s.fmt = fmt;
    g_s.centre = adh::centreOf(w, h);
    if (g_s.pattern.atlasW != w || g_s.pattern.atlasH != h) g_s.pattern = adh::patternOf(w, h);
    g_atlasW.store(w, std::memory_order_relaxed);
    g_atlasH.store(h, std::memory_order_relaxed);
    char line[300];
    if (!g_s.saidW) {
        adh::formatAtlasFound(w, h, fmt, g_s.centre, line, sizeof(line));
        say(line);
    } else if (g_s.saidW != w || g_s.saidH != h) {
        adh::formatAtlasSize(g_s.saidW, g_s.saidH, w, h, g_s.centre, line, sizeof(line));
        say(line);
    }
    g_s.saidW = w;
    g_s.saidH = h;
    if (g_s.patternSaidW != w || g_s.patternSaidH != h) {
        g_s.patternSaidW = w;
        g_s.patternSaidH = h;
        adh::formatPattern(g_s.pattern, line, sizeof(line));
        say(line);
    }
}

// The atlas behind the bound target, with a reference (hide): its format checked.
void adopt(ID3D11DeviceContext* ctx, const void* id) {
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    if (!res) return;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    if (!tex) return;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (!adh::atlasFormatOk(uint32_t(d.Format)) || d.SampleDesc.Count != 1) {
        tex->Release();
        char why[200];
        std::snprintf(why, sizeof(why), "the %ux%u atlas's format is %u (%u samples), not R8G8B8A8 with one", d.Width, d.Height,
                      unsigned(d.Format), d.SampleDesc.Count);
        standDown(why);
        return;
    }
    if (g_s.atlas) g_s.atlas->Release();
    g_s.atlas = tex;
    g_s.atlasId = id;
    g_holding.store(true, std::memory_order_relaxed);
    noteSize(d.Width, d.Height, uint32_t(d.Format));
}

// The centre's size of zeros in the atlas's format, remade on a new size.
ID3D11Texture2D* zeroTexture(uint32_t w, uint32_t h) {
    if (g_s.zero && g_s.zw == w && g_s.zh == h && g_s.zfmt == g_s.fmt) return g_s.zero;
    if (g_s.zero) g_s.zero->Release();
    g_s.zero = nullptr;
    ID3D11Device* device = nullptr;
    g_s.atlas->GetDevice(&device);
    if (!device) return nullptr;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT(g_s.fmt);
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<uint8_t> zeros(size_t(w) * h * 4, 0);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = zeros.data();
    init.SysMemPitch = w * 4;
    const HRESULT hr = device->CreateTexture2D(&d, &init, &g_s.zero);
    device->Release();
    if (FAILED(hr) || !g_s.zero) {
        g_s.zero = nullptr;
        return nullptr;
    }
    g_s.zw = w;
    g_s.zh = h;
    g_s.zfmt = g_s.fmt;
    return g_s.zero;
}
// The ring's staging textures: the centre's rows the atlas's whole width (.60), the atlas's format; remade (and emptied) on a new size.
bool ringReady() {
    const adh::Rect& r = g_s.centre;
    if (g_s.ring[0] && g_s.rw == g_s.w && g_s.rh == r.h() && g_s.rfmt == g_s.fmt) return true;
    for (int i = 0; i < State::kRing; ++i) {
        if (g_s.ring[i]) g_s.ring[i]->Release();
        g_s.ring[i] = nullptr;
        g_s.ringFrame[i] = 0;
    }
    g_s.ringNext = 0;
    ID3D11Device* device = nullptr;
    g_s.atlas->GetDevice(&device);
    if (!device) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = g_s.w;
    d.Height = r.h();
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT(g_s.fmt);
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bool ok = true;
    for (int i = 0; i < State::kRing && ok; ++i) ok = SUCCEEDED(device->CreateTexture2D(&d, nullptr, &g_s.ring[i])) && g_s.ring[i];
    device->Release();
    if (!ok) return false;
    g_s.rw = g_s.w;
    g_s.rh = r.h();
    g_s.rfmt = g_s.fmt;
    return true;
}

// The decision from one read: counted, a line on a change of the read's signature (.58; before it, of the decision only) --
// kCentreLinesPerWindow a window, the rest counted as quiet. An other line is followed by the window's pixels, inside the same
// quota (.56); on a trial size (.62) a plain line too, for the real picture of the plain dot there. Every kCentreNowMs a `centre now`
// line (and the pixels, as above) outside the quota (.58). Called while the copy is mapped: `rgba`, `pitch` are its bytes, the strip
// from the atlas's x 0 (.60: the plain dot is looked for along it; the pixels line is the window the read names, `at`).
void decide(const uint8_t* strip, uint32_t pitch) {
    const adh::Pattern& pat = g_s.pattern;
    const bool trial = !pat.native;
    const adh::CentreRead read = adh::readRow(strip, pitch, g_s.rw, g_s.centre.x0, pat);
    const uint8_t* rgba = strip ? strip + size_t(read.at) * 4 : nullptr;
    const bool plain = read.plain;
    const bool pixels = rgba && (!plain || trial);
    (plain ? g_cPlain : g_cOther).fetch_add(1, std::memory_order_relaxed);
    g_s.plain = plain;
    g_s.plainAt = read.at;
    const uint64_t now = GetTickCount64();
    if (!g_s.nowAt || now - g_s.nowAt >= adh::kCentreNowMs) {
        g_s.nowAt = now;
        char line[320];
        adh::formatCentreNow(read, line, sizeof(line), trial);
        say(line);
        if (pixels) {
            char text[adh::kPixelsLineCap];
            adh::formatCentrePixels(rgba, pitch, pat.w, pat.h, text, sizeof(text));
            say(text);
        }
    }
    if (g_s.plainSaid && adh::sameSignature(read, g_s.said)) return;
    g_s.said = read;
    g_s.plainSaid = true;
    if (g_centreLines.fetch_add(1, std::memory_order_relaxed) >= adh::kCentreLinesPerWindow) {
        g_cQuiet.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    char line[300];
    adh::formatCentre(read, line, sizeof(line), trial);
    say(line);
    if (!pixels) return;
    char text[adh::kPixelsLineCap];
    adh::formatCentrePixels(rgba, pitch, pat.w, pat.h, text, sizeof(text));
    say(text);
}

// The ring's ready copies, the oldest first, without waiting: the newest read decides. Then this frame's centre into the next slot
// (a slot still unread is overwritten: counted as unread). Any atlas size (.62); a size with no pattern (its `pattern for` line said
// none) is neither read nor copied, and the dot shows. False: the module stood down.
bool readAndCopy(ID3D11DeviceContext* ctx) {
    if (!g_s.pattern.valid) {
        g_s.plain = false;
        return true;
    }
    if (ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        standDown("the atlas is read on a deferred context; reading the centre back needs the immediate one");
        return false;
    }
    if (!ringReady()) {
        standDown("textures to read the centre back could not be made");
        return false;
    }
    for (;;) {
        int oldest = -1;
        for (int i = 0; i < State::kRing; ++i)
            if (g_s.ringFrame[i] && (oldest < 0 || g_s.ringFrame[i] < g_s.ringFrame[oldest])) oldest = i;
        if (oldest < 0) break;
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = ctx->Map(g_s.ring[oldest], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) break;
        if (FAILED(hr)) {
            standDown("the centre's copy could not be read");
            return false;
        }
        decide(static_cast<const uint8_t*>(m.pData), m.RowPitch);
        ctx->Unmap(g_s.ring[oldest], 0);
        g_s.ringFrame[oldest] = 0;
    }
    const int slot = g_s.ringNext;
    if (g_s.ringFrame[slot]) g_cUnread.fetch_add(1, std::memory_order_relaxed);
    const adh::Rect& r = g_s.centre;
    D3D11_BOX box{0, r.y0, 0, g_s.w, r.y1, 1};
    ctx->CopySubresourceRegion(g_s.ring[slot], 0, 0, 0, 0, g_s.atlas, 0, &box);
    g_s.ringFrame[slot] = g_s.frame ? g_s.frame : 1;
    g_s.ringNext = (slot + 1) % State::kRing;
    return true;
}
}  // namespace

void aimDotHideNoteGuiDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint32_t count, const void* id, uint32_t w, uint32_t h) {
    if (!ctx || !id || !adh::isAtlasShape(w, h)) return;  // every other panel: no lock, no call
    Busy busy;
    if (!busy.held) return;
    guardedBudget(g_budget, [&] {
        if (g_inert.load(std::memory_order_relaxed)) return;
        stepFrame();
        const bool hide = adh::Mode(g_mode.load(std::memory_order_relaxed)) == adh::Mode::Hide;
        if (hide) {
            if (id != g_s.atlasId || !g_s.atlas) adopt(ctx, id);
            if (!g_s.atlas || id != g_s.atlasId) return;
        } else if (id != g_s.atlasId || w != g_s.w || h != g_s.h) {
            g_s.atlasId = id;  // show: the census's identity and size, nothing held
            noteSize(w, h, 0);
        }
        if (!g_s.atlasDrawn) {
            g_s.atlasDrawn = true;
            g_cAtlasFrames.fetch_add(1, std::memory_order_relaxed);
        }
        const int cue = adh::weaponCue(vs == ofh::kGuiVector, count);
        if (cue >= 0) g_s.cue |= 1u << cue;
        if (g_s.erased) g_s.rewrote = true;  // drawn into after this frame's erase: erased again before the next read, still no weapon
    });
}

void aimDotHideBeforeEyeComposite(ID3D11DeviceContext* ctx, const void* const read[3]) {
    if (!ctx || !read) return;
    Busy busy;
    if (!busy.held) return;
    guardedBudget(g_budget, [&] {
        if (g_inert.load(std::memory_order_relaxed)) return;
        stepFrame();
        if (adh::Mode(g_mode.load(std::memory_order_relaxed)) != adh::Mode::Hide || !g_s.atlas) return;
        if (read[0] != g_s.atlasId && read[1] != g_s.atlasId && read[2] != g_s.atlasId) return;
        // The first read of the frame: the ready copies decide, then the centre as the game drew it is copied -- before the erase.
        if (!g_s.copied) {
            g_s.copied = true;
            if (!readAndCopy(ctx)) return;
        }
        // A trial size (.62: the scaled pattern is read and said, not yet trusted to erase), a weapon in this frame, not the plain dot
        // by the newest read, or erased and not drawn into since: kept.
        if (!g_s.pattern.native || g_s.cue || !g_s.plain || (g_s.erased && !g_s.rewrote)) return;
        const adh::Rect& r = g_s.centre;
        if (!zeroTexture(r.w(), r.h())) {
            standDown("a transparent texture for the centre could not be made");
            return;
        }
        if (g_s.plainAt + r.w() > g_s.w) return;  // a read from another size: never past the edge
        ctx->CopySubresourceRegion(g_s.atlas, 0, g_s.plainAt, r.y0, 0, g_s.zero, 0, nullptr);  // where the read found it (.60)
        (g_s.erased ? g_cRewrites : g_cHidden).fetch_add(1, std::memory_order_relaxed);
        g_s.erased = true;
        g_s.rewrote = false;
    });
}

void aimDotHideFrameBoundary(bool hudCensus, bool onFoot) {
    static uint32_t generation = ~0u;
    static bool saidKey = false;
    static adh::Mode lastMode = adh::Mode::Hide;
    static uint64_t windowStart = 0, frames = 0;
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string text = cfg.getString("experimental.native_vr_on_foot_aim_dot", "hide");
        bool valid = true;
        const adh::Mode mode = adh::parseMode(text.c_str(), &valid);
        if (!saidKey || mode != lastMode || !valid) {
            char line[400];
            adh::formatKey(mode, valid, text.c_str(), line, sizeof(line));
            say(line);
            saidKey = true;
        }
        if (mode != lastMode) g_inert.store(false, std::memory_order_relaxed);  // a new value: an inert module tries again
        lastMode = mode;
        g_mode.store(int(mode), std::memory_order_relaxed);
    }
    const adh::Mode mode = lastMode;
    const bool inert = g_inert.load(std::memory_order_relaxed);
    detail::g_aimDotHideWants.store(hudCensus && !inert, std::memory_order_relaxed);
    detail::g_aimDotHideErases.store(hudCensus && !inert && mode == adh::Mode::Hide, std::memory_order_relaxed);
    g_seq.fetch_add(1, std::memory_order_relaxed);

    // Show (or inert, or off foot): the atlas and EDVR's own texture are given back.
    if (g_holding.load(std::memory_order_relaxed) && (mode != adh::Mode::Hide || inert || !onFoot)) {
        Busy busy;
        if (busy.held) g_s.releaseAll();
    }

    const uint64_t now = GetTickCount64();
    if (!windowStart) windowStart = now;
    if (onFoot) ++frames;
    if (now - windowStart >= adh::kSummaryMs) {
        adh::Summary s;
        s.atlasFrames = g_cAtlasFrames.exchange(0, std::memory_order_relaxed);
        s.weaponFrames = g_cWeaponFrames.exchange(0, std::memory_order_relaxed);
        s.hidden = g_cHidden.exchange(0, std::memory_order_relaxed);
        s.rewrites = g_cRewrites.exchange(0, std::memory_order_relaxed);
        s.failures = g_cFailures.exchange(0, std::memory_order_relaxed);
        s.plain = g_cPlain.exchange(0, std::memory_order_relaxed);
        s.other = g_cOther.exchange(0, std::memory_order_relaxed);
        s.unread = g_cUnread.exchange(0, std::memory_order_relaxed);
        s.quietLines = g_cQuiet.exchange(0, std::memory_order_relaxed);
        g_centreLines.store(0, std::memory_order_relaxed);
        if (frames && (s.atlasFrames || s.failures)) {
            char line[600];
            adh::formatSummary(mode, g_atlasW.load(std::memory_order_relaxed), g_atlasH.load(std::memory_order_relaxed), s, line,
                               sizeof(line));
            say(line);
        }
        frames = 0;
        windowStart = now;
    }
}

}  // namespace edvr
