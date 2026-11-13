#pragma once
// The on-foot scope zoom (EDVRonFOOT; experimental.native_vr_on_foot_scope_zoom; openspec change on-foot-scope-zoom, after the probe
// scope-zoom-probe). scope_zoom_core.h is the pure half and says what and why.
//
// on (the default; empty, unknown and the probe's census / preview read as on, with a line): on foot in stereo, while the Manticore Executioner
//   aims (with or without the Scope mod), the dominant eye's scope glass is magnified by the M the game's own FOV gives (a read-only hook on the
//   game's ADS FOV step, 334925 only, after a byte check), capped at 1.70 / 4.07. In native_sharpen's door, after RCAS and before the HUD layer,
//   so the HUD is drawn over the lens and never magnified. Lines: the key, the weapon on a change, one per aim, a summary every 60 s with aims.
//   A lens that cannot be drawn says so once and stands down until the key changes.
// off: nothing -- no hook in the game's code, no GPU call, no line but the key's.
//
// The zoom comes D (260 ms) after the game's FOV on the way up, when the glass is at the eye, and goes with it on the way down; the lens
// follows the barrel's lag with the game's own sway on top (scope_zoom_core.h, delayedZoom, kLagYawGain, kLagYawLift).
#include <cstdint>

struct ID3D11Texture2D;

namespace edvr {

// The frame thread, once a frame (device_hook, after the aiming eye's and the head aim's boundaries it reads).
void scopeZoomFrameBoundary();

// The lens, from native_sharpen's door (the render thread). `src` holds the eye in `region` ({x0, y0, x1, y1}); flipU/flipV say the region is
// stored mirrored. Returns a region-sized texture with the lens drawn (borrowed: valid until the next call for this eye; the door adds its own
// reference before handing it on), or null: nothing to do, or the lens failed (the eye goes on untouched). One atomic load when the lens is off.
ID3D11Texture2D* scopeZoomLens(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV);

}  // namespace edvr
