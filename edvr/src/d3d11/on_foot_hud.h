#pragma once
// The on-foot HUD (EDVRonFOOT; experimental.native_vr_on_foot_hud; the project's docs\research\native-vr-on-foot.md, "Полёт .9";
// on_foot_hud_core.h for the pure half and its reasons).
//
// With native VR on foot the game draws its whole on-foot interface into hidden panels and, in headset stereo, never lays them into
// the picture. This shows the HUD panel (the one with the HUD's proportions, 1.665) as a sheet bent round the head, in front of the eyes
// (visor) or in front of the body (body), and over it the menus the game draws into panels of their own (.25: the lift and ship door,
// the NPC's answers, the dialog's second panel) at their places, composited at the door by native_menu.cpp's treat, under the F8 menu,
// with menu_panel's pass.
//
// THREE PLACES, THREE THREADS AT MOST.
//   onFootHudNoteDraw      the draw hook (vscreen.cpp), every draw while onFootHudWantsDraws(): counts the interface draws per target
//                          that is not an eye and notes the game's own composite -- only when that draw reads a layer's panel, and
//                          for that layer only (a PSGetShaderResources of slots 0-2, released in the same call). While the flag is
//                          down it is not called at all.
//   onFootHudFrameBoundary the frame thread (device_hook.cpp): the keys, the journal, the flag, the frame's panels ranked and kept
//                          (with a reference) until the next boundary, a line for each new panel size, the 30-second summary.
//   onFootHudSheetsForEye  the door (native_menu.cpp's treat, every eye of every frame): each layer's decision and its line, the copy
//                          of its panel into a texture of EDVR's that its sheet reads -- only from a frame that drew into it; a frame
//                          that did not leaves the layer's last copy, held up to 30 frames.
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

// What the door draws for this eye, sheet by sheet: the copy's view, its height over its width, the width in degrees, its place
// from the anchor (degrees to the right, metres down), its bend, the anchor (0 visor, 1 body) and the layer (0 the HUD, then the
// menus: on_foot_hud_core.h's Layer, menu_panel's set of textures). Returns how many, in compositing order -- the HUD first, the menus
// over it; 0 is nothing this frame (the reason is in the log when it changed). Eye 0 decides and copies; eye 1 reuses it.
constexpr int kOnFootHudSheets = 4;
struct OnFootHudSheet {
    ID3D11ShaderResourceView* srv = nullptr;
    float aspect = 0.0f;
    float widthDeg = 0.0f;
    float yawDeg = 0.0f;
    float downM = 0.0f;
    float curve = 0.0f;
    int anchor = 0;
    int layer = 0;
};
int onFootHudSheetsForEye(ID3D11Device* device, uint32_t eye, OnFootHudSheet out[kOnFootHudSheets]);

}  // namespace edvr
