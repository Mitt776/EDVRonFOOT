#pragma once
// The on-foot scope zoom probe (EDVRonFOOT; experimental.native_vr_on_foot_scope_zoom; openspec change scope-zoom-probe). TEMPORARY: it goes
// away with the permanent lens. scope_zoom_probe_core.h is the pure half and says what and why.
//
// off (the default): nothing -- no hook in the game's code, no GPU call, no line but the key's.
// census: on foot, the lines -- the weapon in hand and its mods, each aim's LT edges and the game's flag with its delay, the camera FOV the game
//   sets while aiming (a read-only hook on the game's ADS FOV step, 334925 only, after a byte check), a snapshot of both eyes on PAUSE, and a
//   summary every 30 s.
// preview: the same, and the lens: in the dominant eye only, on foot in stereo, while a weapon from the table aims, a circle of 5 degrees around
//   the aiming centre is magnified by M (ramped with the dominant eye's blend). In native_sharpen's door, after RCAS and before the HUD layer,
//   so the HUD is drawn over the lens and never magnified. A lens that cannot be drawn says so once and stands down until the key changes.
#include <cstdint>

struct ID3D11Texture2D;

namespace edvr {

// The frame thread, once a frame (device_hook, after the aiming eye's boundary it reads).
void scopeZoomProbeFrameBoundary();

// The PAUSE key (hotkey.dump_camera), from device_hook's hotkey tick: at census/preview on foot, the eyes' snapshot (images only, no census)
// and its line, and true: the press is the probe's, and the camera history dumps are skipped. False at off and off foot: PAUSE as before.
bool scopeZoomProbeOnPause();

// The lens, from native_sharpen's door (the render thread). `src` holds the eye in `region` ({x0, y0, x1, y1}); flipU/flipV say the region is
// stored mirrored. Returns a region-sized texture with the lens drawn (borrowed: valid until the next call for this eye; the door adds its own
// reference before handing it on), or null: nothing to do, or the lens failed (the eye goes on untouched). One atomic load when the lens is off.
ID3D11Texture2D* scopeZoomLens(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV);

}  // namespace edvr
