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
// TEMPORARY, build 1 of on-foot-scope-zoom only (design decision 3): PAUSE on foot at on records 1.5 s -- a line of numbers a frame and a crop of
// the dominant eye at the door's input -- into edvr_logs\scope_zoom\<time>\, in place of the eye dumps.
#include <cstdint>

struct ID3D11Texture2D;

namespace edvr {

// The frame thread, once a frame (device_hook, after the aiming eye's and the head aim's boundaries it reads).
void scopeZoomFrameBoundary();

// The PAUSE key (hotkey.dump_camera), from device_hook's hotkey tick: at on and on foot, arms the recording and is true -- the press is the
// recording's, and the camera history dumps are skipped. False at off and off foot: PAUSE as before. (TEMPORARY, build 1.)
bool scopeZoomOnPause();

// The recording's frame, from native_sharpen's door at its input (before RCAS and the lens; the render thread). One atomic load when no recording
// runs. (TEMPORARY, build 1.)
void scopeZoomRecord(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV);

// The lens, from native_sharpen's door (the render thread). `src` holds the eye in `region` ({x0, y0, x1, y1}); flipU/flipV say the region is
// stored mirrored. Returns a region-sized texture with the lens drawn (borrowed: valid until the next call for this eye; the door adds its own
// reference before handing it on), or null: nothing to do, or the lens failed (the eye goes on untouched). One atomic load when the lens is off.
ID3D11Texture2D* scopeZoomLens(uint32_t eye, ID3D11Texture2D* src, const uint32_t region[4], bool flipU, bool flipV);

}  // namespace edvr
