#pragma once
// On-foot HUD probe, .22: the layout snapshot's D3D11 half (EDVRonFOOT; experimental.on_foot_hud_probe; on_foot_hud_probe.h has the
// story, on_foot_hud_probe_core.h the decisions and the text). A draw in the HUD node's window is replayed once with stream output and
// without any output to the screen; the GPU's answer is read at the window's later draws, without waiting; what it gives goes to the
// frame thread through the probe's queue. OBSERVATION ONLY: the context's geometry shader (with its class instances), stream-output
// targets and topology are put back, and the draw goes through the game's own draw function (`real`), not through EDVR's hooks.
#include "on_foot_hud_probe.h"
#include <cstddef>

struct ID3D11VertexShader;

namespace edvr {

namespace ohp {
struct DrawShape;
}

// From vscreen.cpp's onFootHudProbeNoteDraw, for a draw in the node's window (`shape` is the draw's shape, filled). Reads the earlier
// replays' answers first (own context only), then replays this draw if its form is due.
void onFootHudLayoutSnapshot(ID3D11DeviceContext* ctx, const ohp::DrawShape& shape, char kind, unsigned count, unsigned instances, const DrawArgs& args, bool own,
                             OnFootHudRealDraw real);

// From device_hook.cpp's hookedCreateVS, only while onFootHudProbeKeepsCode(): keeps the shader's code in its private data under
// AnimatedVertexHistory's key (not twice: when the shader holds it already -- the weapon family's -- nothing is added or counted), up to
// 64 MB; a shader made with class linkage is not kept.
void onFootHudLayoutRememberVs(ID3D11VertexShader* shader, const void* bytecode, size_t length, bool hasLinkage);

}  // namespace edvr
