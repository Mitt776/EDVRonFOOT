#include "on_foot_hud.h"
#include "on_foot_hud_core.h"
#include "summary_probe_core.h"  // .49, TEMPORARY
#include "aim_dot.h"
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
std::atomic<bool> g_onFootMenuHeld{false};
}

namespace {
FaultBudget g_budget("onFootHud", 8);

// The frame's panels as the draws arrive (g_cur) and the frame that ended, ordered -- the HUD panel first, then by draws (g_last). Each holds one reference per panel it
// names, taken at the panel's first interface draw of the frame and given back at the boundary after next: no panel is held longer
// than that, and the game is free to re-create them. One lock: the draw hook, the boundary and the door can be different threads.
// withheldTex: the texture the frame's first withheld head-locked draw read (.28), with a reference, given back with the panels' --
// the trader layer copies from it, so a draw is never withheld without something to show.
struct Frame {
    ofh::FrameTally tally;
    ID3D11Texture2D* tex[ofh::kMaxPanels] = {};
    ID3D11Texture2D* withheldTex = nullptr;
    void release() {
        for (auto*& t : tex)
            if (t) {
                t->Release();
                t = nullptr;
            }
        if (withheldTex) {
            withheldTex->Release();
            withheldTex = nullptr;
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
std::atomic<bool> g_nativeOnFoot{true};
std::atomic<int> g_onFoot{-1};
std::atomic<uint64_t> g_frameSeq{1};
// .48: a HUD panel was drawn no more than kHoldFrames frames ago (the boundary's, from the frame that ended; "no" on stepping onto
// the feet). The draw hook reads it to tell the Summary on the wide mesh from the loading dialog on the same mesh.
std::atomic<bool> g_hudRecent{false};

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
uint32_t g_hudWidth = 0;   // under g_mutex: the last HUD panel's width, the shops' measure (ofh::findLayers)
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
    to.withheldTex = from.withheldTex;
    ofh::orderPanels(to.tally.panels, to.tally.count);
    for (int i = 0; i < to.tally.count; ++i) {
        to.tex[i] = nullptr;
        for (int j = 0; j < from.tally.count; ++j)
            if (from.tally.panels[j].id == to.tally.panels[i].id) to.tex[i] = from.tex[j];
    }
}

void say(const char* line) { Log::get().note("%s", line); }

// ---- .49: the Summary probe (summary_probe_core.h) -- TEMPORARY, removed whole after its flight ----
// While the trader's sheet is shown: the door reads its copy back every 0.5 s (a ring of staging copies, read without waiting) and
// writes the window's counts; the draw hook notes every eye draw of the eye-panel shader as a combination and every draw into the
// trader's panel by its vertex shader. The door holds g_mutex and then takes the probe's; the draw hook takes only the probe's.
std::atomic<bool> g_probeOn{false};              // the trader's sheet is shown (the door's; the draw hook reads it)
std::atomic<const void*> g_probePanel{nullptr};  // the trader's panel, identity only
struct Probe {
    std::mutex mutex;  // the tables and the line budget
    smp::Tables tables;
    uint32_t lines = 0, unwritten = 0;
    // the door's alone
    bool showing = false;
    uint64_t start = 0, lastPeriod = 0;
    uint32_t unread = 0;
    ID3D11Texture2D* ring[smp::kRing] = {};
    uint64_t ringStamp[smp::kRing] = {};  // ms since the show's start + 1, 0 for a free slot
    ID3D11Device* ringDevice = nullptr;   // identity only
    uint32_t ringW = 0, ringH = 0, ringFmt = 0;
    uint32_t ringNext = 0;
    bool failSaid = false;
    void releaseRing() {
        for (uint32_t i = 0; i < smp::kRing; ++i) {
            if (ring[i]) ring[i]->Release();
            ring[i] = nullptr;
            ringStamp[i] = 0;
        }
        ringDevice = nullptr;
        ringW = ringH = ringFmt = 0;
        ringNext = 0;
    }
};
Probe g_probe;

// A line within the show's budget; past it counted. Under g_probe.mutex.
void probeSay(const char* line) {
    if (g_probe.lines >= smp::kLinesPerShow) {
        ++g_probe.unwritten;
        return;
    }
    ++g_probe.lines;
    say(line);
}

uint64_t probeMs() {
    const uint64_t now = GetTickCount64();
    return g_probe.showing && now >= g_probe.start ? now - g_probe.start : 0;
}

// The draw hook: an eye draw of the eye-panel shader. Its pixel shader and slots 0-3, every reference given back before the return.
void probeEyeDraw(ID3D11DeviceContext* ctx, uint32_t count) {
    smp::Combo c;
    c.indices = count;
    c.ps = bindingGet(BindSlot::Ps) ? bindingShaderHash(BindSlot::Ps) : 0;
    ID3D11ShaderResourceView* srv[smp::kSlots] = {};
    ctx->PSGetShaderResources(0, smp::kSlots, srv);
    for (int i = 0; i < smp::kSlots; ++i) {
        if (!srv[i]) continue;
        ID3D11Resource* res = nullptr;
        srv[i]->GetResource(&res);
        srv[i]->Release();
        srv[i] = nullptr;
        if (!res) continue;
        ID3D11Texture2D* tex = nullptr;
        res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
        res->Release();
        if (!tex) continue;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        tex->Release();
        c.w[i] = d.Width;
        c.h[i] = d.Height;
        c.fmt[i] = uint32_t(d.Format);
    }
    std::lock_guard<std::mutex> lock(g_probe.mutex);
    bool fresh = false;
    const int index = g_probe.tables.noteCombo(c, &fresh);
    if (fresh) {
        char line[400];
        smp::formatCombo(index, c, probeMs(), line, sizeof(line));
        probeSay(line);
    }
}

// The draw hook: a draw into a target that is not an eye -- counted by its vertex shader when the target is the trader's panel.
void probeTargetDraw(uint64_t vs) {
    const uint32_t gen = bindingGeneration(BindSlot::Rtv0);
    if (gen != g_rtvGen) {
        g_rtvGen = gen;
        resolveTarget();
    }
    const void* panel = g_probePanel.load(std::memory_order_relaxed);
    if (!g_rtvPanel || !panel || g_rtvId != panel) return;
    std::lock_guard<std::mutex> lock(g_probe.mutex);
    g_probe.tables.noteVs(vs);
}

// Which layers' panels (this frame's, or last frame's while this one's are not drawn yet) does this draw of the game's composite
// shaders read as a texture in slots 0-2? Bit k: layer k. The game's own crosshair, markers and captions in stereo come through the
// same shaders from their own atlases and must not stop a sheet. The references the calls take are all given back before the return.
// `probe` (the aim dot wants this draw, aim_dot.h): the slots are read even with no layer's panel known, and `readOut` gets the
// resources behind them (identity only) -- one read of the slots for both.
uint32_t compositeReads(ID3D11DeviceContext* ctx, bool probe, const void* readOut[3]) {
    if (!ctx) return 0;
    const void* now[ofh::kLayers] = {};
    const void* last[ofh::kLayers] = {};
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        int c[ofh::kLayers], l[ofh::kLayers];
        ofh::findLayers(g_cur.tally.panels, g_cur.tally.count, c, g_hudWidth);
        ofh::findLayers(g_last.tally.panels, g_last.tally.count, l, g_hudWidth);
        for (int k = 0; k < ofh::kLayers; ++k) {
            if (c[k] >= 0) now[k] = g_cur.tally.panels[c[k]].id;
            if (l[k] >= 0) last[k] = g_last.tally.panels[l[k]].id;
            any = any || now[k] || last[k];
        }
    }
    if (!any && !probe) return 0;  // no layer's panel known and no probe: nothing to compare with, no COM call
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
    for (int i = 0; i < 3; ++i) readOut[i] = read[i];
    for (auto*& v : srv)
        if (v) {
            v->Release();
            v = nullptr;
        }
    return reads;
}

// The eye-panel shader's draw into an eye: is it the trader's menu laid at the head? The texture in pixel slot 1, its size and format,
// then on_foot_hud_core.h's classifyEyePanelDraw. Withhold: the panel is noted as the trader layer's for this frame, the frame keeps a
// reference to its texture (the first of the frame; the trader layer copies from it), and the caller skips the draw. Another mesh: one
// line a session, the draw left alone. Every other reference the calls take is given back before the return.
bool eyePanelWithhold(ID3D11DeviceContext* ctx, uint64_t vs, uint32_t count) {
    static std::atomic<bool> saidOtherMesh{false};
    if (!ctx) return false;
    ID3D11ShaderResourceView* srv = nullptr;
    ctx->PSGetShaderResources(ofh::kEyePanelSlot, 1, &srv);
    if (!srv) return false;
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    srv->Release();
    if (!res) return false;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    const void* id = res;  // identity only, as the tally's panels: the game holds the resource
    res->Release();
    if (!tex) return false;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    const bool hudRecent = g_hudRecent.load(std::memory_order_relaxed);
    const ofh::EyePanelDraw kind = ofh::classifyEyePanelDraw(vs, count, true, uint32_t(d.Format), d.Width, d.Height, hudRecent);
    if (kind == ofh::EyePanelDraw::OtherMesh && !saidOtherMesh.exchange(true)) {
        char line[300];
        ofh::formatOtherMesh(d.Width, d.Height, count, line, sizeof(line));
        say(line);
    }
    if (!ofh::isWithheldKind(kind)) {
        tex->Release();
        if (kind == ofh::EyePanelDraw::LoaderWide) {  // the loading dialog: left to the game, counted for the summary
            std::lock_guard<std::mutex> lock(g_mutex);
            ++g_cur.tally.wideLeftDraws;
        }
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cur.tally.withheld = id;
    ++g_cur.tally.withheldDraws;
    if (kind == ofh::EyePanelDraw::WithholdWide) ++g_cur.tally.withheldWideDraws;
    if (!g_cur.tally.withheldMesh) g_cur.tally.withheldMesh = count;
    if (!g_cur.withheldTex)
        g_cur.withheldTex = tex;  // the frame's reference, given back at the boundary after next
    else
        tex->Release();
    return true;
}
}  // namespace

bool onFootHudNoteDraw(ID3D11DeviceContext* ctx, bool eye, uint32_t count) {
    const uint64_t vs = bindingGet(BindSlot::Vs) ? bindingShaderHash(BindSlot::Vs) : 0;
    if (!vs) return false;
    if (ofh::isGameCompositeVs(vs)) {
        const bool probe = eye && aimDotHideErases();
        const void* read[3] = {};
        guardedBudget(g_budget, [&] {
            const uint32_t reads = compositeReads(ctx, probe, read);
            if (!reads) return;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_cur.tally.composites |= reads;
        });
        // The aim dot (aim_dot.h): a quad out of its atlas into an eye reads the centre back and erases the plain dot before it draws.
        if (probe) aimDotHideBeforeEyeComposite(ctx, read);
        return false;
    }
    if (vs == ofh::kEyePanelVs) {
        bool withhold = false;
        if (eye && g_probeOn.load(std::memory_order_relaxed)) guardedBudget(g_budget, [&] { probeEyeDraw(ctx, count); });  // .49
        if (eye) guardedBudget(g_budget, [&] { withhold = eyePanelWithhold(ctx, vs, count); });
        return withhold;
    }
    if (!eye && g_probeOn.load(std::memory_order_relaxed)) guardedBudget(g_budget, [&] { probeTargetDraw(vs); });  // .49
    if (eye || !ofh::isGuiVs(vs)) return false;
    guardedBudget(g_budget, [&] {
        const uint32_t gen = bindingGeneration(BindSlot::Rtv0);
        if (gen != g_rtvGen) {
            g_rtvGen = gen;
            resolveTarget();
        }
        if (!g_rtvPanel) return;
        if (aimDotHideWants()) aimDotHideNoteGuiDraw(ctx, vs, count, g_rtvId, g_rtvW, g_rtvH);  // the atlas is one of the panels
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
    return false;
}

void onFootHudFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool saidKey = false;
    static ofh::Mode lastMode = ofh::Mode::Body;
    static uint64_t windowStart = 0;
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string modeText = cfg.getString("experimental.native_vr_on_foot_hud", "body");
        bool modeValid = true;
        const ofh::Mode mode = ofh::parseMode(modeText.c_str(), &modeValid);
        if (!saidKey || mode != lastMode) {
            char line[600];
            ofh::formatKey(mode, modeValid, modeText.c_str(), line, sizeof(line));
            say(line);
            saidKey = true;
        }
        lastMode = mode;
        g_mode.store(int(mode), std::memory_order_relaxed);
        g_nativeOnFoot.store(cfg.getBool("experimental.native_vr_on_foot", true), std::memory_order_relaxed);
    }
    const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
    g_onFoot.store(onFoot, std::memory_order_relaxed);
    const bool wants = ofh::Mode(g_mode.load(std::memory_order_relaxed)) != ofh::Mode::Off &&
                       g_nativeOnFoot.load(std::memory_order_relaxed) && onFoot == 1;
    if (wants && !detail::g_onFootHudWants.load(std::memory_order_relaxed)) drawGateArm();
    detail::g_onFootHudWants.store(wants, std::memory_order_relaxed);
    if (!wants) detail::g_onFootMenuHeld.store(false, std::memory_order_relaxed);   // the door does not run its decision then: no stale menu
    const bool hudCensus =
        ofh::Mode(g_mode.load(std::memory_order_relaxed)) != ofh::Mode::Off && g_nativeOnFoot.load(std::memory_order_relaxed);
    aimDotHideFrameBoundary(hudCensus, wants);  // the aim dot (aim_dot.h): its key, the weapon cue's lines, its summary

    const uint64_t now = GetTickCount64();
    if (!windowStart) windowStart = now;
    std::lock_guard<std::mutex> lock(g_mutex);
    // The frame that ended becomes the door's fallback (a Submit after the Present reads it); the one before gives its references back.
    Frame ranked;
    rankInto(g_cur, ranked);
    g_last.release();
    g_last = ranked;
    const int hud = ofh::findHud(g_last.tally.panels, g_last.tally.count);
    if (hud >= 0) g_hudWidth = g_last.tally.panels[hud].w;
    // .48: frames since the last HUD panel, kHoldFrames for none; "no" while not on foot, so a loading right at stepping out is not
    // taken for a menu.
    static uint32_t sinceHud = ofh::kHoldFrames;
    if (!wants)
        sinceHud = ofh::kHoldFrames;
    else if (hud >= 0)
        sinceHud = 0;
    else if (sinceHud < ofh::kHoldFrames)
        ++sinceHud;
    g_hudRecent.store(wants && sinceHud < ofh::kHoldFrames, std::memory_order_relaxed);
    for (auto*& t : g_cur.tex) t = nullptr;  // carried into g_last
    g_cur.withheldTex = nullptr;              // so is this
    g_cur.tally.clear();
    if (wants) {
        ++g_summary.frames;
        g_summary.withheld += g_last.tally.withheldDraws;
        g_summary.withheldWide += g_last.tally.withheldWideDraws;
        g_summary.wideLeft += g_last.tally.wideLeftDraws;
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

// .49, the Summary probe's door half (TEMPORARY): once per frame for the trader's layer, after its decision. Shown: every 0.5 s the
// ring's ready copies are read (oldest first, never waiting) and counted, the sheet's copy goes into the next slot, and the window's
// counts are written. Not shown: the show's end line, the ring given back. Under g_mutex.
void probeDoor(ID3D11Device* device, const LayerState& L, const void* panel) {
    if (!L.show || !L.copy) {
        g_probeOn.store(false, std::memory_order_relaxed);
        if (!g_probe.showing) return;
        std::lock_guard<std::mutex> lock(g_probe.mutex);
        char line[200];
        smp::formatEnd(probeMs(), g_probe.unwritten, g_probe.unread, line, sizeof(line));
        say(line);  // the end line is always written
        g_probe.showing = false;
        g_probe.releaseRing();
        g_probe.tables.clear();
        g_probePanel.store(nullptr, std::memory_order_relaxed);
        return;
    }
    if (panel) g_probePanel.store(panel, std::memory_order_relaxed);
    const uint64_t now = GetTickCount64();
    if (!g_probe.showing) {
        std::lock_guard<std::mutex> lock(g_probe.mutex);
        g_probe.showing = true;
        g_probe.start = now;
        g_probe.lastPeriod = 0;
        g_probe.lines = g_probe.unwritten = g_probe.unread = 0;
        g_probe.tables.clear();
        char line[300];
        smp::formatStart(L.w, L.h, L.fmt, line, sizeof(line));
        probeSay(line);
        g_probeOn.store(true, std::memory_order_relaxed);
    }
    if (g_probe.lastPeriod && now - g_probe.lastPeriod < smp::kPeriodMs) return;
    g_probe.lastPeriod = now;
    guardedBudget(g_budget, [&] {
        if (!g_probe.ring[0] || g_probe.ringDevice != device || g_probe.ringW != L.w || g_probe.ringH != L.h || g_probe.ringFmt != L.fmt) {
            g_probe.releaseRing();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = L.w;
            d.Height = L.h;
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT(L.fmt);
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            bool ok = true;
            for (uint32_t i = 0; i < smp::kRing && ok; ++i)
                ok = SUCCEEDED(device->CreateTexture2D(&d, nullptr, &g_probe.ring[i])) && g_probe.ring[i];
            if (!ok) {
                g_probe.releaseRing();
                if (!g_probe.failSaid) {
                    g_probe.failSaid = true;
                    say("summary probe: the staging copies could not be made; no copy lines. Said once.");
                }
                return;
            }
            g_probe.ringDevice = device;
            g_probe.ringW = L.w;
            g_probe.ringH = L.h;
            g_probe.ringFmt = L.fmt;
        }
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (!ctx) return;
        // The COM calls outside the probe's lock (the draw hook takes it); the lines after, under it.
        char copyLines[smp::kRing][300];
        uint32_t copyCount = 0;
        for (;;) {
            int oldest = -1;
            for (uint32_t i = 0; i < smp::kRing; ++i)
                if (g_probe.ringStamp[i] && (oldest < 0 || g_probe.ringStamp[i] < g_probe.ringStamp[oldest])) oldest = int(i);
            if (oldest < 0) break;
            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT hr = ctx->Map(g_probe.ring[oldest], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) break;
            if (SUCCEEDED(hr)) {
                const smp::PixelStats st = smp::pixelStats(static_cast<const uint8_t*>(m.pData), m.RowPitch, L.w, L.h);
                ctx->Unmap(g_probe.ring[oldest], 0);
                if (copyCount < smp::kRing)
                    smp::formatCopy(g_probe.ringStamp[oldest] - 1, L.w, L.h, st, copyLines[copyCount++], sizeof(copyLines[0]));
            } else if (!g_probe.failSaid) {
                g_probe.failSaid = true;
                say("summary probe: a staging copy could not be read; it is dropped. Said once.");
            }
            g_probe.ringStamp[oldest] = 0;
        }
        const uint32_t slot = g_probe.ringNext;
        if (g_probe.ringStamp[slot]) ++g_probe.unread;
        ctx->CopyResource(g_probe.ring[slot], L.copy);
        g_probe.ringStamp[slot] = (now - g_probe.start) + 1;
        g_probe.ringNext = (slot + 1) % smp::kRing;
        ctx->Release();
        std::lock_guard<std::mutex> lock(g_probe.mutex);
        for (uint32_t i = 0; i < copyCount; ++i) probeSay(copyLines[i]);
        char line[600];
        smp::formatWindow(now - g_probe.start, g_probe.tables, line, sizeof(line));
        g_probe.tables.newWindow();
        probeSay(line);
    });
}

// Eye 0's decision for every layer, its copies and its lines. Under g_mutex.
void decideLayers(ID3D11Device* device, ofh::Mode mode, float width) {
    // Each layer's panel: this frame's when the Submit comes after its draws, else the one of the frame that just ended. A frame that
    // drew none leaves the layer's last copy to be held.
    Frame ranked;
    if (g_cur.tally.count) rankInto(g_cur, ranked);
    // The trader's: the panel this frame's (or the last frame's) withheld draw read, not a shape.
    int cur[ofh::kLayers], last[ofh::kLayers];
    ofh::findLayerPanels(ranked.tally, cur, g_hudWidth);
    ofh::findLayerPanels(g_last.tally, last, g_hudWidth);
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
        // What a fresh copy comes from: the layer's panel; the trader's, the texture its withheld draw read (.28) -- this frame's,
        // else the last frame's -- with the panel's draw count when the interface shaders drew it too.
        ID3D11Texture2D* source = use ? use->tex[index] : nullptr;
        ofh::Panel fresh;
        if (use) fresh = use->tally.panels[index];
        if (layer == ofh::Layer::Trader) {
            source = g_cur.withheldTex ? g_cur.withheldTex : g_last.withheldTex;
            if (!use) fresh = ofh::Panel{};
        }
        ofh::DecisionIn in = base;
        in.gameComposites = ((composites >> k) & 1u) != 0;
        ofh::HoldStep step = ofh::HoldStep::Expired;
        const bool drew = layer == ofh::Layer::Trader ? source != nullptr : use != nullptr;
        const ofh::Reason reason = ofh::stepLayer(in, drew, L.hold, &step);
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
            panel = fresh;
            if (copyInto(L, device, source)) {
                panel.w = L.w;  // the copy's size: the trader's panel may not be in the tally
                panel.h = L.h;
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
                // the trader's: the mesh of this frame's first withheld draw, else the last frame's (.48)
                ofh::formatMenuShowing(layer, panel, L.place, mode, line, sizeof(line),
                                       layer == ofh::Layer::Trader
                                           ? (g_cur.tally.withheldMesh ? g_cur.tally.withheldMesh : g_last.tally.withheldMesh)
                                           : 0u);
            else
                ofh::formatMenuStopped(layer, reason, line, sizeof(line));
            say(line);
        }
    }
    // .49: the Summary probe, after the trader's decision (TEMPORARY)
    probeDoor(device, g_layers[int(ofh::Layer::Trader)], g_cur.tally.withheld ? g_cur.tally.withheld : g_last.tally.withheld);
    for (auto*& t : ranked.tex) t = nullptr;  // g_cur's, not this copy's to release
    ranked.withheldTex = nullptr;
    // A menu layer (anything but the HUD) is held: shown, or kept by its hold. The head aim reads this (onFootMenuHeld).
    bool menu = false;
    for (int k = 0; k < ofh::kLayers; ++k)
        if (ofh::Layer(k) != ofh::Layer::Hud && g_layers[k].show) menu = true;
    detail::g_onFootMenuHeld.store(menu, std::memory_order_relaxed);
}
}  // namespace

int onFootHudSheetsForEye(ID3D11Device* device, uint32_t eye, OnFootHudSheet out[kOnFootHudSheets]) {
    if (!device || !out || eye > 1) return 0;
    const uint64_t seq = g_frameSeq.load(std::memory_order_relaxed);
    const ofh::Mode mode = ofh::Mode(g_mode.load(std::memory_order_relaxed));
    const float width = ofh::kWidthDefault;
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
