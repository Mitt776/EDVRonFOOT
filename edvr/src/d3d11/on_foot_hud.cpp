#include "on_foot_hud.h"
#include "on_foot_hud_core.h"
#include "binding_shadow.h"
#include "draw_gate.h"
#include "journal_watch.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/hotkey.h"
#include "../common/log.h"
#include <windows.h>
#include <d3d11.h>
#include <mutex>
#include <string>

namespace edvr {
namespace detail {
std::atomic<bool> g_onFootHudWants{false};
}

namespace {
FaultBudget g_budget("onFootHud", 8);

// The frame's panels as the draws arrive (g_cur) and the frame that ended, ranked (g_last). Each holds one reference per panel it
// names, taken at the panel's first interface draw of the frame and given back at the boundary after next: no panel is held longer
// than that, and the game is free to re-create them. One lock: the draw hook, the boundary and the door can be different threads.
struct Frame {
    ofh::FrameTally tally;
    ID3D11Texture2D* tex[ofh::kMaxPanels] = {};
    void release() {
        for (auto*& t : tex)
            if (t) {
                t->Release();
                t = nullptr;
            }
        tally.clear();
    }
};
std::mutex g_mutex;
Frame g_cur, g_last;

// The draw thread's answer for the bound target, kept for its binding generation (the rtv0Eye pattern): is it a panel, and which.
uint32_t g_rtvGen = 0;
bool g_rtvPanel = false;
const void* g_rtvId = nullptr;
uint32_t g_rtvW = 0, g_rtvH = 0, g_rtvFmt = 0;

// The keys and the journal, from the frame thread to the door.
std::atomic<int> g_mode{int(ofh::Mode::Visor)};
std::atomic<float> g_width{ofh::kWidthDefault};
std::atomic<bool> g_nativeOnFoot{true};
std::atomic<int> g_onFoot{-1};
std::atomic<int> g_selected{0};
std::atomic<uint64_t> g_frameSeq{1};

// The door's: the copy the sheet reads, and this frame's decision for the second eye. Under g_mutex with the latch and the summary.
ID3D11Texture2D* g_copy = nullptr;
ID3D11ShaderResourceView* g_copySrv = nullptr;
ID3D11Device* g_copyDevice = nullptr;  // identity only
uint32_t g_copyW = 0, g_copyH = 0, g_copyFmt = 0;
uint64_t g_doorSeq = 0;
bool g_doorShow = false;
float g_doorAspect = 0.0f;
ofh::DecisionLatch g_latch;
ofh::Summary g_summary;
bool g_copyFailSaid = false;

void releaseCopy() {
    if (g_copySrv) g_copySrv->Release();
    if (g_copy) g_copy->Release();
    g_copySrv = nullptr;
    g_copy = nullptr;
    g_copyDevice = nullptr;
    g_copyW = g_copyH = g_copyFmt = 0;
}

// The bound target: a 2D, single-sample texture of the panels' format. One resolve and one GetDesc per binding generation.
void resolveTarget() {
    g_rtvPanel = false;
    void* view = bindingGet(BindSlot::Rtv0);
    if (!view) return;
    ResourceInfo info;
    if (!bindingResolve(view, &info) || !info.isTexture2D) return;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    static_cast<ID3D11RenderTargetView*>(view)->GetDesc(&rd);
    const uint32_t samples = rd.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D ? 1u : 0u;
    if (!ofh::panelShapeOk(info.fmt, samples, false)) return;
    g_rtvPanel = true;
    g_rtvId = info.resource;
    g_rtvW = info.a;
    g_rtvH = info.b;
    g_rtvFmt = info.fmt;
}

// The panel's texture, with a reference, from the bound view: taken once a frame per panel.
ID3D11Texture2D* boundTexture() {
    auto* view = static_cast<ID3D11View*>(bindingGet(BindSlot::Rtv0));
    if (!view) return nullptr;
    ID3D11Resource* res = nullptr;
    view->GetResource(&res);
    if (!res) return nullptr;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    return tex;
}

// The frame that ended, ranked, its references carried along with their panels.
void rankInto(const Frame& from, Frame& to) {
    to.tally = from.tally;
    ofh::rankPanels(to.tally.panels, to.tally.count);
    for (int i = 0; i < to.tally.count; ++i) {
        to.tex[i] = nullptr;
        for (int j = 0; j < from.tally.count; ++j)
            if (from.tally.panels[j].id == to.tally.panels[i].id) to.tex[i] = from.tex[j];
    }
}

void say(const char* line) { Log::get().note("%s", line); }
}  // namespace

void onFootHudNoteDraw(ID3D11DeviceContext*, bool eye) {
    const uint64_t vs = bindingGet(BindSlot::Vs) ? bindingShaderHash(BindSlot::Vs) : 0;
    if (!vs) return;
    if (ofh::isGameCompositeVs(vs)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cur.tally.gameComposites = true;
        return;
    }
    if (eye || !ofh::isGuiVs(vs)) return;
    guardedBudget(g_budget, [&] {
        const uint32_t gen = bindingGeneration(BindSlot::Rtv0);
        if (gen != g_rtvGen) {
            g_rtvGen = gen;
            resolveTarget();
        }
        if (!g_rtvPanel) return;
        int slot = -1;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            slot = g_cur.tally.note(g_rtvId, g_rtvW, g_rtvH, g_rtvFmt);
            if (slot < 0 || g_cur.tex[slot]) return;
        }
        // The COM calls outside the lock: a fault in them must not leave it held.
        ID3D11Texture2D* tex = boundTexture();
        std::lock_guard<std::mutex> lock(g_mutex);
        if (slot < g_cur.tally.count && g_cur.tally.panels[slot].id == g_rtvId && !g_cur.tex[slot])
            g_cur.tex[slot] = tex;
        else if (tex)
            tex->Release();
    });
}

void onFootHudFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool saidKey = false;
    static ofh::Mode lastMode = ofh::Mode::Visor;
    static float lastWidth = -1.0f;
    static Hotkey key;
    static std::string keyText = "\x01";
    static ofh::Selector selector;
    static uint64_t windowStart = 0;
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string modeText = cfg.getString("experimental.native_vr_on_foot_hud", "visor");
        bool modeValid = true, widthValid = true;
        const ofh::Mode mode = ofh::parseMode(modeText.c_str(), &modeValid);
        const float rawWidth = cfg.getFloat("experimental.native_vr_on_foot_hud_size", ofh::kWidthDefault);
        const float width = ofh::parseWidth(rawWidth, &widthValid);
        if (!saidKey || mode != lastMode || width != lastWidth) {
            char line[600];
            ofh::formatKey(mode, modeValid, modeText.c_str(), width, widthValid, rawWidth, line, sizeof(line));
            say(line);
            saidKey = true;
        }
        lastMode = mode;
        lastWidth = width;
        g_mode.store(int(mode), std::memory_order_relaxed);
        g_width.store(width, std::memory_order_relaxed);
        g_nativeOnFoot.store(cfg.getBool("experimental.native_vr_on_foot", true), std::memory_order_relaxed);
        const std::string text = cfg.getString("hotkey.on_foot_hud_panel", "");
        if (text != keyText) {
            keyText = text;
            key.setBinding(text.c_str());
            if (key.bound()) Log::get().note("on-foot hud: the panel key is %s: each press shows the next panel the frame drew", text.c_str());
        }
    }
    const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
    g_onFoot.store(onFoot, std::memory_order_relaxed);
    const bool wants = ofh::Mode(g_mode.load(std::memory_order_relaxed)) != ofh::Mode::Off &&
                       g_nativeOnFoot.load(std::memory_order_relaxed) && onFoot == 1;
    if (wants && !detail::g_onFootHudWants.load(std::memory_order_relaxed)) drawGateArm();
    detail::g_onFootHudWants.store(wants, std::memory_order_relaxed);

    const uint64_t now = GetTickCount64();
    if (!windowStart) windowStart = now;
    std::lock_guard<std::mutex> lock(g_mutex);
    // The frame that ended becomes the door's fallback (a Submit after the Present reads it); the one before gives its references back.
    Frame ranked;
    rankInto(g_cur, ranked);
    g_last.release();
    g_last = ranked;
    for (auto*& t : g_cur.tex) t = nullptr;  // carried into g_last
    g_cur.tally.clear();
    if (wants) {
        ++g_summary.frames;
        if (g_last.tally.gameComposites) ++g_summary.gameComposites;
        if (!g_last.tally.count) ++g_summary.noPanels;
    }
    if (key.bound() && key.pressed()) {
        const int count = g_last.tally.count;
        const int index = selector.press(count);
        g_selected.store(index, std::memory_order_relaxed);
        char line[300];
        ofh::formatSelected(index, count, count ? &g_last.tally.panels[index] : nullptr, line, sizeof(line));
        say(line);
    }
    if (now - windowStart >= ofh::kSummaryMs) {
        if (g_summary.frames || g_summary.shown) {
            char line[700];
            ofh::formatSummary(g_summary, g_last.tally.panels, g_last.tally.count, g_last.tally.dropped, g_latch.unwritten, line,
                               sizeof(line));
            say(line);
        }
        g_summary.clear();
        g_latch.unwritten = 0;
        g_latch.newWindow();
        windowStart = now;
    }
    g_frameSeq.fetch_add(1, std::memory_order_relaxed);
}

bool onFootHudSheetForEye(ID3D11Device* device, uint32_t eye, OnFootHudSheet* out) {
    if (!device || !out || eye > 1) return false;
    const uint64_t seq = g_frameSeq.load(std::memory_order_relaxed);
    const ofh::Mode mode = ofh::Mode(g_mode.load(std::memory_order_relaxed));
    const float width = g_width.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (eye == 1 && g_doorSeq == seq) {
        if (!g_doorShow) return false;
        *out = {g_copySrv, g_doorAspect, width, mode == ofh::Mode::Body ? 1 : 0};
        return true;
    }
    g_doorSeq = seq;
    g_doorShow = false;
    // This frame's panels when the Submit comes before the Present (the draws are all in), else the frame that just ended.
    Frame ranked;
    const Frame* use = &g_last;
    if (g_cur.tally.count) {
        rankInto(g_cur, ranked);
        use = &ranked;
    }
    ofh::DecisionIn in;
    in.onFoot = g_onFoot.load(std::memory_order_relaxed);
    in.nativeOnFoot = g_nativeOnFoot.load(std::memory_order_relaxed);
    in.mode = mode;
    in.gameComposites = g_cur.tally.gameComposites || g_last.tally.gameComposites;
    in.panels = use->tally.count;
    ofh::Reason reason = ofh::decide(in);
    int index = -1;
    ofh::Panel panel;
    if (reason == ofh::Reason::Showing) {
        const int selected = g_selected.load(std::memory_order_relaxed);
        index = selected < use->tally.count ? selected : 0;
        panel = use->tally.panels[index];
        ID3D11Texture2D* tex = use->tex[index];
        bool copied = false;
        guardedBudget(g_budget, [&] {
            if (!tex) return;
            D3D11_TEXTURE2D_DESC d{};
            tex->GetDesc(&d);
            if (d.SampleDesc.Count != 1 || d.ArraySize != 1 || d.MipLevels != 1) return;
            if (!g_copy || g_copyDevice != device || g_copyW != d.Width || g_copyH != d.Height || g_copyFmt != uint32_t(d.Format)) {
                releaseCopy();
                D3D11_TEXTURE2D_DESC c{};
                c.Width = d.Width;
                c.Height = d.Height;
                c.MipLevels = 1;
                c.ArraySize = 1;
                c.Format = d.Format;
                c.SampleDesc.Count = 1;
                c.Usage = D3D11_USAGE_DEFAULT;
                c.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (FAILED(device->CreateTexture2D(&c, nullptr, &g_copy)) || !g_copy) {
                    g_copy = nullptr;
                    return;
                }
                D3D11_SHADER_RESOURCE_VIEW_DESC v{};
                v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                v.Texture2D.MipLevels = 1;
                if (FAILED(device->CreateShaderResourceView(g_copy, &v, &g_copySrv)) || !g_copySrv) {
                    releaseCopy();
                    return;
                }
                g_copyDevice = device;
                g_copyW = d.Width;
                g_copyH = d.Height;
                g_copyFmt = uint32_t(d.Format);
            }
            ID3D11DeviceContext* ctx = nullptr;
            device->GetImmediateContext(&ctx);
            if (!ctx) return;
            ctx->CopyResource(g_copy, tex);
            ctx->Release();
            copied = true;
        });
        if (copied) {
            g_doorShow = true;
            g_doorAspect = ofh::aspectOf(g_copyW, g_copyH);
            ++g_summary.shown;
        } else {
            ++g_summary.copyFail;
            if (!g_copyFailSaid) {
                g_copyFailSaid = true;
                Log::get().note("on-foot hud: the %ux%u panel could not be copied (its texture is gone or of another shape, or a "
                                "texture of EDVR's could not be made); nothing is shown while that lasts, the summary counts it as "
                                "copy-fail. Said once.",
                                panel.w, panel.h);
            }
        }
    }
    // A panel that could not be copied is not shown and not said as shown: the line stays what it was, the summary counts copy-fail.
    if ((reason != ofh::Reason::Showing || g_doorShow) && g_latch.admit(reason, index, panel.w, panel.h, mode)) {
        char line[300];
        if (reason == ofh::Reason::Showing)
            ofh::formatShowing(index, use->tally.count, panel, mode, width, line, sizeof(line));
        else
            ofh::formatStopped(reason, line, sizeof(line));
        say(line);
    }
    if (use == &ranked)
        for (auto*& t : ranked.tex) t = nullptr;  // g_cur's, not this copy's to release
    if (!g_doorShow) return false;
    *out = {g_copySrv, g_doorAspect, width, mode == ofh::Mode::Body ? 1 : 0};
    return true;
}

}  // namespace edvr
