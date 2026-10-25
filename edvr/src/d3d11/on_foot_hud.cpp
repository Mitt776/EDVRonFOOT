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

// The frame's panels as the draws arrive (g_cur) and the frame that ended, ordered -- the HUD panel first, then by draws (g_last). Each holds one reference per panel it
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
std::atomic<int> g_mode{int(ofh::Mode::Body)};
std::atomic<float> g_width{ofh::kWidthDefault};
std::atomic<bool> g_nativeOnFoot{true};
std::atomic<int> g_onFoot{-1};
std::atomic<int> g_selected{0};
std::atomic<bool> g_selectedPressed{false};
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
ofh::Hold g_hold;          // the copy is kept up to 30 frames without a fresh HUD panel
ofh::SeenSizes g_seen;     // the boundary's: panel sizes met, one line each
int g_heldIndex = -1, g_heldCount = 0;  // the panel the copy was made from, for the showing line while it is held
ofh::Panel g_heldPanel;
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

// The frame that ended, ordered, its references carried along with their panels.
void rankInto(const Frame& from, Frame& to) {
    to.tally = from.tally;
    ofh::orderPanels(to.tally.panels, to.tally.count);
    for (int i = 0; i < to.tally.count; ++i) {
        to.tex[i] = nullptr;
        for (int j = 0; j < from.tally.count; ++j)
            if (from.tally.panels[j].id == to.tally.panels[i].id) to.tex[i] = from.tex[j];
    }
}

void say(const char* line) { Log::get().note("%s", line); }

// Does this draw of the game's composite shaders read the HUD panel (this frame's, or last frame's while this one's is not drawn yet)
// as a texture in slots 0-2? The game's own crosshair, markers and captions in stereo come through the same shaders from their own
// atlases and must not stop the sheet. The references the calls take are all given back before the return.
bool compositeReadsHud(ID3D11DeviceContext* ctx) {
    if (!ctx) return false;
    const void* ids[2] = {};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const int c = ofh::findHud(g_cur.tally.panels, g_cur.tally.count);
        const int l = ofh::findHud(g_last.tally.panels, g_last.tally.count);
        if (c >= 0) ids[0] = g_cur.tally.panels[c].id;
        if (l >= 0) ids[1] = g_last.tally.panels[l].id;
    }
    if (!ids[0] && !ids[1]) return false;  // no HUD panel known: nothing to compare with, no COM call
    ID3D11ShaderResourceView* srv[3] = {};
    const void* read[3] = {};
    ctx->PSGetShaderResources(0, 3, srv);
    for (int i = 0; i < 3; ++i) {
        if (!srv[i]) continue;
        ID3D11Resource* res = nullptr;
        srv[i]->GetResource(&res);
        if (res) {
            read[i] = res;  // identity only: the view still holds the resource
            res->Release();
        }
    }
    const bool reads = ofh::readsHudPanel(read, 3, ids[0], ids[1]);
    for (auto*& v : srv)
        if (v) {
            v->Release();
            v = nullptr;
        }
    return reads;
}
}  // namespace

void onFootHudNoteDraw(ID3D11DeviceContext* ctx, bool eye) {
    const uint64_t vs = bindingGet(BindSlot::Vs) ? bindingShaderHash(BindSlot::Vs) : 0;
    if (!vs) return;
    if (ofh::isGameCompositeVs(vs)) {
        guardedBudget(g_budget, [&] {
            if (!compositeReadsHud(ctx)) return;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cur.tally.gameComposites = true;
        });
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
    static ofh::Mode lastMode = ofh::Mode::Body;
    static float lastWidth = -1.0f;
    static Hotkey key;
    static std::string keyText = "\x01";
    static ofh::Selector selector;
    static uint64_t windowStart = 0;
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string modeText = cfg.getString("experimental.native_vr_on_foot_hud", "body");
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
        // A size met for the first time while the sheet is needed, in stereo (the game's own composite is the cinema): one line.
        if (!g_last.tally.gameComposites)
            for (int i = 0; i < g_last.tally.count; ++i)
                if (g_seen.note(g_last.tally.panels[i].w, g_last.tally.panels[i].h)) {
                    char line[200];
                    ofh::formatNewPanel(g_last.tally.panels[i].w, g_last.tally.panels[i].h, g_last.tally.panels[i].draws, line,
                                        sizeof(line));
                    say(line);
                }
    }
    if (key.bound() && key.pressed()) {
        const int count = g_last.tally.count;
        const int index = selector.press(count);
        g_selected.store(index, std::memory_order_relaxed);
        g_selectedPressed.store(true, std::memory_order_relaxed);
        char line[300];
        ofh::formatSelected(index, count, count ? &g_last.tally.panels[index] : nullptr, line, sizeof(line));
        say(line);
    }
    if (now - windowStart >= ofh::kSummaryMs) {
        if (g_summary.frames || g_summary.shown) {
            char line[700];
            ofh::formatSummary(g_summary, g_last.tally.panels, g_last.tally.count, g_last.tally.dropped, g_latch.unwritten, g_seen, line,
                               sizeof(line));
            say(line);
        }
        g_summary.clear();
        g_latch.unwritten = 0;
        g_latch.newWindow();
        g_seen.newWindow();
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
    ofh::Selector selector;
    selector.index = g_selected.load(std::memory_order_relaxed);
    selector.pressed = g_selectedPressed.load(std::memory_order_relaxed);
    // The panel to show: this frame's HUD panel when the Submit comes after its draws, else the one of the frame that just ended. A
    // frame that drew none leaves the last copy to be held.
    Frame ranked;
    const Frame* use = nullptr;
    int index = -1;
    if (g_cur.tally.count) {
        rankInto(g_cur, ranked);
        index = ofh::shownIndex(ranked.tally.panels, ranked.tally.count, selector);
        if (index >= 0) use = &ranked;
    }
    if (index < 0) {
        index = ofh::shownIndex(g_last.tally.panels, g_last.tally.count, selector);
        if (index >= 0) use = &g_last;
    }
    ofh::DecisionIn in;
    in.onFoot = g_onFoot.load(std::memory_order_relaxed);
    in.nativeOnFoot = g_nativeOnFoot.load(std::memory_order_relaxed);
    in.mode = mode;
    in.gameComposites = g_cur.tally.gameComposites || g_last.tally.gameComposites;
    in.panels = 1;  // the panels are the hold's to judge below
    ofh::Reason reason = ofh::decide(in);
    ofh::HoldStep step = ofh::HoldStep::Expired;
    if (reason == ofh::Reason::Showing) {
        step = g_hold.step(index >= 0);
        if (step == ofh::HoldStep::Expired) reason = ofh::Reason::NoPanels;
    } else {
        g_hold.reset();
    }
    int shownIndex = -1, shownCount = 0;
    ofh::Panel panel;
    if (reason == ofh::Reason::Showing && step == ofh::HoldStep::Held) {
        // No fresh HUD panel this frame: the last copy stays.
        g_doorShow = g_copySrv != nullptr;
        shownIndex = g_heldIndex;
        shownCount = g_heldCount;
        panel = g_heldPanel;
        if (g_doorShow) {
            ++g_summary.held;
            ++g_summary.shown;
        }
    } else if (reason == ofh::Reason::Showing) {
        shownIndex = index;
        shownCount = use->tally.count;
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
            g_heldIndex = shownIndex;
            g_heldCount = shownCount;
            g_heldPanel = panel;
            ++g_summary.shown;
        } else {
            g_hold.reset();  // a copy that did not happen is not one to hold
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
    if ((reason != ofh::Reason::Showing || g_doorShow) && g_latch.admit(reason, shownIndex, panel.w, panel.h, mode)) {
        char line[300];
        if (reason == ofh::Reason::Showing)
            ofh::formatShowing(shownIndex, shownCount, panel, mode, width, line, sizeof(line));
        else
            ofh::formatStopped(reason, line, sizeof(line));
        say(line);
    }
    for (auto*& t : ranked.tex) t = nullptr;  // g_cur's, not this copy's to release    if (!g_doorShow) return false;
    *out = {g_copySrv, g_doorAspect, width, mode == ofh::Mode::Body ? 1 : 0};
    return true;
}

}  // namespace edvr
