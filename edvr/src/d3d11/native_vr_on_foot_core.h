#pragma once
// Native VR on foot, the pure half (EDVRonFOOT; experimental.native_vr_on_foot; the project's docs\research\native-vr-on-foot.md,
// and native_vr_on_foot.h for the glue). Two switches of the game's own data, each found by the code that reads it: the
// cinema-on-foot byte (off: the game keeps headset stereo on foot) and the on-foot HUD int (on: the game keeps its HUD, the
// crosshair and the prompts, in headset stereo). Everything here is plain C++ the rig drives directly: the patterns and the
// search, the steps that decide what to write, and the text of every line.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace nvf {

inline const char* onFootText(int onFoot) { return onFoot < 0 ? "unknown" : (onFoot ? "yes" : "no"); }

// experimental.native_vr_on_foot's line when it is first read and whenever it changes (`again`: it was said before).
inline int formatOnFootKey(bool on, bool again, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-on-foot: %s (experimental.native_vr_on_foot = %d)",
                         on ? "on: on foot the game keeps headset stereo (its cinema-on-foot switch off)"
                            : (again ? "off: the cinema-on-foot switch goes back to the game; the next disembark is the game's cinema again"
                                     : "off"),
                         on ? 1 : 0);
}

// experimental.native_vr_on_foot's head camera: the game's own cinema-on-foot switch, a byte in its render settings block. The
// per-frame update (0x2889A00 in 334473) turns headset stereo (3/4) into cinema (5/6) only while this byte is not 0 AND the view
// says first person on foot; that test is the byte's one reader (0x288A139), and in the ship the second condition is never met, so
// the byte changes nothing there. With the byte 0 the game itself keeps headset stereo on foot -- the source, +0xC8 and the camera
// all at once, as in the ship. The exe's value is 1; the switch writes only 1 -> 0 (key on) and 0 -> 1 (key off, and only a 0 it
// wrote), and nothing over any other value.
constexpr uint8_t kCinemaSwitchGame = 1;

// The byte is found by its reader's code, not by a table of builds (the address moved from an earlier build's 0x5E82B3E to
// 0x5E85B8A in 334473; the code around the reader did not). The pattern is the view's question and then the test of the byte:
//   FF 9r B0 00 00 00       call [reg+0xB0]                 (the view: first person on foot?)
//   4x 38 mr <disp32>       cmp byte ptr [rip+disp], r8b..r15b (mr: mod 00, rm 101)
//   74 ??  84 C0  75        je / test al, al / jne
// The registers are free (a rebuild picks others easily); the shape is not. The byte's RVA is the end of the cmp plus disp32. It is
// taken only when the pattern is in the game's executable code exactly once and the byte lies inside a writable section; the value
// test is cinemaSwitchStep's own (1 is written to 0, 0 is the game's own, anything else is refused).
constexpr size_t kSwitchPatternBytes = 18;
constexpr uint32_t kSwitchCmpOffset = 6;  // the cmp's first byte, from the pattern's first
constexpr uint32_t kSwitchCmpBytes = 7;
inline bool switchPatternAt(const uint8_t* p) {
    return p[0] == 0xFF && (p[1] & 0xF8) == 0x90 && p[1] != 0x94 && p[2] == 0xB0 && p[3] == 0 && p[4] == 0 && p[5] == 0 &&
           (p[6] == 0x44 || p[6] == 0x45) && p[7] == 0x38 && (p[8] & 0xC7) == 0x05 && p[13] == 0x74 && p[15] == 0x84 &&
           p[16] == 0xC0 && p[17] == 0x75;
}
struct ImageSection {
    uint32_t rva = 0, size = 0;  // the section in the loaded image
    bool execute = false, write = false;
};
enum : uint8_t {
    kFindFound = 0,
    kFindNone = 1,         // no match in the executable sections
    kFindMany = 2,         // more than one
    kFindOutside = 3,      // the one match points outside every section
    kFindNotWritable = 4,  // ... or into a section that cannot be written
};
struct SwitchFind {
    uint8_t result = kFindNone;
    uint32_t matches = 0;
    uint32_t readerRva = 0;  // the cmp's RVA (the first match's)
    uint32_t byteRva = 0;
};
// A reader's shape: its length, its first byte (the scan's anchor), the test, and where in it the RIP-relative read is.
struct PatternShape {
    size_t bytes;
    uint8_t first;
    bool (*at)(const uint8_t*);
    uint32_t readerOffset;  // the reading instruction's first byte, from the pattern's first
    uint32_t dispOffset;    // its disp32, from the pattern's first
    uint32_t readerBytes;   // its length: the target is its end plus disp32
};
constexpr PatternShape kCinemaShape = {kSwitchPatternBytes, 0xFF, &switchPatternAt, kSwitchCmpOffset, kSwitchCmpOffset + 3, kSwitchCmpBytes};

// `image` is the loaded image (or the rig's copy of one); only the executable sections are read.
inline SwitchFind findPatternTarget(const uint8_t* image, const ImageSection* sections, int count, const PatternShape& shape) {
    SwitchFind f;
    for (int i = 0; i < count; ++i) {
        const ImageSection& s = sections[i];
        if (!s.execute || s.size < shape.bytes) continue;
        const uint8_t* p = image + s.rva;
        const uint8_t* end = p + s.size - shape.bytes + 1;
        while ((p = static_cast<const uint8_t*>(std::memchr(p, shape.first, static_cast<size_t>(end - p)))) != nullptr) {
            if (shape.at(p)) {
                if (++f.matches == 1) {
                    const uint32_t at = s.rva + static_cast<uint32_t>(p - (image + s.rva));
                    int32_t disp;
                    std::memcpy(&disp, p + shape.dispOffset, sizeof(disp));
                    f.readerRva = at + shape.readerOffset;
                    f.byteRva = static_cast<uint32_t>(static_cast<int64_t>(f.readerRva) + shape.readerBytes + disp);
                }
            }
            if (++p >= end) break;
        }
    }
    if (f.matches == 0) return f;
    if (f.matches > 1) {
        f.result = kFindMany;
        return f;
    }
    f.result = kFindOutside;
    for (int i = 0; i < count; ++i) {
        const ImageSection& s = sections[i];
        if (f.byteRva >= s.rva && f.byteRva - s.rva < s.size) {
            f.result = s.write ? kFindFound : kFindNotWritable;
            break;
        }
    }
    return f;
}
inline SwitchFind findCinemaSwitch(const uint8_t* image, const ImageSection* sections, int count) {
    return findPatternTarget(image, sections, count, kCinemaShape);
}

// experimental.native_vr_on_foot's game HUD: the game's own on-foot HUD in headset stereo. The per-frame update (0x2889A00 in 334473)
// turns the FirstPersonPostGUI pass -- the node that lays the HUD's sheets 36-39 into the picture -- on or off every frame by an int
// of its render settings (0x5E85B94, next to the cinema byte): -1 ("auto") means on while a float of the view (+0x16C) is > 0, which
// on foot in headset stereo it is not, so the node goes off at the disembark (flight .13); anything else means on (!= 0). With 1 the
// game keeps its HUD on itself. The switch writes only -1 -> 1 (key on) and 1 -> -1 (key off, and only a 1 it wrote).
constexpr int32_t kHudSwitchGame = -1;
constexpr int32_t kHudSwitchOurs = 1;
// The int's one reader (0x288A055 in 334473), found by its code like the cinema byte:
//   8B 05 <disp32>                  mov e?x, [rip+disp]          (the int)
//   0F 57 xx                        xorps xmm, xmm
//   4x 8B mr 80 11 00 00            mov r64, [r64+0x1180]        (the view)
//   4x 3B mr                        cmp e?x, r32                 (-1 in a register)
//   75 ??                           jne
//   48 8B 4r 18                     mov r64, [r64+0x18]
//   F3 0F 10 mr 6C 01 00 00         movss xmm, [r64+0x16C]       (the float)
// Registers free, shape fixed. The int's RVA is the end of the first mov plus disp32.
constexpr size_t kHudPatternBytes = 33;
inline bool hudPatternAt(const uint8_t* p) {
    const auto disp32Mem = [](uint8_t mr) { return (mr & 0xC0) == 0x80 && (mr & 7) != 4; };  // [r64+disp32], no SIB
    return p[0] == 0x8B && (p[1] & 0xC7) == 0x05 &&                                            // mov r32, [rip+disp32]
           p[6] == 0x0F && p[7] == 0x57 && (p[8] & 0xC0) == 0xC0 &&                             // xorps xmm, xmm
           (p[9] & 0xF8) == 0x48 && p[10] == 0x8B && disp32Mem(p[11]) && p[12] == 0x80 && p[13] == 0x11 && p[14] == 0 &&
           p[15] == 0 &&                                                                         // mov r64, [r64+0x1180]
           (p[16] & 0xF0) == 0x40 && p[17] == 0x3B && (p[18] & 0xC0) == 0xC0 &&                  // cmp r32, r32
           p[19] == 0x75 &&                                                                      // jne
           (p[21] & 0xF8) == 0x48 && p[22] == 0x8B && (p[23] & 0xC0) == 0x40 && (p[23] & 7) != 4 && p[24] == 0x18 &&  // [r64+0x18]
           p[25] == 0xF3 && p[26] == 0x0F && p[27] == 0x10 && disp32Mem(p[28]) && p[29] == 0x6C && p[30] == 0x01 && p[31] == 0 &&
           p[32] == 0;                                                                           // movss xmm, [r64+0x16C]
}
constexpr PatternShape kHudShape = {kHudPatternBytes, 0x8B, &hudPatternAt, 0, 2, 6};
inline SwitchFind findHudSwitch(const uint8_t* image, const ImageSection* sections, int count) {
    return findPatternTarget(image, sections, count, kHudShape);
}
inline int formatHudFind(const SwitchFind& f, uint64_t ms, char* out, size_t cap) {
    const char* tail = "nothing will be written there; on foot the game turns its HUD off as before";
    switch (f.result) {
        case kFindFound:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch found at EliteDangerous64.exe+0x%X (read at +0x%X, the one read of "
                                 "its kind before the view's +0x1180 -> +0x18 -> +0x16C in the game's code; %llu ms)",
                                 f.byteRva, f.readerRva, static_cast<unsigned long long>(ms));
        case kFindNone:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD switch not found: no read of its kind in the game's code; %s", tail);
        case kFindMany:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD switch not found: %u reads of its kind in the game's code, not one; %s",
                                 f.matches, tail);
        case kFindOutside:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch not found: the one match (read at +0x%X) points outside the game's "
                                 "image (+0x%X); %s",
                                 f.readerRva, f.byteRva, tail);
        default:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch not found: the one match (read at +0x%X) points into a section "
                                 "that cannot be written (+0x%X); %s",
                                 f.readerRva, f.byteRva, tail);
    }
}
inline int formatFind(const SwitchFind& f, uint32_t timestamp, uint32_t imageSize, uint64_t ms, char* out, size_t cap) {
    const char* tail = "nothing will be written there; the game keeps its cinema on foot";
    switch (f.result) {
        case kFindFound:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch found at EliteDangerous64.exe+0x%X (read at +0x%X, the one "
                                 "call-then-compare of its kind in the game's code; PE stamp 0x%08X, image size 0x%X; %llu ms)",
                                 f.byteRva, f.readerRva, timestamp, imageSize, static_cast<unsigned long long>(ms));
        case kFindNone:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: no call-then-compare of its kind in the game's "
                                 "code (PE stamp 0x%08X, image size 0x%X; %llu ms); %s",
                                 timestamp, imageSize, static_cast<unsigned long long>(ms), tail);
        case kFindMany:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: %u call-then-compare matches in the game's code, "
                                 "not one (PE stamp 0x%08X, image size 0x%X); %s",
                                 f.matches, timestamp, imageSize, tail);
        case kFindOutside:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: the one match (read at +0x%X) points outside the "
                                 "game's image (+0x%X; PE stamp 0x%08X); %s",
                                 f.readerRva, f.byteRva, timestamp, tail);
        default:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: the one match (read at +0x%X) points into a "
                                 "section that cannot be written (+0x%X; PE stamp 0x%08X); %s",
                                 f.readerRva, f.byteRva, timestamp, tail);
    }
}
enum : uint8_t {
    kSwitchNone = 0,
    kSwitchSet = 1,       // 1 -> 0: ours from now on
    kSwitchAgain = 2,     // the game put 1 back over ours: 0 again
    kSwitchRestored = 3,  // key off: our 0 -> the game's 1
    kSwitchLeft = 4,      // key off, but the byte is no longer our 0: left as it is
    kSwitchStrange = 5,   // neither 1 nor 0 (or unreadable): never written, said once until the key goes off
    kSwitchGameOff = 6,   // already 0 before us: the game's own, not ours, never restored
};
struct SwitchState {
    bool ours = false;         // the byte holds a 0 the switch wrote
    bool refused = false;      // kSwitchStrange said: nothing is written until the key goes off
    bool saidGameOff = false;  // kSwitchGameOff said
    uint32_t again = 0;        // kSwitchAgain count since kSwitchSet
};
struct SwitchStep {
    int write = -1;  // the value to write (-1: none)
    uint8_t kind = kSwitchNone;
};
// Any switch of the game's data that holds `game` and that we keep at `ours`: the write as a flag, since -1 is a value here.
struct ValueStep {
    bool write = false;
    int32_t value = 0;  // the value to write, when `write`
    uint8_t kind = kSwitchNone;
};
// `value` is the switch as read this frame, meaningless unless `readOk`. kSwitchGameOff here means "already `ours`, the game's own".
inline ValueStep switchStep(SwitchState& s, bool key, bool readOk, int32_t value, int32_t game, int32_t ours) {
    ValueStep r;
    if (key) {
        if (s.refused) return r;
        if (readOk && value == game) {
            r.write = true;
            r.value = ours;
            r.kind = s.ours ? kSwitchAgain : kSwitchSet;
            s.again = s.ours ? s.again + 1 : 0;
            s.ours = true;
        } else if (readOk && value == ours) {
            if (!s.ours && !s.saidGameOff) {
                r.kind = kSwitchGameOff;
                s.saidGameOff = true;
            }
        } else {
            r.kind = kSwitchStrange;
            s.ours = false;
            s.refused = true;
        }
    } else {
        if (s.ours) {
            if (readOk && value == ours) {
                r.write = true;
                r.value = game;
                r.kind = kSwitchRestored;
            } else {
                r.kind = kSwitchLeft;
            }
        }
        s.ours = false;
        s.refused = false;
        s.saidGameOff = false;
        s.again = 0;
    }
    return r;
}
// `value` is the byte as read this frame, -1 if it could not be read.
inline SwitchStep cinemaSwitchStep(SwitchState& s, bool key, int value) {
    const ValueStep v = switchStep(s, key, value >= 0, value, kCinemaSwitchGame, 0);
    SwitchStep r;
    r.kind = v.kind;
    r.write = v.write ? v.value : -1;
    return r;
}
inline ValueStep hudSwitchStep(SwitchState& s, bool key, bool readOk, int32_t value) {
    return switchStep(s, key, readOk, value, kHudSwitchGame, kHudSwitchOurs);
}
// A repeat is said at 1, 10, 100, ... so a game that puts 1 back every frame is visible without flooding the log.
inline bool switchAgainSaid(uint32_t again) {
    while (again >= 10 && again % 10 == 0) again /= 10;
    return again == 1;
}
// `rva` is where the switch was found (findCinemaSwitch).
inline int formatSwitch(const SwitchStep& step, const SwitchState& s, int value, int onFoot, uint32_t rva, char* out, size_t cap) {
    switch (step.kind) {
        case kSwitchSet:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera on: the game's cinema-on-foot switch (EliteDangerous64.exe+0x%X) 1 -> 0; "
                                 "on foot the game keeps headset stereo and its headset camera itself, as in the ship (on foot (journal): %s)",
                                 rva, onFootText(onFoot));
        case kSwitchAgain:
            return std::snprintf(out, cap, "native-vr-on-foot: head camera: the game put its cinema-on-foot switch back to 1; 0 again (%u time(s))",
                                 s.again);
        case kSwitchRestored:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera off: the cinema-on-foot switch is the game's 1 again; the next disembark is "
                                 "the game's cinema");
        case kSwitchLeft:
            return std::snprintf(out, cap, "native-vr-on-foot: head camera off: the cinema-on-foot switch holds %d, not our 0; left as it is",
                                 value);
        case kSwitchStrange:
            if (value < 0)
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch could not be read); nothing "
                                     "written there; the game keeps its cinema on foot");
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch holds %d, not the game's 1); "
                                 "nothing written there; the game keeps its cinema on foot",
                                 value);
        case kSwitchGameOff:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera: the cinema-on-foot switch is already 0, the game's own; nothing written");
        default:
            return 0;
    }
}
// The journal's on-foot answer changed while the switch is ours: the line a flight log is read by.
inline int formatHeadCameraJournal(int onFoot, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-on-foot: head camera: %s",
                         onFoot == 1 ? "on foot (journal) with the cinema-on-foot switch 0: the game's own headset stereo"
                                     : "back in the ship (journal); the switch stays 0 while the key is on (it acts only on foot)");
}

// The game HUD switch's lines (hudSwitchStep); `rva` is where it was found, `value` what it held when read.
inline int formatHudSwitch(const ValueStep& step, const SwitchState& s, bool readOk, int32_t value, int onFoot, uint32_t rva, char* out,
                           size_t cap) {
    switch (step.kind) {
        case kSwitchSet:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD on: the game's HUD switch (EliteDangerous64.exe+0x%X) -1 (auto) -> 1 (on); on "
                                 "foot the game keeps its FirstPersonPostGUI pass, the HUD, on in headset stereo (on foot (journal): %s)",
                                 rva, onFootText(onFoot));
        case kSwitchAgain:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD: the game put its HUD switch back to -1; 1 again (%u time(s))", s.again);
        case kSwitchRestored:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch is the game's -1 (auto) again");
        case kSwitchLeft:
            if (!readOk)
                return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch could not be read; left as it is");
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch holds %d, not our 1; left as it is", value);
        case kSwitchStrange:
            if (!readOk)
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: game HUD cannot work (the HUD switch could not be read); nothing written there");
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD cannot work (the HUD switch holds %d, not the game's -1); nothing written "
                                 "there",
                                 value);
        case kSwitchGameOff:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD: the HUD switch is already 1, the game's own; nothing written");
        default:
            return 0;
    }
}

}  // namespace nvf
}  // namespace edvr
