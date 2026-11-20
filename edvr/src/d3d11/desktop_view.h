#pragma once
// EDVRonFOOT desktop eye view (openspec change desktop-eye-mirror). The game's window shows, at the player's choice, one eye's final
// picture -- the one the headset got, with every panel, hologram, the aim dot and the hints -- so a recording made by capturing the game's
// window (OBS "Game Capture") shows what the player saw, at the game's frame rate. experimental.native_vr_desktop_view = off | right | left
// (off by default), experimental.native_vr_desktop_view_fit = crop | whole; both live.
//
// The runtime (openvr_api.dll, native_runtime_host.h finishCapturedEye) hands every finished eye over through the table
// edvrAcquireNativeDesktopView (common\native_desktop_view.h); this side keeps a reference to the wanted one until the game's Present,
// draws it into the swapchain's back buffer just before the real Present (device_hook.cpp hookedPresent), and lets it go. A Present with
// no eye of its own frame leaves the window as the game drew it. With the key off nothing touches the GPU. The headset's picture is
// never read or changed: the draw only samples the eye.
//
// The log: a line every 30 s, "desktop view: ..." (desktop_view_core.h formatLine), off or on, drawn 0 or not.
#include <d3d11.h>
#include <dxgi.h>

namespace edvr {
// The game's swapchain's Present is about to run (VR profile only; never for DXGI_PRESENT_TEST). device: the swapchain's device.
void desktopViewBeforePresent(IDXGISwapChain* swapChain, ID3D11Device* device, UINT flags);
}  // namespace edvr
