// EDVRonFOOT desktop eye view: see desktop_view.h. The provider of the runtime's table, and the draw before the game's Present.
#include "desktop_view.h"
#include "desktop_view_core.h"
#include "shader_swap.h"
#include "temporal_shader_bytecode.h"   // kDesktopViewVsBytecode, kDesktopViewPsBytecode (desktop_view_shader.h, compiled at build time)
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"
#include "../common/native_desktop_view.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace edvr {
namespace {

constexpr uint64_t kLineMs = 30000;

// The runtime's session (one at a time; reacquired after a close).
struct Session {
    bool active = false;
    uint64_t generation = 0;
};
Session g_session;
std::atomic<bool> g_everAcquired{false};

// The key, read on the render thread at a config change; noteEye reads only this.
std::atomic<int> g_view{int(dv::View::Off)};

// The eye held for the next Present. Guarded by g_mutex: noteEye runs on the producer thread, the Present on the render thread
// (one thread in Elite, but nothing here relies on it).
std::mutex g_mutex;
struct Held {
    ComPtr<ID3D11Texture2D> texture;
    float bounds[4]{};
    EdvrNativeDesktopViewPose pose{};   // desktop-eye-steady: the picture's orientation and angles (valid 0: none)
    uint64_t sequence = 0, presentIndex = 0;
    uint32_t eye = 0;
};
// The lens masks the runtime sent (noteMask), per eye, with a revision the render side compares.
std::vector<float> g_masks[2];
uint64_t g_maskRevision[2]{};
Held g_held;
uint64_t g_presentIndex = 0;   // non-TEST Presents counted so far (under g_mutex)

// The render thread's own state.
struct Render {
    uint32_t generation = UINT32_MAX;
    dv::View view = dv::View::Off;
    dv::Fit fit = dv::Fit::Crop;
    bool steady = true;
    bool saidKey = false;
    std::string rawView, rawFit, rawSteady;
    // desktop-eye-steady: the smoothed orientation, for the eye it follows.
    dv::SteadyFilter filter;
    dv::SwayMeter headSway, windowSway;   // the log's measure: the head's jitter and what of it reaches the window
    void resetSteady() { filter.reset(); headSway.reset(); windowSway.reset(); }
    uint32_t filterEye = UINT32_MAX;
    uint64_t lastDrawnSequence = 0;
    uint64_t windowStartMs = 0;
    dv::Window window;
    // GPU objects, made at the first draw for this device.
    ID3D11Device* device = nullptr;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    bool madeFailed = false;
    // A ring of timestamp pairs, read back a few frames later without a flush.
    struct Timer {
        ComPtr<ID3D11Query> disjoint, begin, end;
        bool pending = false;
    } timers[4];
    unsigned nextTimer = 0;
    bool timersFailed = false;
    // The render side's copy of each eye's mask (taken at a Present when its revision moved), and zoom's last placement.
    std::vector<float> mask[2];
    uint64_t maskRevision[2]{};
    // The placement, made again only when an input changed (the fit, steady, the eye, its mask, a size, the picture's angles beyond
    // a hair: a refused temporal pass shifts them by a jitter each frame).
    struct PlaceCache {
        bool valid = false, steady = false, hasFov = false;
        dv::Fit fit = dv::Fit::Crop;
        uint32_t eye = 0, windowW = 0, windowH = 0, eyeW = 0, eyeH = 0;
        uint64_t maskRevision = 0;
        double fov[4]{};
        dv::Placement place;
        double share = 1;
    } cache;
};
Render g_r;
FaultBudget g_budget("desktop view", 3);

uint64_t nowMs() { return GetTickCount64(); }

bool sameDevice(ID3D11Device* expected, ID3D11DeviceChild* child) {
    if (!expected || !child) return false;
    ComPtr<ID3D11Device> owner;
    child->GetDevice(owner.GetAddressOf());
    if (!owner) return false;
    ComPtr<IUnknown> a, b;
    return SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&a))) && SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&b))) && a == b;
}

void readKeys() {
    const Config& cfg = Config::get();
    if (cfg.generation() == g_r.generation) return;
    g_r.generation = cfg.generation();
    const std::string rawView = cfg.getString("experimental.native_vr_desktop_view", "off");
    const std::string rawFit = cfg.getString("experimental.native_vr_desktop_view_fit", "crop");
    const std::string rawSteady = cfg.getString("experimental.native_vr_desktop_view_steady", "on");
    bool viewValid = true, fitValid = true, steadyValid = true;
    const dv::View view = dv::parseView(rawView.c_str(), &viewValid);
    const dv::Fit fit = dv::parseFit(rawFit.c_str(), &fitValid);
    const bool steady = dv::parseSteady(rawSteady.c_str(), &steadyValid);
    if (!steadyValid && rawSteady != g_r.rawSteady)
        Log::get().note("desktop view: experimental.native_vr_desktop_view_steady = '%s' is not on or off; read as on.", rawSteady.c_str());
    if (!viewValid && rawView != g_r.rawView)
        Log::get().note("desktop view: experimental.native_vr_desktop_view = '%s' is not off, right or left; read as off.", rawView.c_str());
    if (!fitValid && rawFit != g_r.rawFit)
        Log::get().note("desktop view: experimental.native_vr_desktop_view_fit = '%s' is not crop or whole; read as crop.", rawFit.c_str());
    if (!g_r.saidKey || view != g_r.view || fit != g_r.fit || steady != g_r.steady)
        Log::get().note("desktop view: key %s, fit %s, steady %s%s.", dv::viewName(view), dv::fitName(fit), steady ? "on" : "off",
                        g_everAcquired.load() ? "" : " (the runtime has not handed over an eye table yet)");
    if (steady != g_r.steady || fit != g_r.fit || view != g_r.view) g_r.resetSteady();
    g_r.saidKey = true;
    g_r.rawView = rawView; g_r.rawFit = rawFit; g_r.rawSteady = rawSteady;
    g_r.view = view; g_r.fit = fit; g_r.steady = steady;
    g_view.store(int(view), std::memory_order_release);
}

bool makeObjects(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    if (g_r.device != device) {
        g_r.vs.Reset(); g_r.ps.Reset(); g_r.sampler.Reset(); g_r.constants.Reset();
        for (auto& t : g_r.timers) t = {};
        g_r.madeFailed = false; g_r.timersFailed = false; g_r.nextTimer = 0;
        g_r.device = device;
    }
    if (g_r.madeFailed) return false;
    if (g_r.vs && g_r.ps && g_r.sampler && g_r.constants) return true;
    g_r.vs.Attach(shaderSwapCreateVs(ctx, kDesktopViewVsBytecode, sizeof(kDesktopViewVsBytecode), "desktop_view_vs", "desktop view"));
    g_r.ps.Attach(shaderSwapCreatePs(ctx, kDesktopViewPsBytecode, sizeof(kDesktopViewPsBytecode), "desktop_view_ps", "desktop view"));
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    s.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (FAILED(device->CreateSamplerState(&s, g_r.sampler.GetAddressOf()))) g_r.sampler.Reset();
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = 112;   // desktop_view_shader.h: m0..m2, win, frame, bounds, mode
    b.Usage = D3D11_USAGE_DYNAMIC;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&b, nullptr, g_r.constants.GetAddressOf()))) g_r.constants.Reset();
    if (!(g_r.vs && g_r.ps && g_r.sampler && g_r.constants)) {
        g_r.madeFailed = true;
        Log::get().note("desktop view: could not make its shaders or states; the window keeps the game's picture for this session.");
        return false;
    }
    return true;
}

// The game's state for everything the draw touches, taken out and put back exactly.
struct SavedState {
    ComPtr<ID3D11InputLayout> layout;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11HullShader> hs;
    ComPtr<ID3D11DomainShader> ds;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> vsCb, psCb;
    ComPtr<ID3D11ShaderResourceView> psSrv;
    ComPtr<ID3D11SamplerState> psSampler;
    ComPtr<ID3D11RasterizerState> rs;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv = nullptr;
    ComPtr<ID3D11BlendState> blend;
    float blendFactor[4]{};
    UINT sampleMask = 0;
    ComPtr<ID3D11DepthStencilState> depth;
    UINT stencilRef = 0;

    void capture(ID3D11DeviceContext* c) {
        c->IAGetInputLayout(layout.GetAddressOf());
        c->IAGetPrimitiveTopology(&topology);
        c->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
        c->HSGetShader(hs.GetAddressOf(), nullptr, nullptr);
        c->DSGetShader(ds.GetAddressOf(), nullptr, nullptr);
        c->GSGetShader(gs.GetAddressOf(), nullptr, nullptr);
        c->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
        c->VSGetConstantBuffers(0, 1, vsCb.GetAddressOf());
        c->PSGetConstantBuffers(0, 1, psCb.GetAddressOf());
        c->PSGetShaderResources(0, 1, psSrv.GetAddressOf());
        c->PSGetSamplers(0, 1, psSampler.GetAddressOf());
        c->RSGetState(rs.GetAddressOf());
        c->RSGetViewports(&viewportCount, viewports);
        c->RSGetScissorRects(&scissorCount, scissors);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
        c->OMGetBlendState(blend.GetAddressOf(), blendFactor, &sampleMask);
        c->OMGetDepthStencilState(depth.GetAddressOf(), &stencilRef);
    }
    void restore(ID3D11DeviceContext* c) {
        ID3D11ShaderResourceView* noSrv = nullptr;
        c->PSSetShaderResources(0, 1, &noSrv);   // ours out before the game's targets go back
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
        c->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth.Get(), stencilRef);
        c->RSSetState(rs.Get());
        c->RSSetViewports(viewportCount, viewports);
        c->RSSetScissorRects(scissorCount, scissors);
        c->IASetInputLayout(layout.Get());
        c->IASetPrimitiveTopology(topology);
        c->VSSetShader(vs.Get(), nullptr, 0);
        c->HSSetShader(hs.Get(), nullptr, 0);
        c->DSSetShader(ds.Get(), nullptr, 0);
        c->GSSetShader(gs.Get(), nullptr, 0);
        c->PSSetShader(ps.Get(), nullptr, 0);
        c->VSSetConstantBuffers(0, 1, vsCb.GetAddressOf());
        c->PSSetConstantBuffers(0, 1, psCb.GetAddressOf());
        c->PSSetShaderResources(0, 1, psSrv.GetAddressOf());
        c->PSSetSamplers(0, 1, psSampler.GetAddressOf());
        for (auto*& r : rtvs) if (r) { r->Release(); r = nullptr; }
        if (dsv) { dsv->Release(); dsv = nullptr; }
    }
};

void pollTimers(ID3D11DeviceContext* ctx) {
    for (auto& t : g_r.timers) {
        if (!t.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(t.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 a = 0, b = 0;
        if (ctx->GetData(t.begin.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (ctx->GetData(t.end.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        t.pending = false;
        if (!dj.Disjoint && dj.Frequency && b >= a) g_r.window.noteGpu(double(b - a) * 1000.0 / double(dj.Frequency));
    }
}

Render::Timer* beginTimer(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    if (g_r.timersFailed) return nullptr;
    Render::Timer& t = g_r.timers[g_r.nextTimer];
    if (t.pending) return nullptr;   // not read back yet: this frame goes untimed
    if (!t.disjoint) {
        D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, s{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device->CreateQuery(&d, t.disjoint.GetAddressOf())) || FAILED(device->CreateQuery(&s, t.begin.GetAddressOf())) ||
            FAILED(device->CreateQuery(&s, t.end.GetAddressOf()))) {
            g_r.timersFailed = true;
            t = {};
            return nullptr;
        }
    }
    g_r.nextTimer = (g_r.nextTimer + 1) % 4;
    ctx->Begin(t.disjoint.Get());
    ctx->End(t.begin.Get());
    return &t;
}

// Draws the held eye into the back buffer; Failure::None when it did.
dv::Failure draw(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11Texture2D* eye, const float bounds[4], uint32_t eyeIndex,
                 const EdvrNativeDesktopViewPose& pose) {
    if (!sameDevice(device, eye)) return dv::Failure::Device;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return dv::Failure::Window;
    D3D11_TEXTURE2D_DESC bd{}, ed{};
    back->GetDesc(&bd);
    eye->GetDesc(&ed);
    g_r.window.windowW = bd.Width; g_r.window.windowH = bd.Height;
    uint32_t eyeW = 0, eyeH = 0;
    dv::eyeSize(ed.Width, ed.Height, bounds, eyeW, eyeH);
    g_r.window.eyeW = eyeW; g_r.window.eyeH = eyeH;
    const dv::ColorPlan plan = dv::colorPlan(ed.Format, bd.Format);
    g_r.window.path = plan.path;
    if (plan.path == dv::ColorPath::Unsupported || ed.SampleDesc.Count != 1 || ed.ArraySize != 1) return dv::Failure::Format;
    if (!(ed.BindFlags & D3D11_BIND_SHADER_RESOURCE) || !(bd.BindFlags & D3D11_BIND_RENDER_TARGET)) return dv::Failure::Bind;
    // desktop-eye-steady: the picture's angles (a frame without a pose keeps the last ones: unturned, the mapping does not depend on
    // them, and the window keeps its size).
    const uint32_t e = eyeIndex & 1;
    auto& pc = g_r.cache;
    double fov[4]{};
    const bool posed = pose.valid && dv::fovTangents(pose.fov, fov);
    bool hasFov = posed;
    if (!posed && pc.valid && pc.hasFov && pc.eye == e) { std::memcpy(fov, pc.fov, sizeof(fov)); hasFov = true; }
    bool fovMoved = hasFov != pc.hasFov;
    for (int i = 0; i < 4 && !fovMoved; ++i) fovMoved = std::fabs(fov[i] - pc.fov[i]) > 2e-3;
    if (!pc.valid || pc.fit != g_r.fit || pc.steady != g_r.steady || pc.eye != e || pc.maskRevision != g_r.maskRevision[e] ||
        pc.windowW != bd.Width || pc.windowH != bd.Height || pc.eyeW != eyeW || pc.eyeH != eyeH || (g_r.steady && fovMoved)) {
        const auto& m = g_r.mask[e];
        pc.place = dv::fitPlacement(g_r.fit, g_r.steady, bd.Width, bd.Height, eyeW, eyeH, m.empty() ? nullptr : m.data(),
                                    uint32_t(m.size() / 2), hasFov ? fov : nullptr, &pc.share);
        pc.valid = true; pc.fit = g_r.fit; pc.steady = g_r.steady; pc.eye = e; pc.maskRevision = g_r.maskRevision[e];
        pc.windowW = bd.Width; pc.windowH = bd.Height; pc.eyeW = eyeW; pc.eyeH = eyeH;
        pc.hasFov = hasFov; std::memcpy(pc.fov, fov, sizeof(fov));
    }
    const dv::Placement place = pc.place;
    g_r.window.zoomShare = pc.share;
    if (g_r.fit == dv::Fit::Zoom) g_r.window.maskTriangles = uint32_t(g_r.mask[e].size() / 6);
    if (!place.ok) return dv::Failure::Window;
    // The smoothed orientation: once per drawn frame (draw runs only for a newer sequence). Off, whole and a frame without a pose draw
    // the picture unturned; a frame without a pose also restarts the filter.
    const dv::Quat p{pose.orientation[0], pose.orientation[1], pose.orientation[2], pose.orientation[3]};
    dv::Quat s = p;
    bool turn = false;
    if (g_r.steady && g_r.fit != dv::Fit::Whole) {
        if (g_r.filterEye != e) { g_r.resetSteady(); g_r.filterEye = e; }
        if (posed) {
            LARGE_INTEGER now{}, freq{};
            QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
            double lag = 0;
            bool clamped = false;
            const double seconds = double(now.QuadPart) / double(freq.QuadPart);
            s = g_r.filter.update(p, seconds, &lag, &clamped);
            g_r.window.noteLag(lag * 180 / dv::kPi, clamped);
            g_r.window.noteSway(g_r.headSway.update(p, seconds), g_r.windowSway.update(s, seconds));
            turn = true;
        } else {
            g_r.resetSteady();
            ++g_r.window.unposed;
        }
    }
    const dv::Projection projection = dv::projection(place.src, hasFov ? fov : nullptr, turn, p, s);

    ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(ctx.GetAddressOf());
    if (!ctx || !makeObjects(device, ctx.Get())) return dv::Failure::Shaders;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = plan.srv;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = plan.rtv;
    if (bd.SampleDesc.Count > 1) rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    else { rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rd.Texture2D.MipSlice = 0; }
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateShaderResourceView(eye, &sd, srv.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(back.Get(), &rd, rtv.GetAddressOf())))
        return dv::Failure::Views;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(g_r.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return dv::Failure::Views;
    float data[28]{};
    std::memcpy(data, projection.m, sizeof(projection.m));
    std::memcpy(data + 12, projection.win, sizeof(projection.win));
    std::memcpy(data + 16, projection.frame, sizeof(projection.frame));
    std::memcpy(data + 20, bounds, 4 * sizeof(float));
    data[24] = float(int(plan.path));
    std::memcpy(mapped.pData, data, sizeof(data));
    ctx->Unmap(g_r.constants.Get(), 0);

    pollTimers(ctx.Get());
    SavedState saved;
    saved.capture(ctx.Get());
    Render::Timer* timer = beginTimer(device, ctx.Get());
    ID3D11RenderTargetView* target = rtv.Get();
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
    if (place.clear) {
        const float black[4] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(rtv.Get(), black);
    }
    const D3D11_VIEWPORT vp{place.dst[0], place.dst[1], place.dst[2], place.dst[3], 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_r.vs.Get(), nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(g_r.ps.Get(), nullptr, 0);
    ID3D11Buffer* cb = g_r.constants.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* view = srv.Get();
    ctx->PSSetShaderResources(0, 1, &view);
    ID3D11SamplerState* sampler = g_r.sampler.Get();
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->Draw(3, 0);
    if (timer) {
        ctx->End(timer->end.Get());
        ctx->End(timer->disjoint.Get());
        timer->pending = true;
    }
    saved.restore(ctx.Get());
    return dv::Failure::None;
}

void maybeLine(uint64_t now) {
    if (!g_r.windowStartMs) g_r.windowStartMs = now;
    if (now - g_r.windowStartMs < kLineMs) return;
    char line[640];
    dv::formatLine(line, sizeof(line), g_r.view, g_r.fit, g_everAcquired.load(), g_r.window, g_r.steady);
    Log::get().note("%s", line);
    g_r.window = {};
    g_r.windowStartMs = now;
}

// The table.
HRESULT WINAPI noteEye(void* context, uint64_t sequence, uint32_t eye, ID3D11Texture2D* texture, const float* bounds,
                       const EdvrNativeDesktopViewPose* pose) {
    if (context != &g_session) return E_INVALIDARG;
    const int want = dv::viewEye(dv::View(g_view.load(std::memory_order_acquire)));
    if (want < 0 || eye != uint32_t(want)) return S_FALSE;
    if (!texture || !bounds || !pose || !sequence || !dv::validBounds(bounds)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session.active) return E_INVALIDARG;
    g_held.texture = texture;
    std::memcpy(g_held.bounds, bounds, sizeof(g_held.bounds));
    g_held.pose = *pose;
    g_held.sequence = sequence;
    g_held.presentIndex = g_presentIndex;
    g_held.eye = eye;
    return S_OK;
}

HRESULT WINAPI noteMask(void* context, uint32_t eye, const float* xy, uint32_t vertexCount) {
    if (context != &g_session || eye > 1 || vertexCount % 3 || vertexCount > 65536 || (vertexCount && !xy)) return E_INVALIDARG;
    for (uint32_t i = 0; i < 2 * vertexCount; ++i)
        if (!std::isfinite(xy[i])) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session.active) return E_INVALIDARG;
    g_masks[eye].assign(xy, xy + 2 * size_t(vertexCount));
    ++g_maskRevision[eye];
    Log::get().note("desktop view: lens mask for eye %u, %u triangles (revision %llu).", eye, vertexCount / 3,
                    (unsigned long long)g_maskRevision[eye]);
    return S_OK;
}

HRESULT WINAPI closeTable(void* context) {
    if (context != &g_session) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session.active) return S_FALSE;
    g_session.active = false;
    g_held = {};
    Log::get().note("desktop view: the runtime closed its eye table (generation %llu).", (unsigned long long)g_session.generation);
    return S_OK;
}

}  // namespace

void desktopViewBeforePresent(IDXGISwapChain* swapChain, ID3D11Device* device, UINT flags) {
    if (!swapChain || !device || (flags & DXGI_PRESENT_TEST)) return;
    readKeys();
    Held held;
    uint64_t index = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        held = std::move(g_held);
        g_held = {};
        index = g_presentIndex++;
        if (held.texture) {
            const uint32_t e = held.eye & 1;
            if (g_maskRevision[e] != g_r.maskRevision[e]) { g_r.mask[e] = g_masks[e]; g_r.maskRevision[e] = g_maskRevision[e]; }
        }
    }
    ++g_r.window.presents;
    const uint64_t now = nowMs();
    if (g_r.view != dv::View::Off) {
        const dv::Verdict verdict = dv::decide(held.texture != nullptr, held.presentIndex, index, held.sequence, g_r.lastDrawnSequence);
        if (verdict == dv::Verdict::NoEye) ++g_r.window.noEye;
        else if (verdict == dv::Verdict::Stale) ++g_r.window.stale;
        else {
            dv::Failure failure = dv::Failure::Shaders;
            if (!guardedBudget(g_budget, [&] { failure = draw(swapChain, device, held.texture.Get(), held.bounds, held.eye, held.pose); }))
                failure = dv::Failure::Shaders;
            if (failure == dv::Failure::None) {
                ++g_r.window.drawn;
                g_r.lastDrawnSequence = held.sequence;
            } else {
                ++g_r.window.failed;
                g_r.window.lastFailure = failure;
            }
        }
    }
    held = {};
    maybeLine(now);
}

}  // namespace edvr

extern "C" HRESULT WINAPI edvrAcquireNativeDesktopView(const EdvrNativeDesktopViewRequest* r, EdvrNativeDesktopViewTable* t) {
    if (!t || t->size != sizeof(*t) || t->version != EDVR_NATIVE_DESKTOP_VIEW_VERSION_2) return E_INVALIDARG;
    *t = {sizeof(*t), EDVR_NATIVE_DESKTOP_VIEW_VERSION_2};
    if (!r || r->size != sizeof(*r) || r->version != EDVR_NATIVE_DESKTOP_VIEW_VERSION_2 || !r->gameDevice || !r->generation)
        return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(edvr::g_mutex);
    if (edvr::g_session.active) return E_PENDING;
    edvr::g_session = {};
    edvr::g_session.active = true;
    edvr::g_session.generation = r->generation;
    edvr::g_held = {};
    for (unsigned e = 0; e < 2; ++e) { edvr::g_masks[e].clear(); ++edvr::g_maskRevision[e]; }   // the new session sends its own
    edvr::g_everAcquired.store(true);
    t->context = &edvr::g_session;
    t->noteEye = edvr::noteEye;
    t->noteMask = edvr::noteMask;
    t->close = edvr::closeTable;
    edvr::Log::get().note("desktop view: the runtime acquired the eye table (generation %llu).", (unsigned long long)r->generation);
    return S_OK;
}
