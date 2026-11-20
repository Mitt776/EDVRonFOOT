#pragma once
// EDVRonFOOT desktop eye view (openspec change desktop-eye-mirror): the runtime's hand-over of each eye's final picture -- after the menu,
// the one the headset gets -- to d3d11.dll, which draws the wanted eye into the game's window before the game's Present.
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>

#define EDVR_NATIVE_DESKTOP_VIEW_VERSION_1 1u

struct EdvrNativeDesktopViewRequest {
  uint32_t size, version;
  ID3D11Device* gameDevice;
  uint64_t generation;
};
// noteEye runs on the bound game producer thread, inside the game's Submit, once per finished eye: sequence is the runtime's frame
// sequence, eye the OpenVR eye (0 left, 1 right), texture the eye's final picture on the game's device and bounds its UVUV box in it
// (a flipped axis has min > max). The provider decides whether it wants the eye (the key, the eye): S_OK when it holds a reference to the
// texture until the next Present, S_FALSE when it does not want it (off, the other eye) -- one atomic load, no reference, no GPU work.
// The texture must not change before that Present: the runtime's treatment outputs are stable until the next frame's treatment.
// close is CPU-only, may run on the XR owner, and drops any reference held.
typedef HRESULT(WINAPI *EdvrNativeDesktopViewNoteEye)(void*, uint64_t, uint32_t, ID3D11Texture2D*, const float*);
typedef HRESULT(WINAPI *EdvrNativeDesktopViewClose)(void*);
struct EdvrNativeDesktopViewTable {
  uint32_t size, version;
  void* context;
  EdvrNativeDesktopViewNoteEye noteEye;
  EdvrNativeDesktopViewClose close;
};
extern "C" HRESULT WINAPI edvrAcquireNativeDesktopView(const EdvrNativeDesktopViewRequest*, EdvrNativeDesktopViewTable*);
