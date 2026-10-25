#pragma once
// Native VR on foot (EDVRonFOOT; experimental.native_vr_on_foot, on by default; the project's docs\research\native-vr-on-foot.md).
//
// On foot the game shows a flat "cinema" screen in the headset. It decides that by a switch of its own render settings, a byte
// found here by the code that reads it (native_vr_on_foot_core.h); with the byte 0 the game itself keeps headset stereo and its
// headset camera on foot, as in the ship. The same key also turns on the game's own on-foot HUD (an int next to the byte, -1 auto
// -> 1 on), so the game draws its crosshair and prompts into the eyes' pictures; the HUD's panels are EDVR's HUD sheet
// (on_foot_hud.h). Nothing is hooked: two values of the game's data, each written only from the game's value to ours and back.
#include <cstddef>
#include <cstdint>

namespace edvr {

using NativeVrOnFootSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkNativeVrOnFoot). Reads experimental.native_vr_on_foot (on when
// missing); the first time it is on, finds both switches; while it is on, the cinema-on-foot switch is off and the game HUD switch
// is on, and when it goes off both go back to the game's values. Render thread only.
void nativeVrOnFootFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// The head camera. Find: the switch's RVA in the image at `base`, by its reader's code (nvf::findCinemaSwitch over the image's own
// section table), with one line saying where or why not; 0 if not found. Once. Step (nvf::cinemaSwitchStep): once a frame, the
// switch at `address` (null: not found; `rva` names it in the lines) is turned 1 -> 0 while the key is on, and our 0 back to 1 when
// it goes off, with a line on each change and when the journal's on-foot answer changes while the switch is ours. Frame thread.
uint32_t nativeVrCinemaSwitchFind(uintptr_t base, NativeVrOnFootSink sink, void* ctx);
void nativeVrCinemaSwitchStep(uint8_t* address, uint32_t rva, bool key, int onFoot, NativeVrOnFootSink sink, void* ctx);

// The game's own on-foot HUD in headset stereo. Find: the HUD switch's RVA (an int; -1 auto) by its reader's code
// (nvf::findHudSwitch), one line, 0 if not found (or not 4-aligned). Once. Step (nvf::hudSwitchStep): once a frame, the int at
// `address` is turned -1 -> 1 while `key` is on, and our 1 back to -1 when it goes off, with a line on each change. Frame thread.
uint32_t nativeVrHudSwitchFind(uintptr_t base, NativeVrOnFootSink sink, void* ctx);
void nativeVrHudSwitchStep(int32_t* address, uint32_t rva, bool key, int onFoot, NativeVrOnFootSink sink, void* ctx);

}  // namespace edvr
