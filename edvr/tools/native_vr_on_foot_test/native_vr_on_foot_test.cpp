// Build gate for EDVRonFOOT native VR on foot (src\d3d11\native_vr_on_foot.h, experimental.native_vr_on_foot): the game's two
// switches, each found by the code that reads it.
//
// Part F: the cinema-on-foot switch -- only 1 -> 0 with the key on and our 0 -> 1 with it off, nothing over any other value, a 0 that
// was not ours never restored, the game putting 1 back counted and said at 1, 10, 100, ... -- its lines, and the glue on a byte of the
// rig's own.
// Part G: the cinema switch's reader found by its code on a synthetic image (one match, many, outside, not writable, other registers),
// the find lines, and the glue on a synthetic PE in memory for both switches (unreadable headers fault into a not-found).
// Part H: the game HUD switch -- its reader on build 334473's own bytes, the search, its step (only -1 -> 1 and back) and its lines.
// Part I: the main menu after gameplay (nvf::mainMenuStep) -- both switches back to the game in the menu and ours again at LoadGame,
// one line each and no key lines, a value not ours left alone, nothing at all with the key off.
#include "../../src/d3d11/native_vr_on_foot.h"
#include "../../src/d3d11/native_vr_on_foot_core.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace edvr {
void breadcrumb(const char*) {}  // production guard.cpp's crash-channel dependency (proxy.cpp), as the other rigs stub it
bool journalOnFootKnown() { return false; }  // journal_watch.cpp's, for the frame boundary the rig never calls
bool journalOnFoot() { return false; }
bool journalMainMenu() { return false; }
}

using namespace edvr;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<std::string> g_lines;
void sink(void*, const char* line) { g_lines.emplace_back(line); }
size_t countLines(const char* needle) {
    size_t n = 0;
    for (const auto& l : g_lines) n += l.find(needle) != std::string::npos ? 1 : 0;
    return n;
}
bool anyLine(const char* needle) { return countLines(needle) > 0; }

void partF() {
    {
        nvf::SwitchState s;
        nvf::SwitchStep r = nvf::cinemaSwitchStep(s, false, 1);
        check(r.write < 0 && r.kind == nvf::kSwitchNone && !s.ours, "switch: key off, the game's 1 is left alone");
        r = nvf::cinemaSwitchStep(s, true, 1);
        check(r.write == 0 && r.kind == nvf::kSwitchSet && s.ours, "switch: key on, 1 becomes 0 and is ours");
        r = nvf::cinemaSwitchStep(s, true, 0);
        check(r.write < 0 && r.kind == nvf::kSwitchNone, "switch: our 0 in place, nothing to do");
        r = nvf::cinemaSwitchStep(s, true, 1);
        check(r.write == 0 && r.kind == nvf::kSwitchAgain && s.again == 1, "switch: the game puts 1 back: 0 again, counted");
        r = nvf::cinemaSwitchStep(s, false, 0);
        check(r.write == 1 && r.kind == nvf::kSwitchRestored && !s.ours, "switch: key off, our 0 goes back to the game's 1");
        r = nvf::cinemaSwitchStep(s, false, 1);
        check(r.write < 0 && r.kind == nvf::kSwitchNone, "switch: key off and restored, nothing more");

        nvf::SwitchState g;
        r = nvf::cinemaSwitchStep(g, true, 0);
        check(r.write < 0 && r.kind == nvf::kSwitchGameOff && !g.ours, "switch: a 0 before us is the game's own, not ours");
        r = nvf::cinemaSwitchStep(g, true, 0);
        check(r.kind == nvf::kSwitchNone, "switch: and said once");
        r = nvf::cinemaSwitchStep(g, false, 0);
        check(r.write < 0 && r.kind == nvf::kSwitchNone, "switch: key off never writes over a 0 that was not ours");

        nvf::SwitchState x;
        r = nvf::cinemaSwitchStep(x, true, 7);
        check(r.write < 0 && r.kind == nvf::kSwitchStrange, "switch: a value neither 1 nor 0 is refused, unwritten");
        r = nvf::cinemaSwitchStep(x, true, 1);
        check(r.write < 0 && r.kind == nvf::kSwitchNone, "switch: refused stays refused while the key is on");
        r = nvf::cinemaSwitchStep(x, false, 1);
        r = nvf::cinemaSwitchStep(x, true, 1);
        check(r.write == 0 && r.kind == nvf::kSwitchSet, "switch: key off and on again tries afresh");
        r = nvf::cinemaSwitchStep(x, true, -1);
        check(r.write < 0 && r.kind == nvf::kSwitchStrange && !x.ours, "switch: unreadable while ours: refused, no longer ours");
        r = nvf::cinemaSwitchStep(x, false, 0);
        check(r.write < 0 && r.kind == nvf::kSwitchNone, "switch: and key off restores nothing then");

        nvf::SwitchState w;
        r = nvf::cinemaSwitchStep(w, true, 0);
        r = nvf::cinemaSwitchStep(w, true, 1);
        check(r.write == 0 && r.kind == nvf::kSwitchSet, "switch: the game's own 0 turned 1 later is taken");

        nvf::SwitchState t;
        r = nvf::cinemaSwitchStep(t, false, 0);
        check(r.kind == nvf::kSwitchNone, "switch: key off from the start does nothing");
        bool onlyOneOrZero = true;
        for (int key = 0; key < 2; ++key)
            for (int v = -1; v < 8; ++v) {
                nvf::SwitchState a, b;
                b.ours = true;
                const nvf::SwitchStep ra = nvf::cinemaSwitchStep(a, key != 0, v);
                const nvf::SwitchStep rb = nvf::cinemaSwitchStep(b, key != 0, v);
                for (const nvf::SwitchStep& q : {ra, rb})
                    if (q.write >= 0 && !((q.write == 0 && v == 1) || (q.write == 1 && v == 0))) onlyOneOrZero = false;
            }
        check(onlyOneOrZero, "switch: in no case is anything written but 1 -> 0 or 0 -> 1");
        check(nvf::switchAgainSaid(1) && nvf::switchAgainSaid(10) && nvf::switchAgainSaid(1000) && !nvf::switchAgainSaid(2) &&
                  !nvf::switchAgainSaid(11) && !nvf::switchAgainSaid(0),
              "switch: a repeat is said at 1, 10, 100, ...");
    }
    {
        char line[400];
        nvf::SwitchState s;
        nvf::SwitchStep r = nvf::cinemaSwitchStep(s, true, 1);
        nvf::formatSwitch(r, s, 1, 0, 0x5E85B8Au, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: head camera on: the game's cinema-on-foot switch (EliteDangerous64.exe+0x5E85B8A) 1 -> 0") &&
                  std::strstr(line, "on foot (journal): no"),
              "text: the switch's on line, by the RVA it was found at");
        nvf::SwitchStep strange;
        strange.kind = nvf::kSwitchStrange;
        nvf::formatSwitch(strange, s, 7, 0, 0x5E85B8Au, line, sizeof(line));
        check(std::strstr(line, "head camera cannot work (the cinema-on-foot switch holds 7") && std::strstr(line, "the game keeps its cinema on foot"),
              "text: the refusal says the value and that the game keeps its cinema");
        nvf::formatHeadCameraJournal(1, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: head camera: on foot (journal)") != nullptr, "text: the head camera's on-foot line");
    }

    // The glue on a byte of the rig's own, as the frame thread drives it.
    uint8_t game = 1;
    g_lines.clear();
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, false, 0, &sink, nullptr);
    check(game == 1 && g_lines.empty(), "glue switch: key off, the byte and the log are untouched");
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, true, 0, &sink, nullptr);
    check(game == 0 && countLines("head camera on:") == 1, "glue switch: key on, the byte is 0, one line");
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, true, 0, &sink, nullptr);
    check(game == 0 && g_lines.size() == 1, "glue switch: next frame, nothing more");
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, true, 1, &sink, nullptr);
    check(game == 0 && countLines("head camera: on foot (journal)") == 1, "glue switch: on foot by the journal, one line");
    for (int i = 0; i < 1000; ++i) {
        game = 1;
        nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, true, 1, &sink, nullptr);
    }
    check(game == 0 && countLines("back to 1; 0 again") == 4 && anyLine("(1000 time(s))"),
          "glue switch: the game puts 1 back a thousand times: 0 each time, said at 1, 10, 100, 1000");
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, true, 0, &sink, nullptr);
    check(countLines("back in the ship (journal)") == 1 && game == 0, "glue switch: back in the ship, said, the byte stays 0");
    nativeVrCinemaSwitchStep(&game, 0x5E85B8Au, false, 0, &sink, nullptr);
    check(game == 1 && countLines("head camera off: the cinema-on-foot switch is the game's 1 again") == 1,
          "glue switch: key off, the game's 1 is back");
    uint8_t odd = 7;
    g_lines.clear();
    nativeVrCinemaSwitchStep(&odd, 0x5E85B8Au, true, 0, &sink, nullptr);
    nativeVrCinemaSwitchStep(&odd, 0x5E85B8Au, true, 0, &sink, nullptr);
    check(odd == 7 && countLines("head camera cannot work") == 1, "glue switch: an unexpected value is left alone, said once");
    nativeVrCinemaSwitchStep(&odd, 0x5E85B8Au, false, 0, &sink, nullptr);
    g_lines.clear();
    nativeVrCinemaSwitchStep(nullptr, 0, true, 0, &sink, nullptr);
    nativeVrCinemaSwitchStep(nullptr, 0, true, 0, &sink, nullptr);
    check(countLines("head camera cannot work (the cinema-on-foot switch was not found") == 1, "glue switch: not found, said once");
    nativeVrCinemaSwitchStep(nullptr, 0, false, 0, &sink, nullptr);
}

// ---- Part G: finding the switch by its reader's code ------------------------------------------------------------------------------
// A synthetic image: code at 0x1000 (0x1000 bytes, execute), data at 0x2000 (0x800, write), read-only data at 0x2800 (0x800).
constexpr uint32_t kImgText = 0x1000, kImgData = 0x2000, kImgRdata = 0x2800, kImgSize = 0x3000;
// The pattern at `at` (an RVA) pointing at `target`; `call` and `cmp` are the two register-bearing bytes (FF 9r, 4x 38 mr).
void putPattern(std::vector<uint8_t>& img, uint32_t at, uint32_t target, uint8_t call = 0x90, uint8_t rex = 0x44, uint8_t mr = 0x2D) {
    const uint8_t head[] = {0xFF, call, 0xB0, 0, 0, 0, rex, 0x38, mr};
    std::memcpy(&img[at], head, sizeof(head));
    const int32_t disp = static_cast<int32_t>(target) - static_cast<int32_t>(at + nvf::kSwitchCmpOffset + nvf::kSwitchCmpBytes);
    std::memcpy(&img[at + 9], &disp, sizeof(disp));
    const uint8_t tail[] = {0x74, 0x05, 0x84, 0xC0, 0x75};
    std::memcpy(&img[at + 13], tail, sizeof(tail));
}
nvf::SwitchFind findIn(const std::vector<uint8_t>& img) {
    nvf::ImageSection s[3];
    s[0].rva = kImgText, s[0].size = 0x1000, s[0].execute = true;
    s[1].rva = kImgData, s[1].size = 0x800, s[1].write = true;
    s[2].rva = kImgRdata, s[2].size = 0x800;
    return nvf::findCinemaSwitch(img.data(), s, 3);
}
// The game HUD switch's reader (Part H); Part G's glue checks use it too.
void putHudPattern(std::vector<uint8_t>& img, uint32_t at, uint32_t target, uint8_t movMr = 0x05, uint8_t viewRex = 0x49,
                   uint8_t viewMr = 0x8F, uint8_t floatMr = 0x80);

void partG() {
    {
        std::vector<uint8_t> img(kImgSize, 0xCC);
        nvf::SwitchFind f = findIn(img);
        check(f.result == nvf::kFindNone && f.matches == 0, "find: no pattern, not found");
        putPattern(img, 0x1100, 0x2010);
        f = findIn(img);
        check(f.result == nvf::kFindFound && f.matches == 1 && f.readerRva == 0x1106 && f.byteRva == 0x2010,
              "find: one pattern, the byte's RVA is the cmp's end plus disp32");
        putPattern(img, 0x1800, 0x2020);
        f = findIn(img);
        check(f.result == nvf::kFindMany && f.matches == 2, "find: two patterns, refused with the count");
    }
    {
        std::vector<uint8_t> img(kImgSize, 0xCC);
        putPattern(img, 0x1100, 0x2900);
        check(findIn(img).result == nvf::kFindNotWritable, "find: the byte in a section that cannot be written, refused");
        putPattern(img, 0x1100, 0x5000);
        check(findIn(img).result == nvf::kFindOutside, "find: the byte outside every section, refused");
        putPattern(img, 0x1100, 0x0FF0);
        check(findIn(img).result == nvf::kFindOutside, "find: the byte before the first section, refused");
    }
    {
        std::vector<uint8_t> img(kImgSize, 0xCC);
        putPattern(img, 0x1100, 0x2010, 0x93, 0x45, 0x3D);
        nvf::SwitchFind f = findIn(img);
        check(f.result == nvf::kFindFound && f.byteRva == 0x2010, "find: other registers in the call and the cmp, found");
        std::vector<uint8_t> sib(kImgSize, 0xCC);
        putPattern(sib, 0x1100, 0x2010, 0x94);
        check(findIn(sib).result == nvf::kFindNone, "find: FF 94 (a SIB byte follows) is not the pattern");
        std::vector<uint8_t> notRip(kImgSize, 0xCC);
        putPattern(notRip, 0x1100, 0x2010, 0x90, 0x44, 0x6D);
        check(findIn(notRip).result == nvf::kFindNone, "find: a cmp that is not RIP-relative is not the pattern");
        std::vector<uint8_t> noRex(kImgSize, 0xCC);
        putPattern(noRex, 0x1100, 0x2010, 0x90, 0x40);
        check(findIn(noRex).result == nvf::kFindNone, "find: a cmp without REX.R is not the pattern");
        std::vector<uint8_t> inData(kImgSize, 0xCC);
        putPattern(inData, 0x2100, 0x2010);
        check(findIn(inData).result == nvf::kFindNone, "find: the pattern in a section that does not execute is not looked at");
        std::vector<uint8_t> atEnd(kImgSize, 0xCC);
        putPattern(atEnd, kImgText + 0x1000 - static_cast<uint32_t>(nvf::kSwitchPatternBytes), 0x2010);
        check(findIn(atEnd).result == nvf::kFindFound, "find: the pattern in the code section's last bytes, found");
        std::vector<uint8_t> broken(kImgSize, 0xCC);
        putPattern(broken, 0x1100, 0x2010);
        broken[0x1100 + 17] = 0x74;
        check(findIn(broken).result == nvf::kFindNone, "find: the last branch not a jne, not the pattern");
    }
    {
        char line[400];
        nvf::SwitchFind f;
        f.result = nvf::kFindFound, f.matches = 1, f.readerRva = 0x288A139, f.byteRva = 0x5E85B8A;
        nvf::formatFind(f, 0x6ACD6F3D, 0x640B000, 12, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: cinema-on-foot switch found at EliteDangerous64.exe+0x5E85B8A (read at +0x288A139") &&
                  std::strstr(line, "PE stamp 0x6ACD6F3D") && std::strstr(line, "12 ms"),
              "text: found, with the reader, the build and the time");
        f.result = nvf::kFindNone;
        nvf::formatFind(f, 1, 2, 3, line, sizeof(line));
        check(std::strstr(line, "not found: no call-then-compare") && std::strstr(line, "the game keeps its cinema on foot"),
              "text: none");
        f.result = nvf::kFindMany, f.matches = 3;
        nvf::formatFind(f, 1, 2, 3, line, sizeof(line));
        check(std::strstr(line, "not found: 3 call-then-compare matches") != nullptr, "text: many, with the count");
        f.result = nvf::kFindNotWritable;
        nvf::formatFind(f, 1, 2, 3, line, sizeof(line));
        check(std::strstr(line, "cannot be written (+0x5E85B8A") != nullptr, "text: not writable, with the address");
        f.result = nvf::kFindOutside;
        nvf::formatFind(f, 1, 2, 3, line, sizeof(line));
        check(std::strstr(line, "outside the game's image (+0x5E85B8A") != nullptr, "text: outside, with the address");
    }

    // The glue on a synthetic PE image in memory: its own headers name the sections.
    auto makePe = [](std::vector<uint8_t>& img, int sections) {
        img.assign(kImgSize, 0xCC);
        std::memset(img.data(), 0, 0x400);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(img.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(img.data() + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections = static_cast<WORD>(sections);
        nt->FileHeader.TimeDateStamp = 0x6ACD6F3D;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = kImgSize;
        IMAGE_SECTION_HEADER* h = IMAGE_FIRST_SECTION(nt);
        const uint32_t rvas[3] = {kImgText, kImgData, kImgRdata}, sizes[3] = {0x1000, 0x800, 0x800};
        const DWORD flags[3] = {IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, IMAGE_SCN_MEM_READ};
        for (int i = 0; i < 3 && i < sections; ++i) {
            h[i].VirtualAddress = rvas[i];
            h[i].Misc.VirtualSize = sizes[i];
            h[i].Characteristics = flags[i];
        }
    };
    std::vector<uint8_t> pe;
    makePe(pe, 3);
    putPattern(pe, 0x1100, 0x2010);
    g_lines.clear();
    const uint32_t rva = nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr);
    check(rva == 0x2010 && countLines("cinema-on-foot switch found at EliteDangerous64.exe+0x2010 (read at +0x1106") == 1 &&
              anyLine("PE stamp 0x6ACD6F3D, image size 0x3000"),
          "glue find: the image's own headers, the one pattern, one line");
    pe[0x2010] = 1;
    nativeVrCinemaSwitchStep(&pe[0x2010], rva, true, 0, &sink, nullptr);
    check(pe[0x2010] == 0 && anyLine("switch (EliteDangerous64.exe+0x2010) 1 -> 0"), "glue find: the step writes the found byte, named");
    nativeVrCinemaSwitchStep(&pe[0x2010], rva, false, 0, &sink, nullptr);
    check(pe[0x2010] == 1, "glue find: key off, the game's 1 is back");
    makePe(pe, 40);
    g_lines.clear();
    check(nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr) == 0 && countLines("not searched") == 1,
          "glue find: more sections than fit, not searched, said");
    makePe(pe, 3);
    g_lines.clear();
    check(nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr) == 0 && countLines("not found: no call") == 1,
          "glue find: no pattern, 0 and one line");
    void* noAccess = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    g_lines.clear();
    check(noAccess && nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(noAccess), &sink, nullptr) == 0 && countLines("not found") == 1,
          "glue find: unreadable headers fault into a not-found, no crash");
    if (noAccess) VirtualFree(noAccess, 0, MEM_RELEASE);

    // The game HUD switch's glue on the same synthetic PE (its pattern and step are Part H's).
    makePe(pe, 3);
    putHudPattern(pe, 0x1100, 0x2014);
    g_lines.clear();
    const uint32_t hud = nativeVrHudSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr);
    check(hud == 0x2014 && countLines("game HUD switch found at EliteDangerous64.exe+0x2014 (read at +0x1100") == 1,
          "glue hud find: the one pattern, one line");
    check(nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr) == 0,
          "glue hud find: the cinema search does not take it");
    makePe(pe, 3);
    putHudPattern(pe, 0x1100, 0x2013);
    g_lines.clear();
    check(nativeVrHudSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr) == 0 && countLines("not 4-byte aligned") == 1,
          "glue hud find: an unaligned int is refused, said");
    makePe(pe, 3);
    g_lines.clear();
    check(nativeVrHudSwitchFind(reinterpret_cast<uintptr_t>(pe.data()), &sink, nullptr) == 0 &&
              countLines("game HUD switch not found: no read of its kind") == 1,
          "glue hud find: no pattern, 0 and one line");

    alignas(4) int32_t hudInt = -1;
    g_lines.clear();
    nativeVrHudSwitchStep(&hudInt, 0x5E85B94u, false, 0, &sink, nullptr);
    check(hudInt == -1 && g_lines.empty(), "glue hud switch: key off, the int and the log are untouched");
    nativeVrHudSwitchStep(&hudInt, 0x5E85B94u, true, 0, &sink, nullptr);
    check(hudInt == 1 && countLines("game HUD on: the game's HUD switch (EliteDangerous64.exe+0x5E85B94) -1 (auto) -> 1 (on)") == 1,
          "glue hud switch: key on, the int is 1, one line");
    nativeVrHudSwitchStep(&hudInt, 0x5E85B94u, true, 1, &sink, nullptr);
    check(hudInt == 1 && g_lines.size() == 1, "glue hud switch: next frame, nothing more");
    for (int i = 0; i < 100; ++i) {
        hudInt = -1;
        nativeVrHudSwitchStep(&hudInt, 0x5E85B94u, true, 1, &sink, nullptr);
    }
    check(hudInt == 1 && countLines("back to -1; 1 again") == 3 && anyLine("(100 time(s))"),
          "glue hud switch: the game puts -1 back a hundred times: 1 each time, said at 1, 10, 100");
    nativeVrHudSwitchStep(&hudInt, 0x5E85B94u, false, 0, &sink, nullptr);
    check(hudInt == -1 && countLines("game HUD off: the HUD switch is the game's -1 (auto) again") == 1,
          "glue hud switch: key off, the game's -1 is back");
    alignas(4) int32_t hudOdd = 0;
    g_lines.clear();
    nativeVrHudSwitchStep(&hudOdd, 0x5E85B94u, true, 0, &sink, nullptr);
    nativeVrHudSwitchStep(&hudOdd, 0x5E85B94u, true, 0, &sink, nullptr);
    check(hudOdd == 0 && countLines("game HUD cannot work (the HUD switch holds 0") == 1, "glue hud switch: a 0 is left alone, said once");
    nativeVrHudSwitchStep(&hudOdd, 0x5E85B94u, false, 0, &sink, nullptr);
    g_lines.clear();
    nativeVrHudSwitchStep(nullptr, 0, true, 0, &sink, nullptr);
    nativeVrHudSwitchStep(nullptr, 0, true, 0, &sink, nullptr);
    check(countLines("game HUD cannot work (the HUD switch was not found") == 1, "glue hud switch: not found, said once");
    nativeVrHudSwitchStep(nullptr, 0, false, 0, &sink, nullptr);
    void* noAccessHud = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    g_lines.clear();
    if (noAccessHud) {
        nativeVrHudSwitchStep(static_cast<int32_t*>(noAccessHud), 0x5E85B94u, true, 0, &sink, nullptr);
        nativeVrHudSwitchStep(static_cast<int32_t*>(noAccessHud), 0x5E85B94u, false, 0, &sink, nullptr);
        VirtualFree(noAccessHud, 0, MEM_RELEASE);
    }
    check(noAccessHud && countLines("game HUD cannot work (the HUD switch could not be read)") == 1,
          "glue hud switch: an unreadable int faults into a refusal, no crash");
}

// ---- Part H: the game HUD switch (experimental.native_vr_on_foot's game HUD) ------------------------------------------------------
// The reader's 33 bytes at `at` (an RVA) pointing at `target`, as build 334473 has them at 0x288A055; the overrides swap registers.
void putHudPattern(std::vector<uint8_t>& img, uint32_t at, uint32_t target, uint8_t movMr, uint8_t viewRex, uint8_t viewMr,
                   uint8_t floatMr) {
    const uint8_t code[nvf::kHudPatternBytes] = {0x8B, movMr, 0,    0,    0,    0,    0x0F, 0x57, 0xFF, viewRex, 0x8B,
                                                 viewMr, 0x80, 0x11, 0,    0,    0x41, 0x3B, 0xC4, 0x75, 0x14,    0x48,
                                                 0x8B, 0x41, 0x18, 0xF3, 0x0F, 0x10, floatMr, 0x6C, 0x01, 0,    0};
    std::memcpy(&img[at], code, sizeof(code));
    const int32_t disp = static_cast<int32_t>(target) - static_cast<int32_t>(at + 6);
    std::memcpy(&img[at + 2], &disp, sizeof(disp));
}
nvf::SwitchFind findHudIn(const std::vector<uint8_t>& img) {
    nvf::ImageSection s[3];
    s[0].rva = kImgText, s[0].size = 0x1000, s[0].execute = true;
    s[1].rva = kImgData, s[1].size = 0x800, s[1].write = true;
    s[2].rva = kImgRdata, s[2].size = 0x800;
    return nvf::findHudSwitch(img.data(), s, 3);
}

void partH() {
    {
        // The game's own bytes, copied from EliteDangerous64.exe 334473 at 0x288A055, disp32 and all.
        const uint8_t real[nvf::kHudPatternBytes] = {0x8B, 0x05, 0x39, 0xBB, 0x5F, 0x03, 0x0F, 0x57, 0xFF, 0x49, 0x8B,
                                                     0x8F, 0x80, 0x11, 0x00, 0x00, 0x41, 0x3B, 0xC4, 0x75, 0x14, 0x48,
                                                     0x8B, 0x41, 0x18, 0xF3, 0x0F, 0x10, 0x80, 0x6C, 0x01, 0x00, 0x00};
        check(nvf::hudPatternAt(real), "hud find: build 334473's reader is the pattern");
        int32_t disp;
        std::memcpy(&disp, real + 2, sizeof(disp));
        check(0x288A055u + 6u + static_cast<uint32_t>(disp) == 0x5E85B94u, "hud find: its disp32 lands on 0x5E85B94");
        // The same in a synthetic image: the reader at 0x1100 for 0x288A055, the int at 0x2014 (aligned) for 0x5E85B94.
        std::vector<uint8_t> img(kImgSize, 0xCC);
        check(findHudIn(img).result == nvf::kFindNone, "hud find: no pattern, not found");
        std::memcpy(&img[0x1100], real, sizeof(real));
        const int32_t synth = 0x2014 - (0x1100 + 6);
        std::memcpy(&img[0x1102], &synth, sizeof(synth));
        nvf::SwitchFind f = findHudIn(img);
        check(f.result == nvf::kFindFound && f.matches == 1 && f.readerRva == 0x1100 && f.byteRva == 0x2014,
              "hud find: one pattern, the int's RVA is the mov's end plus disp32");
        putHudPattern(img, 0x1800, 0x2020);
        f = findHudIn(img);
        check(f.result == nvf::kFindMany && f.matches == 2, "hud find: two patterns, refused with the count");
        check(findIn(img).result == nvf::kFindNone, "hud find: the cinema search does not take the HUD's reader");
    }
    {
        std::vector<uint8_t> img(kImgSize, 0xCC);
        putHudPattern(img, 0x1100, 0x2010, 0x0D, 0x4C, 0x8E, 0x86);
        check(findHudIn(img).result == nvf::kFindFound && findHudIn(img).byteRva == 0x2010, "hud find: other registers, found");
        putHudPattern(img, 0x1100, 0x2900);
        check(findHudIn(img).result == nvf::kFindNotWritable, "hud find: the int in a section that cannot be written, refused");
        putHudPattern(img, 0x1100, 0x5000);
        check(findHudIn(img).result == nvf::kFindOutside, "hud find: the int outside every section, refused");
        std::vector<uint8_t> notRip(kImgSize, 0xCC);
        putHudPattern(notRip, 0x1100, 0x2010, 0x45);
        check(findHudIn(notRip).result == nvf::kFindNone, "hud find: a first mov that is not RIP-relative is not the pattern");
        std::vector<uint8_t> sib(kImgSize, 0xCC);
        putHudPattern(sib, 0x1100, 0x2010, 0x05, 0x49, 0x8C);
        check(findHudIn(sib).result == nvf::kFindNone, "hud find: [+0x1180] through a SIB byte is not the pattern");
        std::vector<uint8_t> otherField(kImgSize, 0xCC);
        putHudPattern(otherField, 0x1100, 0x2010);
        otherField[0x1100 + 29] = 0x70;
        check(findHudIn(otherField).result == nvf::kFindNone, "hud find: a float other than +0x16C is not the pattern");
        std::vector<uint8_t> je(kImgSize, 0xCC);
        putHudPattern(je, 0x1100, 0x2010);
        je[0x1100 + 19] = 0x74;
        check(findHudIn(je).result == nvf::kFindNone, "hud find: a je, not a jne, is not the pattern");
        std::vector<uint8_t> inData(kImgSize, 0xCC);
        putHudPattern(inData, 0x2100, 0x2010);
        check(findHudIn(inData).result == nvf::kFindNone, "hud find: the pattern in a section that does not execute is not looked at");
        std::vector<uint8_t> atEnd(kImgSize, 0xCC);
        putHudPattern(atEnd, kImgText + 0x1000 - static_cast<uint32_t>(nvf::kHudPatternBytes), 0x2010);
        check(findHudIn(atEnd).result == nvf::kFindFound, "hud find: the pattern in the code section's last bytes, found");
    }
    {
        char line[400];
        nvf::SwitchFind f;
        f.result = nvf::kFindFound, f.matches = 1, f.readerRva = 0x288A055, f.byteRva = 0x5E85B94;
        nvf::formatHudFind(f, 9, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: game HUD switch found at EliteDangerous64.exe+0x5E85B94 (read at +0x288A055") &&
                  std::strstr(line, "9 ms"),
              "hud text: found, with the reader and the time");
        f.result = nvf::kFindNone;
        nvf::formatHudFind(f, 1, line, sizeof(line));
        check(std::strstr(line, "game HUD switch not found: no read of its kind") && std::strstr(line, "turns its HUD off as before"),
              "hud text: none");
        f.result = nvf::kFindMany, f.matches = 2;
        nvf::formatHudFind(f, 1, line, sizeof(line));
        check(std::strstr(line, "2 reads of its kind") != nullptr, "hud text: many, with the count");
        f.result = nvf::kFindNotWritable;
        nvf::formatHudFind(f, 1, line, sizeof(line));
        check(std::strstr(line, "cannot be written (+0x5E85B94") != nullptr, "hud text: not writable, with the address");
        f.result = nvf::kFindOutside;
        nvf::formatHudFind(f, 1, line, sizeof(line));
        check(std::strstr(line, "outside the game's image (+0x5E85B94") != nullptr, "hud text: outside, with the address");
    }
    {
        nvf::SwitchState s;
        nvf::ValueStep r = nvf::hudSwitchStep(s, false, true, -1);
        check(!r.write && r.kind == nvf::kSwitchNone && !s.ours, "hud switch: key off, the game's -1 is left alone");
        r = nvf::hudSwitchStep(s, true, true, -1);
        check(r.write && r.value == 1 && r.kind == nvf::kSwitchSet && s.ours, "hud switch: key on, -1 becomes 1 and is ours");
        r = nvf::hudSwitchStep(s, true, true, 1);
        check(!r.write && r.kind == nvf::kSwitchNone, "hud switch: our 1 in place, nothing to do");
        r = nvf::hudSwitchStep(s, true, true, -1);
        check(r.write && r.value == 1 && r.kind == nvf::kSwitchAgain && s.again == 1, "hud switch: the game puts -1 back: 1 again, counted");
        r = nvf::hudSwitchStep(s, false, true, 1);
        check(r.write && r.value == -1 && r.kind == nvf::kSwitchRestored && !s.ours, "hud switch: key off, our 1 goes back to the game's -1");
        r = nvf::hudSwitchStep(s, false, true, -1);
        check(!r.write && r.kind == nvf::kSwitchNone, "hud switch: key off and restored, nothing more");

        nvf::SwitchState left;
        nvf::hudSwitchStep(left, true, true, -1);
        r = nvf::hudSwitchStep(left, false, true, 0);
        check(!r.write && r.kind == nvf::kSwitchLeft, "hud switch: key off over a value that is not our 1: left as it is");

        nvf::SwitchState g;
        r = nvf::hudSwitchStep(g, true, true, 1);
        check(!r.write && r.kind == nvf::kSwitchGameOff && !g.ours, "hud switch: a 1 before us is the game's own, not ours");
        r = nvf::hudSwitchStep(g, false, true, 1);
        check(!r.write && r.kind == nvf::kSwitchNone, "hud switch: and key off never writes over it");

        for (int32_t odd : {0, 7}) {
            nvf::SwitchState x;
            r = nvf::hudSwitchStep(x, true, true, odd);
            const bool refused = !r.write && r.kind == nvf::kSwitchStrange;
            r = nvf::hudSwitchStep(x, true, true, -1);
            check(refused && !r.write && r.kind == nvf::kSwitchNone, "hud switch: 0 or 7 is refused and stays refused while the key is on");
            nvf::hudSwitchStep(x, false, true, -1);
            r = nvf::hudSwitchStep(x, true, true, -1);
            check(r.write && r.kind == nvf::kSwitchSet, "hud switch: key off and on again tries afresh");
        }
        nvf::SwitchState u;
        nvf::hudSwitchStep(u, true, true, -1);
        r = nvf::hudSwitchStep(u, true, false, 0);
        check(!r.write && r.kind == nvf::kSwitchStrange && !u.ours, "hud switch: unreadable while ours: refused, no longer ours");

        bool onlyOurs = true;
        for (int key = 0; key < 2; ++key)
            for (int32_t v = -3; v < 8; ++v)
                for (int ok = 0; ok < 2; ++ok) {
                    nvf::SwitchState a, b;
                    b.ours = true;
                    for (const nvf::ValueStep& q : {nvf::hudSwitchStep(a, key != 0, ok != 0, v), nvf::hudSwitchStep(b, key != 0, ok != 0, v)})
                        if (q.write && !(ok && ((q.value == 1 && v == -1) || (q.value == -1 && v == 1)))) onlyOurs = false;
                }
        check(onlyOurs, "hud switch: in no case is anything written but -1 -> 1 or 1 -> -1, and never over an unread value");
    }
    {
        char line[400];
        nvf::SwitchState s;
        nvf::ValueStep r = nvf::hudSwitchStep(s, true, true, -1);
        nvf::formatHudSwitch(r, s, true, -1, 0, 0x5E85B94u, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: game HUD on: the game's HUD switch (EliteDangerous64.exe+0x5E85B94) -1 (auto) -> 1 (on)") &&
                  std::strstr(line, "on foot (journal): no"),
              "hud text: the on line, by the RVA it was found at");
        nvf::ValueStep strange;
        strange.kind = nvf::kSwitchStrange;
        nvf::formatHudSwitch(strange, s, true, 7, 0, 0x5E85B94u, line, sizeof(line));
        check(std::strstr(line, "game HUD cannot work (the HUD switch holds 7, not the game's -1)") != nullptr, "hud text: the refusal says the value");
    }
}

// ---- Part I: the main menu after gameplay ------------------------------------------------------------------------------------------
// One frame as nativeVrOnFootFrameBoundary composes it: nvf::mainMenuStep, its line, then both switches' REAL glue with `held` and
// `menu`, on a byte and an int of the rig's own.
struct MenuRig {
    nvf::MainMenuState state;
    uint8_t cinema = 1;
    alignas(4) int32_t hud = -1;
    void frame(bool key, bool mainMenu) {
        const nvf::MainMenuStep m = nvf::mainMenuStep(state, key, mainMenu);
        char line[200];
        if (nvf::formatMainMenu(m.kind, line, sizeof(line)) > 0) sink(nullptr, line);
        nativeVrCinemaSwitchStep(&cinema, 0x5E85B8Au, m.held, 0, &sink, nullptr, m.menu);
        nativeVrHudSwitchStep(&hud, 0x5E85B94u, m.held, 0, &sink, nullptr, m.menu);
    }
};
size_t keyLines() { return countLines("head camera on:") + countLines("head camera off:") + countLines("game HUD on:") + countLines("game HUD off:"); }

void partI() {
    // The pure step.
    {
        nvf::MainMenuState s;
        nvf::MainMenuStep m = nvf::mainMenuStep(s, true, false);
        check(m.held && !m.menu && m.kind == nvf::kMenuNone, "menu step: key on, no menu: held, nothing said");
        m = nvf::mainMenuStep(s, true, true);
        check(!m.held && m.menu && m.kind == nvf::kMenuEnter, "menu step: the menu begins: not held, quiet, the enter line");
        m = nvf::mainMenuStep(s, true, true);
        check(!m.held && m.menu && m.kind == nvf::kMenuNone, "menu step: in the menu: not held, said once");
        m = nvf::mainMenuStep(s, true, false);
        check(m.held && m.menu && m.kind == nvf::kMenuLeave, "menu step: LoadGame: held, quiet, the leave line");
        m = nvf::mainMenuStep(s, true, false);
        check(m.held && !m.menu && m.kind == nvf::kMenuNone, "menu step: after it, as before");
        nvf::MainMenuState off;
        m = nvf::mainMenuStep(off, false, true);
        check(!m.held && !m.menu && m.kind == nvf::kMenuNone, "menu step: key off: the menu says nothing");
        m = nvf::mainMenuStep(off, false, false);
        check(!m.held && !m.menu && m.kind == nvf::kMenuNone, "menu step: key off, LoadGame: nothing");
        nvf::MainMenuState f8;
        nvf::mainMenuStep(f8, true, true);
        m = nvf::mainMenuStep(f8, false, true);
        check(!m.held && !m.menu && m.kind == nvf::kMenuNone, "menu step: key off in the menu is the key's line, not LoadGame's");
        char line[200];
        nvf::formatMainMenu(nvf::kMenuEnter, line, sizeof(line));
        check(std::strcmp(line, "native-vr-on-foot: main menu (journal Music: MainMenu): both switches back to the game until the next "
                                "LoadGame") == 0,
              "text: the main menu's line");
        nvf::formatMainMenu(nvf::kMenuLeave, line, sizeof(line));
        check(std::strcmp(line, "native-vr-on-foot: LoadGame (journal): both switches ours again") == 0, "text: the LoadGame line");
        check(nvf::formatMainMenu(nvf::kMenuNone, line, sizeof(line)) == 0, "text: no change, no line");
    }

    // The glue: key on, play, the menu, LoadGame.
    MenuRig r;
    g_lines.clear();
    r.frame(true, false);
    check(r.cinema == 0 && r.hud == 1 && keyLines() == 2, "glue menu: key on in play: both ours (0 and 1), their on lines");
    g_lines.clear();
    r.frame(true, true);
    check(r.cinema == 1 && r.hud == -1, "glue menu: the main menu: both the game's again (1 and -1)");
    check(countLines("main menu (journal Music: MainMenu)") == 1 && keyLines() == 0 && g_lines.size() == 1,
          "glue menu: ...one main-menu line, no head camera off / game HUD off lines");
    for (int i = 0; i < 10; ++i) r.frame(true, true);
    check(r.cinema == 1 && r.hud == -1 && g_lines.size() == 1, "glue menu: in the menu, nothing written, nothing more said");
    g_lines.clear();
    r.frame(true, false);
    check(r.cinema == 0 && r.hud == 1, "glue menu: LoadGame: both ours again (0 and 1)");
    check(countLines("LoadGame (journal): both switches ours again") == 1 && keyLines() == 0 && g_lines.size() == 1,
          "glue menu: ...one LoadGame line, no on lines");
    g_lines.clear();
    r.frame(true, false);
    check(r.cinema == 0 && r.hud == 1 && g_lines.empty(), "glue menu: next frame, nothing more");

    // A value that is not ours when the menu begins: left as it is, said, as with the key off.
    r.cinema = 7;
    g_lines.clear();
    r.frame(true, true);
    check(r.cinema == 7 && r.hud == -1 && countLines("head camera off: the cinema-on-foot switch holds 7, not our 0; left as it is") == 1,
          "glue menu: a cinema value not ours is left alone and said; the HUD still goes back");
    r.cinema = 1;
    r.frame(true, false);  // LoadGame
    r.frame(false, false);  // key off: both back, as the key line says
    check(r.cinema == 1 && r.hud == -1, "glue menu: key off afterwards: both the game's");

    // Key off: the menu and LoadGame write nothing and say nothing.
    MenuRig k;
    k.state = nvf::MainMenuState{};
    g_lines.clear();
    k.frame(false, false);
    k.frame(false, true);
    k.frame(false, true);
    k.frame(false, false);
    check(k.cinema == 1 && k.hud == -1 && g_lines.empty(), "glue menu: key off: the menu and LoadGame touch nothing, say nothing");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: native_vr_on_foot_test --self-test\n");
        return 2;
    }
    partF();
    partG();
    partH();
    partI();
    std::printf("%s: native_vr_on_foot_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
