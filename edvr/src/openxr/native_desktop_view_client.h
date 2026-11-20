#pragma once
// EDVRonFOOT desktop eye view (openspec change desktop-eye-mirror): the runtime's side of edvrAcquireNativeDesktopView
// (common\native_desktop_view.h). Every finished eye -- the picture the headset gets, after the menu -- is offered to d3d11.dll, which
// keeps the one the player wants until the game's Present. Optional: a d3d11.dll without the table leaves the view off and the runtime
// exactly as before. A refusal never fails the eye.
#include "../common/native_desktop_view.h"
#include "../openvr/compat/openvr_v0_9_20.h"
#include <wrl/client.h>

namespace edvr::openxr {
// The XR owner serializes calls. Acquire and note run on the bound producer; close is CPU only.
class NativeDesktopViewClient final {
 public:
  HRESULT acquire(HMODULE provider, ID3D11Device* device, uint64_t generation) {
    if (table_.context) return E_PENDING;
    if (!provider || !device || !generation) return E_INVALIDARG;
    const auto entry = GetProcAddress(provider, "edvrAcquireNativeDesktopView");
    if (!owned(provider, entry)) return E_NOINTERFACE;
    EdvrNativeDesktopViewRequest request{sizeof(request), EDVR_NATIVE_DESKTOP_VIEW_VERSION_1, device, generation};
    EdvrNativeDesktopViewTable candidate{sizeof(candidate), EDVR_NATIVE_DESKTOP_VIEW_VERSION_1};
    const auto result = reinterpret_cast<decltype(&edvrAcquireNativeDesktopView)>(entry)(&request, &candidate);
    if (result != S_OK) return FAILED(result) ? result : E_NOINTERFACE;
    if (candidate.size != sizeof(candidate) || candidate.version != EDVR_NATIVE_DESKTOP_VIEW_VERSION_1 ||
        !candidate.context || !owned(provider, candidate.noteEye) || !owned(provider, candidate.close)) return E_NOINTERFACE;
    table_ = candidate;
    return S_OK;
  }
  bool acquired() const { return table_.context != nullptr; }
  // S_OK held, S_FALSE not wanted, a failure counted by the caller and otherwise ignored.
  HRESULT note(uint64_t sequence, unsigned eye, ID3D11Texture2D* texture, const vr::VRTextureBounds_t& bounds) {
    if (!acquired() || !texture) return S_FALSE;
    const float box[4] = {bounds.uMin, bounds.vMin, bounds.uMax, bounds.vMax};
    return table_.noteEye(table_.context, sequence, eye, texture, box);
  }
  HRESULT close() {
    if (!acquired()) return S_FALSE;
    const auto result = table_.close(table_.context);
    if (SUCCEEDED(result)) table_ = {};
    return result;
  }
 private:
  template<class T> static bool owned(HMODULE provider, T address) {
    if (!address) return false;
    HMODULE implementation = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &implementation) && implementation == provider;
  }
  EdvrNativeDesktopViewTable table_{};
};
} // namespace edvr::openxr
