#include "temporal_shader_bytecode.h"
#include "on_foot_hud_layout.h"
#include "on_foot_hud_probe_core.h"
#include "animated_vertex_history.h"
#include "draw_census.h"
#include "../common/log.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

// On-foot HUD probe, .22: see on_foot_hud_layout.h. Everything here runs on the thread that draws (inside EDVR's draw path, under the
// context's own use) and takes no lock: one try-flag serialises the replay and the reads (a second thread in the window just skips its
// turn), so the slots, the geometry shader cache and the staging buffers are one thread's at a time.

namespace edvr {
namespace {

using Microsoft::WRL::ComPtr;

std::atomic_flag g_busy = ATOMIC_FLAG_INIT;

// The shader's own: the declaration the stream output was made from (plain data, dies with the shader).
const GUID kDeclKey = {0x2e9a6d03, 0x58c1, 0x47f4, {0xa3, 0x1b, 0x6f, 0x90, 0xe2, 0x74, 0xc5, 0x1d}};

// The stream-output geometry shaders by vertex shader. A pointer reused by another shader is found out by its missing declaration
// (private data goes with the object), and its entry is made again.
struct GsEntry {
    ID3D11VertexShader* vs = nullptr;
    ComPtr<ID3D11GeometryShader> gs;
};
constexpr int kGsCache = 64;
GsEntry g_gs[kGsCache];
int g_gsNext = 0;

ID3D11GeometryShader* findGs(ID3D11VertexShader* vs) {
    for (const GsEntry& e : g_gs)
        if (e.vs == vs) return e.gs.Get();
    return nullptr;
}
void putGs(ID3D11VertexShader* vs, ID3D11GeometryShader* gs) {
    for (GsEntry& e : g_gs)
        if (e.vs == vs) {
            e.gs = gs;
            return;
        }
    GsEntry& e = g_gs[g_gsNext];
    g_gsNext = (g_gsNext + 1) % kGsCache;
    e.vs = vs;
    e.gs = gs;
}

// One staging slot of the ring of four: the stream-output buffer and its staging copy (grown when a bigger draw comes), the panel kept
// while the vertices are read, the panel's staging copy, and what the snapshot is.
struct Slot {
    ComPtr<ID3D11Buffer> so, staging;
    UINT capacity = 0;
    ComPtr<ID3D11Texture2D> panelSrc, panelStage;
    UINT stageW = 0, stageH = 0, stageFormat = 0;
    ID3D11DeviceContext* owner = nullptr;
    int row = -1;
    ohp::SnapInfo info{};
    uint32_t vertices = 0, perVertex = 4;
    std::unique_ptr<ohp::SnapshotResult> pending;  // the result waiting for its panel image
};
Slot g_slots[ohp::kReplayRing];
ohp::SlotRing g_ring;

void releaseSlot(int i) {
    Slot& s = g_slots[i];
    s.panelSrc.Reset();
    s.pending.reset();
    s.owner = nullptr;
    g_ring.release(i);
}

bool ensureBuffers(ID3D11Device* dev, Slot& s, UINT bytes) {
    if (s.so && s.staging && s.capacity >= bytes) return true;
    const UINT capacity = (bytes + 65535u) & ~65535u;
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = capacity;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_STREAM_OUTPUT;
    ComPtr<ID3D11Buffer> so, staging;
    if (FAILED(dev->CreateBuffer(&d, nullptr, &so))) return false;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateBuffer(&d, nullptr, &staging))) return false;
    s.so = so;
    s.staging = staging;
    s.capacity = capacity;
    return true;
}

// The declaration (from the shader's own code) and the geometry shader made from it. -1 to go, or a refusal.
int prepareShader(ID3D11Device* dev, ID3D11VertexShader* vs, ohp::SoDecl* decl, ID3D11GeometryShader** gsOut) {
    UINT size = sizeof(ohp::SoDecl);
    ohp::SoDecl d{};
    const bool have = SUCCEEDED(vs->GetPrivateData(kDeclKey, &size, &d)) && size == sizeof(ohp::SoDecl);
    std::vector<unsigned char> code;
    auto readCode = [&]() {
        UINT codeSize = 0;
        if (FAILED(vs->GetPrivateData(AnimatedVertexHistory::bytecodeKey, &codeSize, nullptr)) || !codeSize) return false;
        code.resize(codeSize);
        return SUCCEEDED(vs->GetPrivateData(AnimatedVertexHistory::bytecodeKey, &codeSize, code.data()));
    };
    if (!have) {
        if (!readCode()) return ohp::kRefNoCode;
        d = ohp::parseSoDecl(code.data(), code.size());
        vs->SetPrivateData(kDeclKey, sizeof(d), &d);
    }
    if (!d.ok) return ohp::kRefNoOutput;
    if (d.gsFailed) return ohp::kRefCreate;
    // A shader without a declaration is a new object (maybe at an old shader's address): its cache entry, if any, is not its own.
    ID3D11GeometryShader* gs = have ? findGs(vs) : nullptr;
    if (!gs) {
        if (code.empty() && !readCode()) return ohp::kRefNoCode;
        D3D11_SO_DECLARATION_ENTRY e[2] = {};
        for (int i = 0; i < d.entries; ++i) {
            e[i].Stream = 0;
            e[i].SemanticName = d.e[i].name;
            e[i].SemanticIndex = d.e[i].semanticIndex;
            e[i].StartComponent = d.e[i].start;
            e[i].ComponentCount = d.e[i].count;
            e[i].OutputSlot = 0;
        }
        const UINT stride = d.stride;
        ComPtr<ID3D11GeometryShader> made;
        if (FAILED(dev->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), e, d.entries, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &made))) {
            d.gsFailed = 1;
            vs->SetPrivateData(kDeclKey, sizeof(d), &d);
            return ohp::kRefCreate;
        }
        putGs(vs, made.Get());
        gs = made.Get();
    }
    *decl = d;
    *gsOut = gs;
    return -1;
}

// One replay, in the order ohp::replaySequence drives.
struct ReplayOps {
    ID3D11DeviceContext* ctx;
    const ohp::DrawShape& shape;
    char kind;
    unsigned count, instances;
    const DrawArgs& args;
    bool own;
    OnFootHudRealDraw real;

    int slot = -1;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11Device> dev;
    ohp::SoDecl decl{};
    ID3D11GeometryShader* ownGs = nullptr;  // owned by the cache
    uint32_t vertices = 0;
    // What the replay saved.
    ComPtr<ID3D11GeometryShader> savedGs;
    ID3D11ClassInstance* savedClasses[256] = {};
    UINT savedClassCount = 256;
    ID3D11Buffer* savedSo[4] = {};
    D3D11_PRIMITIVE_TOPOLOGY savedTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

    int check() {
        if (!own) return ohp::kRefForeign;
        if (!real) return ohp::kRefCreate;
        if (kind != 'D' && kind != 'I' && kind != 'N' && kind != 'X') return ohp::kRefIndirect;
        ComPtr<ID3D11GeometryShader> gs0;
        ComPtr<ID3D11HullShader> hs;
        ComPtr<ID3D11DomainShader> ds;
        ctx->GSGetShader(&gs0, nullptr, nullptr);
        ctx->HSGetShader(&hs, nullptr, nullptr);
        ctx->DSGetShader(&ds, nullptr, nullptr);
        if (gs0 || hs || ds) return ohp::kRefGsOrTess;
        ctx->VSGetShader(&vs, nullptr, nullptr);
        if (!vs) return ohp::kRefNoCode;
        ctx->GetDevice(&dev);
        if (!dev) return ohp::kRefCreate;
        const int prepared = prepareShader(dev.Get(), vs.Get(), &decl, &ownGs);
        if (prepared >= 0) return prepared;
        const uint64_t out = static_cast<uint64_t>(count) * (instances ? instances : 1u);
        if (out > ohp::kReplayMaxVertices) return ohp::kRefTooBig;
        vertices = static_cast<uint32_t>(out);
        slot = g_ring.acquire();
        if (slot < 0) return ohp::kRefRingBusy;
        if (!ensureBuffers(dev.Get(), g_slots[slot], vertices * decl.stride)) {
            g_ring.release(slot);
            slot = -1;
            return ohp::kRefCreate;
        }
        return -1;
    }
    void save() {
        ctx->GSGetShader(&savedGs, savedClasses, &savedClassCount);
        ctx->SOGetTargets(4, savedSo);
        ctx->IAGetPrimitiveTopology(&savedTopology);
    }
    void bind() {
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ctx->GSSetShader(ownGs, nullptr, 0);
        ID3D11Buffer* target = g_slots[slot].so.Get();
        const UINT offset = 0;
        ctx->SOSetTargets(1, &target, &offset);
    }
    void draw() { real(ctx, kind, count, instances, args); }
    void restore() {
        ctx->SOSetTargets(0, nullptr, nullptr);
        ctx->GSSetShader(savedGs.Get(), savedGs ? savedClasses : nullptr, savedGs ? savedClassCount : 0);
        bool any = false;
        for (ID3D11Buffer* b : savedSo) any = any || b != nullptr;
        if (any) {
            const UINT append[4] = {~0u, ~0u, ~0u, ~0u};  // -1: continue where the buffer was
            ctx->SOSetTargets(4, savedSo, append);
        }
        ctx->IASetPrimitiveTopology(savedTopology);
        for (UINT i = 0; i < savedClassCount; ++i)
            if (savedClasses[i]) savedClasses[i]->Release();
        for (ID3D11Buffer*& b : savedSo)
            if (b) {
                b->Release();
                b = nullptr;
            }
    }
    void copyOut() {
        const D3D11_BOX box = {0, 0, 0, vertices * decl.stride, 1, 1};
        ctx->CopySubresourceRegion(g_slots[slot].staging.Get(), 0, 0, 0, 0, g_slots[slot].so.Get(), 0, &box);
    }

    // After a replay that went through: the slot waits for the GPU. The panel is kept (a reference) for a copy when the snapshot gives a line.
    void finish() {
        Slot& s = g_slots[slot];
        s.owner = ctx;
        s.row = -1;  // set by the caller
        s.vertices = vertices;
        s.perVertex = decl.stride / 4;
        s.info = ohp::SnapInfo{};
        s.info.list = shape.list;
        s.info.kind = shape.kind;
        s.info.own = shape.own;
        s.info.place = shape.place;
        s.info.viewBit = shape.viewBit;
        s.info.count = count;
        s.info.instances = instances;
        s.info.vs = shape.vs;
        s.info.ps = shape.ps;
        s.info.panelSlot = ohp::panelSlotOf(shape.vs);
        s.info.panel = shape.tex[s.info.panelSlot];
        s.info.target = shape.rt;
        s.panelSrc.Reset();
        ComPtr<ID3D11ShaderResourceView> srv;
        ctx->PSGetShaderResources(static_cast<UINT>(s.info.panelSlot), 1, srv.GetAddressOf());
        if (srv) {
            ComPtr<ID3D11Resource> res;
            srv->GetResource(&res);
            ComPtr<ID3D11Texture2D> tex;
            if (res && SUCCEEDED(res.As(&tex)) && reinterpret_cast<uint64_t>(res.Get()) == s.info.panel.resource) s.panelSrc = tex;
        }
    }
};

ohp::RefusalNote noteOf(const ohp::DrawShape& s, unsigned count, unsigned instances) {
    ohp::RefusalNote n{};
    n.list = s.list;
    n.place = s.place;
    n.kind = s.kind;
    n.count = count;
    n.instances = instances;
    n.vs = s.vs;
    n.ps = s.ps;
    return n;
}

// A result for the frame thread; the slot is free again.
void submitAndRelease(int i, std::unique_ptr<ohp::SnapshotResult> r) {
    onFootHudLayoutSubmit(r.release());
    releaseSlot(i);
}

// The earlier replays' answers, without waiting: Map with DO_NOT_WAIT on the context they were issued on.
void pollReads(ID3D11DeviceContext* ctx) {
    for (int i = 0; i < ohp::kReplayRing; ++i) {
        const ohp::SlotRing::State st = g_ring.state(i);
        Slot& s = g_slots[i];
        if ((st != ohp::SlotRing::kVertices && st != ohp::SlotRing::kPanel) || s.owner != ctx) continue;
        const uint64_t frame = onFootHudProbeFrame();
        if (st == ohp::SlotRing::kVertices) {
            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT hr = ctx->Map(s.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            const ohp::ReadMap rm = SUCCEEDED(hr) ? ohp::kMapOk : hr == DXGI_ERROR_WAS_STILL_DRAWING ? ohp::kMapBusy : ohp::kMapFailed;
            const ohp::PollVerdict verdict = ohp::pollVerdict(rm, g_ring.frame(i), frame);
            if (verdict == ohp::kPollWait) continue;
            if (verdict != ohp::kPollReady) {
                ohp::RefusalNote n{};
                n.list = s.info.list;
                n.place = s.info.place;
                n.kind = s.info.kind;
                n.count = s.info.count;
                n.instances = s.info.instances;
                n.vs = s.info.vs;
                n.ps = s.info.ps;
                onFootHudLayoutRefuse(verdict == ohp::kPollTimeout ? ohp::kRefTimeout : ohp::kRefRead, n);
                releaseSlot(i);
                continue;
            }
            std::vector<float> v(static_cast<size_t>(s.vertices) * s.perVertex);
            std::memcpy(v.data(), m.pData, v.size() * sizeof(float));
            ctx->Unmap(s.staging.Get(), 0);
            auto r = std::make_unique<ohp::SnapshotResult>();
            r->info = s.info;
            r->floatsPerVertex = s.perVertex;
            ohp::summarizeLayout(v.data(), s.vertices, s.perVertex, &r->layout);
            onFootHudLayoutSnapped(s.info.place);
            if (!onFootHudLayoutDecide(s.row, r->layout)) {
                releaseSlot(i);
                continue;
            }
            r->vertices = std::move(v);
            // The panel image, only for a snapshot that gives a line, and only 8 bits a channel up to 4096x4096.
            D3D11_TEXTURE2D_DESC pd{};
            bool copy = false;
            if (s.panelSrc) {
                s.panelSrc->GetDesc(&pd);
                const int order = ohp::rgba8Order(static_cast<uint32_t>(pd.Format));
                if (!order || pd.SampleDesc.Count != 1) r->panelState = ohp::kPanelFormat;
                else if (!pd.Width || !pd.Height || pd.Width > ohp::kPanelMaxSide || pd.Height > ohp::kPanelMaxSide) r->panelState = ohp::kPanelTooBig;
                else {
                    r->panelBgra = order == 2 ? 1 : 0;
                    copy = true;
                }
            }
            if (copy) {
                ComPtr<ID3D11Device> dev;
                ctx->GetDevice(&dev);
                if (!s.panelStage || s.stageW != pd.Width || s.stageH != pd.Height || s.stageFormat != static_cast<UINT>(pd.Format)) {
                    D3D11_TEXTURE2D_DESC d{};
                    d.Width = pd.Width;
                    d.Height = pd.Height;
                    d.MipLevels = 1;
                    d.ArraySize = 1;
                    d.Format = pd.Format;
                    d.SampleDesc.Count = 1;
                    d.Usage = D3D11_USAGE_STAGING;
                    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    s.panelStage.Reset();
                    if (dev && SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &s.panelStage))) {
                        s.stageW = pd.Width;
                        s.stageH = pd.Height;
                        s.stageFormat = static_cast<UINT>(pd.Format);
                    }
                }
                if (s.panelStage) {
                    ctx->CopySubresourceRegion(s.panelStage.Get(), 0, 0, 0, 0, s.panelSrc.Get(), 0, nullptr);
                    r->panelW = pd.Width;
                    r->panelH = pd.Height;
                    s.pending = std::move(r);
                    s.panelSrc.Reset();
                    g_ring.set(i, ohp::SlotRing::kPanel, frame);
                    continue;
                }
                r->panelState = ohp::kPanelNotRead;
            }
            submitAndRelease(i, std::move(r));
        } else {
            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT hr = ctx->Map(s.panelStage.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            const ohp::ReadMap rm = SUCCEEDED(hr) ? ohp::kMapOk : hr == DXGI_ERROR_WAS_STILL_DRAWING ? ohp::kMapBusy : ohp::kMapFailed;
            const ohp::PollVerdict verdict = ohp::pollVerdict(rm, g_ring.frame(i), frame);
            if (verdict == ohp::kPollWait) continue;
            std::unique_ptr<ohp::SnapshotResult> r = std::move(s.pending);
            if (!r) {
                if (verdict == ohp::kPollReady) ctx->Unmap(s.panelStage.Get(), 0);
                releaseSlot(i);
                continue;
            }
            if (verdict == ohp::kPollReady) {
                r->pixels.resize(static_cast<size_t>(r->panelW) * r->panelH * 4);
                for (UINT y = 0; y < r->panelH; ++y)
                    std::memcpy(r->pixels.data() + static_cast<size_t>(y) * r->panelW * 4, static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                                static_cast<size_t>(r->panelW) * 4);
                ctx->Unmap(s.panelStage.Get(), 0);
                r->panelState = ohp::kPanelRead;
            } else {
                r->panelState = ohp::kPanelNotRead;
            }
            submitAndRelease(i, std::move(r));
        }
    }
}

}  // namespace

void onFootHudLayoutSnapshot(ID3D11DeviceContext* ctx, const ohp::DrawShape& shape, char kind, unsigned count, unsigned instances, const DrawArgs& args, bool own,
                             OnFootHudRealDraw real) {
    if (!ctx) return;
    if (g_busy.test_and_set(std::memory_order_acquire)) return;  // another thread is in it: skip this turn
    struct Unlock {
        ~Unlock() { g_busy.clear(std::memory_order_release); }
    } unlock;
    if (own) pollReads(ctx);
    if (!count) return;
    int row = -1;
    uint64_t prior = 0;
    if (!onFootHudLayoutClaim(ohp::shapeKey(shape), GetTickCount64(), &row, &prior)) return;
    ReplayOps ops{ctx, shape, kind, count, instances, args, own, real};
    const int refusal = ohp::replaySequence(ops);
    if (refusal == ohp::kRefRingBusy) onFootHudLayoutUnclaim(row, prior);
    if (refusal >= 0) {
        onFootHudLayoutRefuse(refusal, noteOf(shape, count, instances));
        return;
    }
    ops.finish();
    g_slots[ops.slot].row = row;
    g_ring.set(ops.slot, ohp::SlotRing::kVertices, onFootHudProbeFrame());
}

void onFootHudLayoutRememberVs(ID3D11VertexShader* shader, const void* bytecode, size_t length, bool hasLinkage) {
    if (!shader || !bytecode || !length || hasLinkage || length > 65536) return;
    UINT held = 0;
    if (SUCCEEDED(shader->GetPrivateData(AnimatedVertexHistory::bytecodeKey, &held, nullptr)) && held) return;  // the weapon family's: one copy
    if (onFootHudProbeCodeAdd(length) != 0) return;
    AnimatedVertexHistory::rememberShader(shader, bytecode, length);
}

}  // namespace edvr
