#pragma once
// EDVRonFOOT desktop eye view (openspec changes desktop-eye-mirror, desktop-eye-steady): the runtime's hand-over of each eye's final
// picture -- after the menu, the one the headset gets -- to d3d11.dll, which draws the wanted eye into the game's window before the
// game's Present.
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>

// Version 2 (desktop-eye-steady): noteEye also carries the eye's pose and the angles its pixels hold. Both DLLs ship in one installer;
// a pair of different versions hands out no table and the view stays off.
#define EDVR_NATIVE_DESKTOP_VIEW_VERSION_2 2u

struct EdvrNativeDesktopViewRequest {
  uint32_t size, version;
  ID3D11Device* gameDevice;
  uint64_t generation;
};
// The picture's projection: the orientation of the eye it was rendered from (an XrQuaternionf, x y z w, from the eye's view space to
// the tracking space the runtime locates the head in; neither the stick's turn nor the ship is in it) and the angles its pixels -- the
// bounds' box -- hold (an XrFovf, radians: left and down negative). valid 0: no pose for this frame (the eye is drawn unturned).
struct EdvrNativeDesktopViewPose {
  float orientation[4];
  float fov[4];   // left, right, up, down
  uint32_t valid;
};
// noteEye runs on the bound game producer thread, inside the game's Submit, once per finished eye: sequence is the runtime's frame
// sequence, eye the OpenVR eye (0 left, 1 right), texture the eye's final picture on the game's device and bounds its UVUV box in it
// (a flipped axis has min > max), pose its projection (never null). The provider decides whether it wants the eye (the key, the eye):
// S_OK when it holds a reference to the texture until the next Present, S_FALSE when it does not want it (off, the other eye) -- one
// atomic load, no reference, no GPU work. The texture must not change before that Present: the runtime's treatment outputs are stable
// until the next frame's treatment. close is CPU-only, may run on the XR owner, and drops any reference held.
typedef HRESULT(WINAPI *EdvrNativeDesktopViewNoteEye)(void*, uint64_t, uint32_t, ID3D11Texture2D*, const float*,
                                                      const EdvrNativeDesktopViewPose*);
typedef HRESULT(WINAPI *EdvrNativeDesktopViewClose)(void*);
// noteMask: the lens mask of one eye, the same hidden area mesh the game is given (GetHiddenAreaMesh): vertexCount vertices (a multiple
// of three; zero for no mask), xy pairs in fractions of the eye's picture (x right, y down). Copied; sent only when it changes. Producer
// thread, like noteEye. Only the zoom fit and the steady margin read it.
typedef HRESULT(WINAPI *EdvrNativeDesktopViewNoteMask)(void*, uint32_t, const float*, uint32_t);
struct EdvrNativeDesktopViewTable {
  uint32_t size, version;
  void* context;
  EdvrNativeDesktopViewNoteEye noteEye;
  EdvrNativeDesktopViewNoteMask noteMask;
  EdvrNativeDesktopViewClose close;
};
extern "C" HRESULT WINAPI edvrAcquireNativeDesktopView(const EdvrNativeDesktopViewRequest*, EdvrNativeDesktopViewTable*);
