#include "on_foot_hud.h"
#include "on_foot_hud_core.h"
#include "binding_shadow.h"
#include "draw_gate.h"
#include "journal_watch.h"
#include "../common/config.h"
#include "../common/guard.h"
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
std::atomic<uint64_t> g_frameSeq{1};

// The door's, one per layer (the HUD, then the menus): the copy its sheet reads, its hold and its line, and this frame's decision for
// the second eye. Under g_mutex with the summary.
struct LayerState {
    ID3D11Texture2D* copy = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Device* device = nullptr;  // identity only
    uint32_t w = 0, h = 0, fmt = 0;
    bool show = false;  // this frame's decision
    float aspect = 0.0f;
    ofh::Place place;
    ofh::Hold hold;          // the copy is kept up to 30 frames without a fresh panel of the layer's shape
    ofh::DecisionLatch latch;
    ofh::Panel heldPanel;    // the panel the copy was made from, for the showing line while it is held
    bool copyFailSaid = false;
    void releaseCopy() {
        if (srv) srv->Release();
        if (copy) copy->Release();
        srv = nullptr;
        copy = nullptr;
        device = nullptr;
        w = h = fmt = 0;
    }
};
LayerState g_layers[ofh::kLayers];
uint64_t g_doorSeq = 0;
ofh::Summary g_summary;
ofh::SeenSizes g_seen;     // the boundary's: panel sizes met, one line each
static_assert(kOnFootHudSheets == ofh::kLayers, "one sheet per layer");

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

// Which layers' panels (this frame's, or last frame's while this one's are not drawn yet) does this draw of the game's composite
// shaders read as a texture in slots 0-2? Bit k: layer k. The game's own crosshair, markers and captions in stereo come through the
// same shaders from their own atlases and must not stop a sheet. The references the calls take are all given back before the return.
uint32_t compositeReads(ID3D11DeviceContext* ctx) {
    if (!ctx) return 0;
    const void* now[ofh::kLayers] = {};
    const void* last[ofh::kLayers] = {};
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        int c[ofh::kLayers], l[ofh::kLayers];
        ofh::findLayers(g_cur.tally.panels, g_cur.tally.count, c);
        ofh::findLayers(g_last.tally.panels, g_last.tally.count, l);
        for (int k = 0; k < ofh::kLayers; ++k) {
            if (c[k] >= 0) now[k] = g_cur.tally.panels[c[k]].id;
            if (l[k] >= 0) last[k] = g_last.tally.panels[l[k]].id;
            any = any || now[k] || last[k];
        }
    }
    if (!any) return 0;  // no layer's panel known: nothing to compare with, no COM call
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
    const uint32_t reads = ofh::readsPanels(read, 3, now, last);
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
            const uint32_t reads = compositeReads(ctx);
            if (!reads) return;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cur.tally.composites |= reads;
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
        if (g_last.tally.composites) ++g_summary.gameComposites;
        if (!g_last.tally.count) ++g_summary.noPanels;
        // A size met for the first time while the sheet is needed, in stereo (the game's own composite is the cinema): one line.
        if (!g_last.tally.composites)
            for (int i = 0; i < g_last.tally.count; ++i)
                if (g_seen.note(g_last.tally.panels[i].w, g_last.tally.panels[i].h)) {
                    char line[200];
                    ofh::formatNewPanel(g_last.tally.panels[i].w, g_last.tally.panels[i].h, g_last.tally.panels[i].draws, line,
                                        sizeof(line));
                    say(line);
                }
    }
    if (now - windowStart >= ofh::kSummaryMs) {
        uint32_t unwritten = 0;
        for (const LayerState& L : g_layers) unwritten += L.latch.unwritten;
        if (g_summary.frames || g_summary.shown) {
            char line[1200];
            ofh::formatSummary(g_summary, g_last.tally.panels, g_last.tally.count, g_last.tally.dropped, unwritten, g_seen, line,
                               sizeof(line));
            say(line);
        }
        g_summary.clear();
        for (LayerState& L : g_layers) {
            L.latch.unwritten = 0;
            L.latch.newWindow();
        }
        g_seen.newWindow();
        windowStart = now;
    }
    g_frameSeq.fetch_add(1, std::memory_order_relaxed);
}

namespace {
// The layer's panel into the layer's own texture: made (or remade) to the panel's size, then copied. False when the panel's texture is
// gone or of another kind, or a texture of EDVR's could not be made.
bool copyInto(LayerState& L, ID3D11Device* device, ID3D11Texture2D* tex) {
    bool copied = false;
    guardedBudget(g_budget, [&] {
        if (!tex) return;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        if (d.SampleDesc.Count != 1 || d.ArraySize != 1 || d.MipLevels != 1) return;
        if (!L.copy || L.device != device || L.w != d.Width || L.h != d.Height || L.fmt != uint32_t(d.Format)) {
            L.releaseCopy();
            D3D11_TEXTURE2D_DESC c{};
            c.Width = d.Width;
            c.Height = d.Height;
            c.MipLevels = 1;
            c.ArraySize = 1;
            c.Format = d.Format;
            c.SampleDesc.Count = 1;
            c.Usage = D3D11_USAGE_DEFAULT;
            c.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&c, nullptr, &L.copy)) || !L.copy) {
                L.copy = nullptr;
                return;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC v{};
            v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = 1;
            if (FAILED(device->CreateShaderResourceView(L.copy, &v, &L.srv)) || !L.srv) {
                L.releaseCopy();
                return;
            }
            L.device = device;
            L.w = d.Width;
            L.h = d.Height;
            L.fmt = uint32_t(d.Format);
        }
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (!ctx) return;
        ctx->CopyResource(L.copy, tex);
        ctx->Release();
        copied = true;
    });
    return copied;
}

// Eye 0's decision for every layer, its copies and its lines. Under g_mutex.
void decideLayers(ID3D11Device* device, ofh::Mode mode, float width) {
    // Each layer's panel: this frame's when the Submit comes after its draws, else the one of the frame that just ended. A frame that
    // drew none leaves the layer's last copy to be held.
    Frame ranked;
    if (g_cur.tally.count) rankInto(g_cur, ranked);
    int cur[ofh::kLayers], last[ofh::kLayers];
    ofh::findLayers(ranked.tally.panels, ranked.tally.count, cur);
    ofh::findLayers(g_last.tally.panels, g_last.tally.count, last);
    const uint32_t composites = g_cur.tally.composites | g_last.tally.composites;
    ofh::DecisionIn base;
    base.onFoot = g_onFoot.load(std::memory_order_relaxed);
    base.nativeOnFoot = g_nativeOnFoot.load(std::memory_order_relaxed);
    base.mode = mode;
    for (int k = 0; k < ofh::kLayers; ++k) {
        LayerState& L = g_layers[k];
        const ofh::Layer layer = ofh::Layer(k);
        L.show = false;
        L.latch.quietUntilShown = layer != ofh::Layer::Hud;
        const Frame* use = cur[k] >= 0 ? &ranked : last[k] >= 0 ? &g_last : nullptr;
        const int index = cur[k] >= 0 ? cur[k] : last[k];
        ofh::DecisionIn in = base;
        in.gameComposites = ((composites >> k) & 1u) != 0;
        ofh::HoldStep step = ofh::HoldStep::Expired;
        const ofh::Reason reason = ofh::stepLayer(in, use != nullptr, L.hold, &step);
        uint64_t& shown = layer == ofh::Layer::Hud ? g_summary.shown : g_summary.menus[k].shown;
        uint64_t& held = layer == ofh::Layer::Hud ? g_summary.held : g_summary.menus[k].held;
        ofh::Panel panel;
        if (reason == ofh::Reason::Showing && step == ofh::HoldStep::Held) {
            // No fresh panel of the layer's shape this frame: the last copy stays.
            L.show = L.srv != nullptr;
            panel = L.heldPanel;
            if (L.show) {
                ++held;
                ++shown;
            }
        } else if (reason == ofh::Reason::Showing) {
            panel = use->tally.panels[index];
            if (copyInto(L, device, use->tex[index])) {
                L.show = true;
                L.aspect = ofh::aspectOf(L.w, L.h);
                L.heldPanel = panel;
                ++shown;
            } else {
                L.hold.reset();  // a copy that did not happen is not one to hold
                ++g_summary.copyFail;
                if (!L.copyFailSaid) {
                    L.copyFailSaid = true;
                    Log::get().note("on-foot hud: the %ux%u panel (%s) could not be copied (its texture is gone or of another shape, or "
                                    "a texture of EDVR's could not be made); it is not shown while that lasts, the summary counts it "
                                    "as copy-fail. Said once.",
                                    panel.w, panel.h, ofh::specOf(layer).name);
                }
            }
        }
        if (L.show) L.place = ofh::placeOf(layer, width);
        // A panel that could not be copied is not shown and not said as shown: the line stays what it was, the summary counts copy-fail.
        if ((reason != ofh::Reason::Showing || L.show) && L.latch.admit(reason, panel.w, panel.h, mode)) {
            char line[400];
            if (layer == ofh::Layer::Hud && reason == ofh::Reason::Showing)
                ofh::formatShowing(panel, mode, width, line, sizeof(line));
            else if (layer == ofh::Layer::Hud)
                ofh::formatStopped(reason, line, sizeof(line));
            else if (reason == ofh::Reason::Showing)
                ofh::formatMenuShowing(layer, panel, L.place, mode, line, sizeof(line));
            else
                ofh::formatMenuStopped(layer, reason, line, sizeof(line));
            say(line);
        }
    }
    for (auto*& t : ranked.tex) t = nullptr;  // g_cur's, not this copy's to release
}
}  // namespace

int onFootHudSheetsForEye(ID3D11Device* device, uint32_t eye, OnFootHudSheet out[kOnFootHudSheets]) {
    if (!device || !out || eye > 1) return 0;
    const uint64_t seq = g_frameSeq.load(std::memory_order_relaxed);
    const ofh::Mode mode = ofh::Mode(g_mode.load(std::memory_order_relaxed));
    const float width = g_width.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (eye == 0 || g_doorSeq != seq) {
        g_doorSeq = seq;
        decideLayers(device, mode, width);
    }
    // The HUD first, the menus over it: the door composites them in this order.
    int n = 0;
    for (int k = 0; k < ofh::kLayers; ++k) {
        const LayerState& L = g_layers[k];
        if (!L.show) continue;
        out[n++] = {L.srv, L.aspect, L.place.widthDeg, L.place.yawDeg, L.place.downM, L.place.curve, mode == ofh::Mode::Body ? 1 : 0, k};
    }
    return n;
}

}  // namespace edvr
