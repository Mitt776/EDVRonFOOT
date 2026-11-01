#include "aim_dot_probe.h"
#include "aim_dot_probe_core.h"
#include "aim_dot_core.h"
#include "on_foot_hud_core.h"
#include "binding_shadow.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"
#include <windows.h>
#include <d3d11.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace edvr {
namespace detail {
std::atomic<bool> g_aimDotWants{false};
}

namespace {
FaultBudget g_budget("aimDot", 8);

// The draw thread's state is one thread's at a time: the draw calls and the boundary's release take this try-flag (a second thread in
// the window skips its turn, as .22's layout probe did). The boundary's own state and the counters are atomics.
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
struct Busy {
    bool held = !g_busy.test_and_set(std::memory_order_acquire);
    ~Busy() {
        if (held) g_busy.clear(std::memory_order_release);
    }
};

std::atomic<int> g_mode{int(adp::Mode::Off)};
std::atomic<bool> g_inert{false};
std::atomic<bool> g_holding{false};      // the draw thread holds a texture: the boundary gives it back when the key goes off
std::atomic<uint64_t> g_seq{1};          // the frame's number, from the boundary
std::atomic<uint64_t> g_armedAt{0};      // the mark's tick (census), 0 for none
std::atomic<uint32_t> g_atlasW{0}, g_atlasH{0};
// The draw thread counts, the boundary takes them every 30 s.
std::atomic<uint64_t> g_cAtlasFrames{0}, g_cEyeQuads{0}, g_cSnapshots{0}, g_cFailures{0};

void say(const char* line) { Log::get().note("%s", line); }

// A snapshot from its copy to its lines: the frame it was taken in, then (finalized, at the next frame) that frame's eye quads and the
// whole list of its interface draws into the atlas, then the read of the staging copy.
struct Snapshot {
    bool active = false, finalized = false, copied = false;
    int n = 0;
    uint32_t w = 0, h = 0;
    adp::Cell cell;
    uint32_t quads[2] = {}, targets = 0;
    int draws = 0;
    std::vector<adp::GuiDraw> list;
    int steps = 0;              // the centre's copies in `centre`, before each draw and before the eye
    std::string centreLine;     // made at the read
};

struct State {
    const void* atlasId = nullptr;  // identity only, as the HUD's census
    ID3D11Texture2D* atlas = nullptr;  // a reference, while the probe works
    uint32_t w = 0, h = 0, fmt = 0;
    adp::Cell cell;
    uint32_t saidW = 0, saidH = 0;
    ID3D11Texture2D* staging = nullptr;
    uint32_t sw = 0, sh = 0, sfmt = 0;
    ID3D11Texture2D* centre = nullptr;  // kCentreSteps slices of the aim dot's centre (staging)
    uint32_t cw = 0, ch = 0, cfmt = 0;
    int steps = 0;                      // this frame's copies into `centre`
    // This frame's.
    uint64_t frame = 0;
    bool atlasDrawn = false, recording = false;
    const void* eyeTargets[2] = {};
    uint32_t quads[2] = {}, targets = 0;
    std::vector<adp::GuiDraw> list;
    int listTotal = 0;
    // The launch's.
    Snapshot snap;
    int taken = 0;
    bool limitSaid = false;

    void releaseAll() {
        for (ID3D11Texture2D** t : {&atlas, &staging, &centre})
            if (*t) {
                (*t)->Release();
                *t = nullptr;
            }
        atlasId = nullptr;
        w = h = fmt = 0;
        sw = sh = sfmt = 0;
        cw = ch = cfmt = 0;
        snap = Snapshot{};
        g_holding.store(false, std::memory_order_relaxed);
    }
};
State g_s;

// One line, the probe stands until the key changes; everything it holds is given back.
void standDown(const char* why) {
    char line[400];
    adp::formatInert(why, line, sizeof(line));
    say(line);
    g_inert.store(true, std::memory_order_relaxed);
    detail::g_aimDotWants.store(false, std::memory_order_relaxed);
    g_cFailures.fetch_add(1, std::memory_order_relaxed);
    g_s.releaseAll();
}

// The snapshot's lines: the head, then its draws kDrawsPerLine a line.
void saySnapshot(const Snapshot& s, const char* files) {
    char line[1200];
    adp::formatSnapshot(s.n, s.w, s.h, s.quads[0], s.quads[1], s.targets, s.draws, files, line, sizeof(line));
    say(line);
    const int listed = int(s.list.size());
    for (int from = 0; from < listed; from += adp::kDrawsPerLine) {
        const int to = from + adp::kDrawsPerLine < listed ? from + adp::kDrawsPerLine : listed;
        adp::formatDraws(s.n, s.list.data(), from, to, line, sizeof(line));
        say(line);
    }
    if (!s.centreLine.empty()) say(s.centreLine.c_str());
}

// <logDir>\aim_dot\<the launch's local time>, made on the first file.
std::mutex g_dirMutex;
bool snapshotDirectory(std::wstring* out) {
    static bool done = false, ok = false;
    static std::wstring dir;
    std::lock_guard<std::mutex> lock(g_dirMutex);
    if (!done) {
        done = true;
        SYSTEMTIME st{};
        FILETIME created{}, exited{}, kernel{}, user{}, local{};
        if (!(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) && FileTimeToLocalFileTime(&created, &local) &&
              FileTimeToSystemTime(&local, &st)))
            GetLocalTime(&st);
        wchar_t stamp[40];
        _snwprintf_s(stamp, _TRUNCATE, L"%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        const std::wstring root = Config::get().logDir() + L"\\aim_dot";
        dir = root + L"\\" + stamp;
        ok = ensureDirectory(Config::get().logDir()) && ensureDirectory(root) && ensureDirectory(dir);
    }
    *out = dir;
    return ok;
}
bool writeFile(const std::wstring& dir, const char* name, const std::vector<uint8_t>& data) {
    const std::wstring path = dir + L"\\" + std::wstring(name, name + std::strlen(name));
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const size_t wrote = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return wrote == data.size();
}

// The files and the lines, off the draw thread: a 26 MB atlas is not written while the game waits for its frame.
void writeSnapshot(std::shared_ptr<Snapshot> s, std::shared_ptr<std::vector<uint8_t>> pixels) {
    std::thread([s, pixels] {
        char atlasName[64], cellName[64], files[700];
        adp::atlasFileName(s->n, s->w, s->h, atlasName, sizeof(atlasName));
        adp::cellFileName(s->n, cellName, sizeof(cellName));
        std::wstring dir;
        bool ok = snapshotDirectory(&dir);
        if (ok) {
            std::vector<uint8_t> bmp(adp::bmpBytes(s->w, s->h));
            adp::makeBmp32(pixels->data(), s->w, s->h, bmp.data());
            ok = writeFile(dir, atlasName, bmp);
            const uint32_t cw = s->cell.w() * adp::kCellZoom, ch = s->cell.h() * adp::kCellZoom;
            std::vector<uint8_t> zoomed(size_t(cw) * ch * 4);
            adp::zoomCell(pixels->data(), s->w, s->cell, adp::kCellZoom, zoomed.data());
            bmp.assign(adp::bmpBytes(cw, ch), 0);
            adp::makeBmp32(zoomed.data(), cw, ch, bmp.data());
            ok = writeFile(dir, cellName, bmp) && ok;
        }
        if (ok) {
            char d[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, d, int(sizeof(d)) - 1, nullptr, nullptr);
            std::snprintf(files, sizeof(files), "%s, %s in %s", atlasName, cellName, d);
        } else {
            std::snprintf(files, sizeof(files), "none (the folder or a file could not be written)");
            g_cFailures.fetch_add(1, std::memory_order_relaxed);
        }
        saySnapshot(*s, files);
    }).detach();
}

// A new frame met by a draw: the frame that ended finalizes a snapshot taken in it; this one starts clean, recording the interface
// draws when a mark waits (census).
void stepFrame() {
    const uint64_t seq = g_seq.load(std::memory_order_relaxed);
    if (seq == g_s.frame) return;
    if (g_s.snap.active && !g_s.snap.finalized) {
        g_s.snap.quads[0] = g_s.quads[0];
        g_s.snap.quads[1] = g_s.quads[1];
        g_s.snap.targets = g_s.targets;
        g_s.snap.draws = g_s.listTotal;
        g_s.snap.list = std::move(g_s.list);
        g_s.snap.finalized = true;
    }
    g_s.frame = seq;
    g_s.atlasDrawn = false;
    g_s.eyeTargets[0] = g_s.eyeTargets[1] = nullptr;
    g_s.quads[0] = g_s.quads[1] = 0;
    g_s.targets = 0;
    g_s.list.clear();
    g_s.listTotal = 0;
    g_s.steps = 0;
    g_s.recording = adp::Mode(g_mode.load(std::memory_order_relaxed)) == adp::Mode::Census &&
                    g_armedAt.load(std::memory_order_relaxed) != 0 && !g_s.snap.active;
}

// A texture of the centre's size and the atlas's format, kCentreSteps slices to read (census). Remade on a new size.
bool centreTexture(uint32_t w, uint32_t h) {
    if (g_s.centre && g_s.cw == w && g_s.ch == h && g_s.cfmt == g_s.fmt) return true;
    if (g_s.centre) g_s.centre->Release();
    g_s.centre = nullptr;
    ID3D11Device* device = nullptr;
    g_s.atlas->GetDevice(&device);
    if (!device) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = adp::kCentreSteps;
    d.Format = DXGI_FORMAT(g_s.fmt);
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const HRESULT hr = device->CreateTexture2D(&d, nullptr, &g_s.centre);
    device->Release();
    if (FAILED(hr) || !g_s.centre) {
        g_s.centre = nullptr;
        return false;
    }
    g_s.cw = w;
    g_s.ch = h;
    g_s.cfmt = g_s.fmt;
    return true;
}

// The centre as it stands now, into the next slice (census, a snapshot's frame).
void copyCentreStep(ID3D11DeviceContext* ctx) {
    if (g_s.steps >= adp::kCentreSteps || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    const adh::Rect r = adh::centreOf(g_s.w, g_s.h);
    if (!centreTexture(r.w(), r.h())) return;  // the steps are extra: without them the snapshot goes on
    D3D11_BOX box{r.x0, r.y0, 0, r.x1, r.y1, 1};
    ctx->CopySubresourceRegion(g_s.centre, UINT(g_s.steps), 0, 0, 0, g_s.atlas, 0, &box);
    ++g_s.steps;
}

// The snapshot's centre steps read (they were copied before the atlas, so they are ready when it is): its line. False: not yet.
bool readCentreSteps(ID3D11DeviceContext* ctx, Snapshot& s) {
    std::vector<std::vector<uint8_t>> px(size_t(s.steps));
    for (int i = 0; i < s.steps; ++i) {
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT hr = ctx->Map(g_s.centre, UINT(i), D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
        if (FAILED(hr)) {
            s.centreLine = "aim dot probe: snapshot centre steps could not be read";
            return true;
        }
        px[i].resize(size_t(g_s.cw) * g_s.ch * 4);
        for (uint32_t y = 0; y < g_s.ch; ++y)
            std::memcpy(px[i].data() + size_t(y) * g_s.cw * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                        size_t(g_s.cw) * 4);
        ctx->Unmap(g_s.centre, UINT(i));
    }
    std::vector<adp::CentreStep> step(size_t(s.steps));
    for (int i = 0; i < s.steps; ++i) step[i] = adp::centreStep(px[i].data(), g_s.cw * 4, g_s.cw, g_s.ch);
    // Step i is the centre before draw i (the last: before the eye); a draw changed it when its after differs from its before.
    const int draws = s.steps - 1 < int(s.list.size()) ? s.steps - 1 : int(s.list.size());
    std::vector<char> changed(size_t(draws > 0 ? draws : 1), 0);
    for (int i = 0; i < draws; ++i) changed[i] = px[i] != px[i + 1];
    bool flagArr[adp::kCentreSteps] = {};
    for (int i = 0; i < draws; ++i) flagArr[i] = changed[i] != 0;
    char line[1500];
    adp::formatCentreSteps(s.n, step.data(), flagArr, s.list.data(), draws, line, sizeof(line));
    s.centreLine = line;
    return true;
}

// A finalized snapshot: its lines without files, or its staging copy read without waiting (a busy copy is tried at the next draw).
void pollSnapshot(ID3D11DeviceContext* ctx) {
    Snapshot& s = g_s.snap;
    if (!s.active || !s.finalized) return;
    if (!s.copied) {
        saySnapshot(s, "none (the limit)");
        s = Snapshot{};
        return;
    }
    if (ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;  // read on the context the copy was made on
    if (s.steps > 1 && g_s.centre && s.centreLine.empty() && !readCentreSteps(ctx, s)) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = ctx->Map(g_s.staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    if (FAILED(hr)) {
        standDown("the atlas's snapshot could not be read");
        return;
    }
    auto pixels = std::make_shared<std::vector<uint8_t>>(size_t(s.w) * s.h * 4);
    for (uint32_t y = 0; y < s.h; ++y)
        std::memcpy(pixels->data() + size_t(y) * s.w * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch, size_t(s.w) * 4);
    ctx->Unmap(g_s.staging, 0);
    writeSnapshot(std::make_shared<Snapshot>(std::move(s)), pixels);
    s = Snapshot{};
}

// The atlas behind the bound target, with a reference: its format checked, its cell, a line when found or resized.
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
    if (!adp::atlasFormatOk(uint32_t(d.Format)) || d.SampleDesc.Count != 1) {
        tex->Release();
        char why[200];
        std::snprintf(why, sizeof(why), "the %ux%u atlas's format is %u (%u samples), not R8G8B8A8 with one", d.Width, d.Height,
                      unsigned(d.Format), d.SampleDesc.Count);
        standDown(why);
        return;
    }
    if (g_s.atlas) g_s.atlas->Release();
    if (g_s.snap.active && g_s.snap.copied && (d.Width != g_s.snap.w || d.Height != g_s.snap.h))
        g_s.snap = Snapshot{};  // a copy of the old atlas's size: the staging texture is remade, the snapshot goes with it
    g_s.atlas = tex;
    g_s.atlasId = id;
    g_holding.store(true, std::memory_order_relaxed);
    g_s.w = d.Width;
    g_s.h = d.Height;
    g_s.fmt = uint32_t(d.Format);
    g_s.cell = adp::cellOf(d.Width, d.Height);
    g_atlasW.store(d.Width, std::memory_order_relaxed);
    g_atlasH.store(d.Height, std::memory_order_relaxed);
    char line[300];
    if (!g_s.saidW) {
        adp::formatAtlasFound(d.Width, d.Height, g_s.fmt, g_s.cell, line, sizeof(line));
        say(line);
    } else if (g_s.saidW != d.Width || g_s.saidH != d.Height) {
        adp::formatAtlasSize(g_s.saidW, g_s.saidH, d.Width, d.Height, g_s.cell, line, sizeof(line));
        say(line);
    }
    g_s.saidW = d.Width;
    g_s.saidH = d.Height;
}

// A staging texture of the atlas's size and format, to read it (census). Remade on a new size.
ID3D11Texture2D* ownTexture(ID3D11Texture2D** slot, uint32_t* sw, uint32_t* sh, uint32_t* sf, uint32_t w, uint32_t h) {
    if (*slot && *sw == w && *sh == h && *sf == g_s.fmt) return *slot;
    if (*slot) (*slot)->Release();
    *slot = nullptr;
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
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const HRESULT hr = device->CreateTexture2D(&d, nullptr, slot);
    device->Release();
    if (FAILED(hr) || !*slot) {
        *slot = nullptr;
        return nullptr;
    }
    *sw = w;
    *sh = h;
    *sf = g_s.fmt;
    return *slot;
}

adp::Gui guiOf(uint64_t vs) {
    return vs == ofh::kGuiVector ? adp::Gui::Vector : vs == ofh::kGuiText ? adp::Gui::Text : vs == ofh::kGuiIcons ? adp::Gui::Icons
                                                                                                                  : adp::Gui::Other;
}
}  // namespace

void aimDotNoteGuiDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint32_t count, const void* id, uint32_t w, uint32_t h, uint32_t) {
    if (!ctx || !id || !adp::isAtlasShape(w, h)) return;  // every other panel: no lock, no call
    Busy busy;
    if (!busy.held) return;
    guardedBudget(g_budget, [&] {
        if (g_inert.load(std::memory_order_relaxed)) return;
        stepFrame();
        if (id != g_s.atlasId) adopt(ctx, id);
        if (!g_s.atlas || id != g_s.atlasId) return;
        if (!g_s.atlasDrawn) {
            g_s.atlasDrawn = true;
            g_cAtlasFrames.fetch_add(1, std::memory_order_relaxed);
        }
        if (!g_s.recording) return;
        copyCentreStep(ctx);  // the centre before this draw
        ++g_s.listTotal;
        if (int(g_s.list.size()) >= adp::kMaxGuiDraws) return;
        adp::GuiDraw g;
        g.kind = guiOf(vs);
        g.count = count;
        D3D11_VIEWPORT vp{};
        UINT n = 1;
        ctx->RSGetViewports(&n, &vp);
        if (n) {
            g.vpX = int(vp.TopLeftX);
            g.vpY = int(vp.TopLeftY);
            g.vpW = int(vp.Width);
            g.vpH = int(vp.Height);
        }
        D3D11_RECT rc{};
        n = 1;
        ctx->RSGetScissorRects(&n, &rc);
        if (n) {
            g.hasScissor = true;
            g.scL = int(rc.left);
            g.scT = int(rc.top);
            g.scR = int(rc.right);
            g.scB = int(rc.bottom);
        }
        g_s.list.push_back(g);
    });
}

void aimDotBeforeEyeComposite(ID3D11DeviceContext* ctx, const void* const read[3]) {
    if (!ctx || !read) return;
    Busy busy;
    if (!busy.held) return;
    guardedBudget(g_budget, [&] {
        if (g_inert.load(std::memory_order_relaxed)) return;
        stepFrame();
        pollSnapshot(ctx);
        if (!g_s.atlas) return;
        if (read[0] != g_s.atlasId && read[1] != g_s.atlasId && read[2] != g_s.atlasId) return;
        // An eye quad out of the atlas: counted by its target, in the order the frame meets them (two at most).
        void* view = bindingGet(BindSlot::Rtv0);
        const void* target = view;
        ResourceInfo info;
        if (view && bindingResolve(view, &info) && info.resource) target = info.resource;
        int eye = -1;
        for (uint32_t i = 0; i < g_s.targets; ++i)
            if (g_s.eyeTargets[i] == target) eye = int(i);
        if (eye < 0 && g_s.targets < 2) {
            eye = int(g_s.targets);
            g_s.eyeTargets[g_s.targets++] = target;
        }
        if (eye < 0) eye = 1;  // a third target: counted with the second
        ++g_s.quads[eye];
        g_cEyeQuads.fetch_add(1, std::memory_order_relaxed);

        const adp::Mode mode = adp::Mode(g_mode.load(std::memory_order_relaxed));
        if (mode == adp::Mode::Census && g_s.recording && g_s.atlasDrawn && !g_s.snap.active &&
            g_armedAt.load(std::memory_order_relaxed) != 0) {
            // The snapshot: the atlas as the eyes read it, the frame's draws finalized at the next one.
            if (ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
                standDown("the atlas is read on a deferred context; the snapshot needs the immediate one");
                return;
            }
            Snapshot& s = g_s.snap;
            s = Snapshot{};
            s.active = true;
            s.n = ++g_s.taken;
            s.w = g_s.w;
            s.h = g_s.h;
            s.cell = g_s.cell;
            copyCentreStep(ctx);  // the centre before the eye reads it
            s.steps = g_s.steps;
            if (adp::snapshotFilesAllowed(s.n - 1)) {
                if (!ownTexture(&g_s.staging, &g_s.sw, &g_s.sh, &g_s.sfmt, g_s.w, g_s.h)) {
                    standDown("a texture to read the atlas could not be made");
                    return;
                }
                ctx->CopyResource(g_s.staging, g_s.atlas);
                s.copied = true;
            } else if (!g_s.limitSaid) {
                g_s.limitSaid = true;
                char line[200];
                adp::formatLimit(line, sizeof(line));
                say(line);
            }
            g_armedAt.store(0, std::memory_order_relaxed);
            g_cSnapshots.fetch_add(1, std::memory_order_relaxed);
        }
    });
}

void aimDotFrameBoundary(bool hudCensus, bool onFoot) {
    static uint32_t generation = ~0u;
    static bool saidKey = false, saidWaiting = false;
    static adp::Mode lastMode = adp::Mode::Off;
    static uint64_t windowStart = 0;
    static adp::Summary summary;
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const std::string text = cfg.getString("experimental.native_vr_on_foot_dot", "off");
        bool valid = true;
        const adp::Mode mode = adp::parseMode(text.c_str(), &valid);
        if (!saidKey || mode != lastMode || !valid) {
            char line[400];
            adp::formatKey(mode, valid, text.c_str(), line, sizeof(line));
            say(line);
            saidKey = true;
        }
        if (mode != lastMode) {  // a new value: an inert probe tries again, a waiting mark is dropped
            g_inert.store(false, std::memory_order_relaxed);
            g_armedAt.store(0, std::memory_order_relaxed);
            saidWaiting = false;
        }
        lastMode = mode;
        g_mode.store(int(mode), std::memory_order_relaxed);
    }
    const adp::Mode mode = lastMode;
    const bool inert = g_inert.load(std::memory_order_relaxed);
    detail::g_aimDotWants.store(mode != adp::Mode::Off && !inert, std::memory_order_relaxed);
    if (mode != adp::Mode::Off && !hudCensus && !saidWaiting) {
        char line[300];
        adp::formatWaitingForHud(line, sizeof(line));
        say(line);
        saidWaiting = true;
    }
    if (hudCensus) saidWaiting = false;
    g_seq.fetch_add(1, std::memory_order_relaxed);

    // Off (or inert, or off foot with no snapshot under way): the atlas and EDVR's own textures are given back.
    if (g_holding.load(std::memory_order_relaxed) && (mode == adp::Mode::Off || inert || !onFoot)) {
        Busy busy;
        if (busy.held && (mode == adp::Mode::Off || inert || !g_s.snap.active)) g_s.releaseAll();
    }

    const uint64_t now = GetTickCount64();
    uint64_t armed = g_armedAt.load(std::memory_order_relaxed);
    if (armed && now - armed > adp::kSnapshotWaitMs && g_armedAt.compare_exchange_strong(armed, 0)) {
        char line[200];
        adp::formatSkipped(line, sizeof(line));
        say(line);
    }

    if (!windowStart) windowStart = now;
    if (mode != adp::Mode::Off && onFoot) ++summary.frames;
    if (now - windowStart >= adp::kSummaryMs) {
        summary.atlasFrames = g_cAtlasFrames.exchange(0, std::memory_order_relaxed);
        summary.eyeQuads = g_cEyeQuads.exchange(0, std::memory_order_relaxed);
        summary.snapshots = g_cSnapshots.exchange(0, std::memory_order_relaxed);
        summary.failures = g_cFailures.exchange(0, std::memory_order_relaxed);
        if (mode != adp::Mode::Off && summary.frames) {
            char line[600];
            adp::formatSummary(mode, g_atlasW.load(std::memory_order_relaxed), g_atlasH.load(std::memory_order_relaxed), summary, line,
                               sizeof(line));
            say(line);
        }
        summary.clear();
        windowStart = now;
    }
}

void aimDotMark() {
    if (adp::Mode(g_mode.load(std::memory_order_relaxed)) != adp::Mode::Census || g_inert.load(std::memory_order_relaxed)) return;
    const uint64_t now = GetTickCount64();
    g_armedAt.store(now ? now : 1, std::memory_order_relaxed);
}

}  // namespace edvr
