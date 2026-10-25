#pragma once
// The on-foot HUD (EDVRonFOOT; experimental.native_vr_on_foot_hud; the project's docs\research\native-vr-on-foot.md, "Полёт .9";
// on_foot_hud_core.h for the pure half and its reasons).
//
// With native VR on foot the game draws its whole on-foot interface into hidden panels and, in headset stereo, never lays them into
// the picture. This shows the HUD panel (the one with the HUD's proportions, 1.665) as a sheet bent round the head, in front of the eyes
// (visor) or in front of the body (body), composited at the door by native_menu.cpp's treat, under the F8 menu, with menu_panel's pass.
//
// THREE PLACES, THREE THREADS AT MOST.
//   onFootHudNoteDraw      the draw hook (vscreen.cpp), every draw while onFootHudWantsDraws(): counts the interface draws per target
//                          that is not an eye and notes the game's own composite -- only when that draw reads the HUD panel (a
//                          PSGetShaderResources of slots 0-2, released in the same call). While the flag is down it is not called at all.
//   onFootHudFrameBoundary the frame thread (device_hook.cpp): the keys, the journal, the flag, the frame's panels ranked and kept
//                          (with a reference) until the next boundary, a line for each new panel size, the panel key, the 30-second summary.
//   onFootHudSheetForEye   the door (native_menu.cpp's treat, every eye of every frame): the decision and its line, the copy of the
//                          HUD panel into a texture of EDVR's that the sheet reads -- only from a frame that drew into it; a frame
//                          that did not leaves the last copy, held up to 30 frames.
#include <atomic>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace edvr {

namespace detail {
extern std::atomic<bool> g_onFootHudWants;
}

// The draw path's one load: on foot (journal), native VR on foot on, the HUD key not off.
inline bool onFootHudWantsDraws() { return detail::g_onFootHudWants.load(std::memory_order_relaxed); }

// `eye`: the bound target is an eye texture (vscreen's rtv0Eye).
void onFootHudNoteDraw(ID3D11DeviceContext* ctx, bool eye);

void onFootHudFrameBoundary();

// What the door draws for this eye: the copy's view, its height over its width, the width in degrees and the anchor (0 visor,
// 1 body). False: nothing this frame (the reason is in the log when it changed). Eye 0 decides and copies; eye 1 reuses it.
struct OnFootHudSheet {
    ID3D11ShaderResourceView* srv = nullptr;
    float aspect = 0.0f;
    float widthDeg = 0.0f;
    int anchor = 0;
};
bool onFootHudSheetForEye(ID3D11Device* device, uint32_t eye, OnFootHudSheet* out);

}  // namespace edvr
