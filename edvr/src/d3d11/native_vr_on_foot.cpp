// Native VR on foot: the two switches' glue and the frame boundary (native_vr_on_foot.h says what and why).
#include "native_vr_on_foot.h"
#include "native_vr_on_foot_core.h"
#include "journal_watch.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace {

// The cinema-on-foot switch (nvf::cinemaSwitchStep): its value, -1 if unreadable; and a compare-and-swap, so a value the game put
// there between the read and the write is never overwritten.
__declspec(noinline) int sehReadByte(const uint8_t* p) noexcept {
    __try {
        return *reinterpret_cast<const volatile uint8_t*>(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
__declspec(noinline) bool sehSwapByte(uint8_t* p, uint8_t from, uint8_t to) noexcept {
    __try {
        return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(p), static_cast<char>(to),
                                                                 static_cast<char>(from))) == from;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game HUD switch (nvf::hudSwitchStep): an aligned int, read and compare-and-swapped the same way.
__declspec(noinline) bool sehReadInt(const int32_t* p, int32_t* value) noexcept {
    __try {
        *value = *reinterpret_cast<const volatile int32_t*>(p);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
__declspec(noinline) bool sehSwapInt(int32_t* p, int32_t from, int32_t to) noexcept {
    __try {
        return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(p), to, from) == from;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The image's identity and sections from its own headers, then a switch's pattern in its executable sections (nvf::findPatternTarget).
// False if the headers could not be read or name more sections than fit; a fault while scanning is a not-found.
constexpr int kMaxSections = 32;
__declspec(noinline) bool sehFindSwitch(uintptr_t base, nvf::SwitchFind* found, uint32_t* timestamp, uint32_t* imageSize,
                                        const nvf::PatternShape& shape = nvf::kCinemaShape) noexcept {
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        *timestamp = nt->FileHeader.TimeDateStamp;
        *imageSize = nt->OptionalHeader.SizeOfImage;
        const int count = nt->FileHeader.NumberOfSections;
        if (count > kMaxSections) return false;
        nvf::ImageSection sections[kMaxSections];
        const IMAGE_SECTION_HEADER* h = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < count; ++i) {
            sections[i].rva = h[i].VirtualAddress;
            sections[i].size = h[i].Misc.VirtualSize;
            sections[i].execute = (h[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            sections[i].write = (h[i].Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            if (sections[i].rva + static_cast<uint64_t>(sections[i].size) > *imageSize) sections[i].size = 0;  // never past the image
        }
        *found = nvf::findPatternTarget(reinterpret_cast<const uint8_t*>(base), sections, count, shape);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *found = nvf::SwitchFind{};
        return true;
    }
}

void say(NativeVrOnFootSink sink, void* ctx, const char* fmt, ...) {
    char line[700];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sink(ctx, line);
}

void logSink(void*, const char* line) { Log::get().note("%s", line); }

}  // namespace

uint32_t nativeVrCinemaSwitchFind(uintptr_t base, NativeVrOnFootSink sink, void* ctx) {
    nvf::SwitchFind found;
    uint32_t timestamp = 0, imageSize = 0;
    const uint64_t startMs = GetTickCount64();
    char line[400];
    if (!sehFindSwitch(base, &found, &timestamp, &imageSize)) {
        say(sink, ctx, "native-vr-on-foot: cinema-on-foot switch not searched: the game's image headers could not be read; the game "
                       "keeps its cinema on foot");
        return 0;
    }
    if (nvf::formatFind(found, timestamp, imageSize, GetTickCount64() - startMs, line, sizeof(line)) > 0) sink(ctx, line);
    return found.result == nvf::kFindFound ? found.byteRva : 0;
}

void nativeVrCinemaSwitchStep(uint8_t* address, uint32_t rva, bool key, int onFoot, NativeVrOnFootSink sink, void* ctx) {
    static nvf::SwitchState state;
    static bool saidNoAddress = false;
    static int lastOnFoot = -1;
    char line[400];
    if (!address) {
        if (key && !saidNoAddress)
            sink(ctx, "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch was not found, see its line); nothing "
                      "written; the game keeps its cinema");
        saidNoAddress = key;
        return;
    }
    const int value = sehReadByte(address);
    nvf::SwitchStep step = nvf::cinemaSwitchStep(state, key, value);
    if (step.write >= 0 && !sehSwapByte(address, static_cast<uint8_t>(value), static_cast<uint8_t>(step.write))) {
        // The game changed it between the read and the write: next frame decides again on what it holds then.
        if (step.kind == nvf::kSwitchSet) state.ours = false;
        if (step.kind == nvf::kSwitchAgain) --state.again;
        return;
    }
    if (step.kind != nvf::kSwitchAgain || nvf::switchAgainSaid(state.again)) {
        if (nvf::formatSwitch(step, state, value, onFoot, rva, line, sizeof(line)) > 0) sink(ctx, line);
    }
    if (state.ours && onFoot >= 0 && onFoot != lastOnFoot && lastOnFoot >= 0) {
        nvf::formatHeadCameraJournal(onFoot, line, sizeof(line));
        sink(ctx, line);
    }
    lastOnFoot = onFoot;
}

uint32_t nativeVrHudSwitchFind(uintptr_t base, NativeVrOnFootSink sink, void* ctx) {
    nvf::SwitchFind found;
    uint32_t timestamp = 0, imageSize = 0;
    const uint64_t startMs = GetTickCount64();
    char line[400];
    if (!sehFindSwitch(base, &found, &timestamp, &imageSize, nvf::kHudShape)) {
        say(sink, ctx, "native-vr-on-foot: game HUD switch not searched: the game's image headers could not be read; on foot the game "
                       "turns its HUD off as before");
        return 0;
    }
    if (nvf::formatHudFind(found, GetTickCount64() - startMs, line, sizeof(line)) > 0) sink(ctx, line);
    if (found.result != nvf::kFindFound) return 0;
    // An int the compare-and-swap can take whole: the game's is aligned; one that is not is not the switch we know.
    if (found.byteRva % sizeof(int32_t) != 0) {
        say(sink, ctx, "native-vr-on-foot: game HUD switch at +0x%X is not 4-byte aligned; nothing will be written there", found.byteRva);
        return 0;
    }
    return found.byteRva;
}

void nativeVrHudSwitchStep(int32_t* address, uint32_t rva, bool key, int onFoot, NativeVrOnFootSink sink, void* ctx) {
    static nvf::SwitchState state;
    static bool saidNoAddress = false;
    char line[400];
    if (!address) {
        if (key && !saidNoAddress)
            sink(ctx, "native-vr-on-foot: game HUD cannot work (the HUD switch was not found, see its line); nothing written; on foot "
                      "the game turns its HUD off as before");
        saidNoAddress = key;
        return;
    }
    int32_t value = 0;
    const bool readOk = sehReadInt(address, &value);
    const nvf::ValueStep step = nvf::hudSwitchStep(state, key, readOk, value);
    if (step.write && !sehSwapInt(address, value, step.value)) {
        // The game changed it between the read and the write: next frame decides again on what it holds then.
        if (step.kind == nvf::kSwitchSet) state.ours = false;
        if (step.kind == nvf::kSwitchAgain) --state.again;
        return;
    }
    if (step.kind != nvf::kSwitchAgain || nvf::switchAgainSaid(state.again)) {
        if (nvf::formatHudSwitch(step, state, readOk, value, onFoot, rva, line, sizeof(line)) > 0) sink(ctx, line);
    }
}

void nativeVrOnFootFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool tried = false;
    static bool onFootKey = false, saidOnFoot = false;
    static uint32_t switchRva = 0;  // the cinema-on-foot switch, found once (0: not found)
    static uint32_t hudRva = 0;     // the game HUD switch, likewise
    const Config& cfg = Config::get();
    const uint32_t now = cfg.generation();
    if (now != generation) {
        generation = now;
        // Native VR on foot is the default: a missing key means on.
        const bool wantOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);
        if (!saidOnFoot || wantOnFoot != onFootKey) {
            char line[200];
            nvf::formatOnFootKey(wantOnFoot, saidOnFoot, line, sizeof(line));
            Log::get().note("%s", line);
            saidOnFoot = true;
        }
        onFootKey = wantOnFoot;
    }
    if (onFootKey && !tried) {
        tried = true;
        // Both switches are data, found by their readers' code: any build where that code is the one match.
        switchRva = nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), &logSink, nullptr);
        hudRva = nativeVrHudSwitchFind(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), &logSink, nullptr);
    }
    if (!tried) return;
    const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
    nativeVrCinemaSwitchStep(switchRva ? reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + switchRva : nullptr, switchRva,
                             onFootKey, onFoot, &logSink, nullptr);
    nativeVrHudSwitchStep(hudRva ? reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + hudRva) : nullptr,
                          hudRva, onFootKey, onFoot, &logSink, nullptr);
}

}  // namespace edvr
