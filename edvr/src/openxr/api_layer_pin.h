// OpenXR API layers, pinned for the life of the process.
//
// WHY. An implicit API layer is loaded into every OpenXR instance, EDVR's too,
// and some layers patch D3D11 code process-wide. OpenXR Toolkit 1.3.2 does: at
// session start it detours ID3D11DeviceContext::RSSetViewports with Microsoft
// Detours, which rewrites the system function's own code, so the jump is taken on
// every context, the game's included. On exit EDVR destroys the instance and frees
// the loader (shutdown stages host_instance_destroy, host_loader_free), the loader
// unmaps the layer, and the jump stays: the Steam overlay's Present calls
// RSSetViewports on the game's context one frame later and executes into the
// unmapped DLL. That is the exit crash: 0xC0000005 at an address ending in F780
// (Toolkit's handler sits at RVA 0x8F780), rax - rcx = 0x168 (vtable slot 44),
// 31 ms after host_loader_free, in 65 breadcrumbs and 114 dumps by 2026-11-05
// (docs/shutdown-freelibrary-race-2026-09-21.md, tools/exit_crash_dump.py).
//
// THE FIX IS TO KEEP THE LAYER'S CODE THERE, not to unhook it or to race it. A
// pinned layer stays mapped whatever FreeLibrary the loader calls, so a hook it
// left behind runs live code, which hands the call on to the original. The same
// pin as the graphics DLL's (src/common/module_pin.h) and the runtime module's
// (native_module.cpp, configureModule). The loader and the runtime are NOT pinned:
// nothing breaks when they go, and the runtime's threads should close as designed.
//
// WHERE. Once per instance, right after xrCreateInstance succeeds in
// NativeRuntimeHost::open(): the layers are loaded by then and no session exists.
// A failure only writes its line; the session goes on. Pinning is idempotent.
//
// WHAT. A layer is any module in the process that exports
// xrNegotiateLoaderApiLayerInterface (api_layer_pin_core.h). The modules are
// walked with K32EnumProcessModules (kernel32, no psapi.lib), each pinned through
// the address of that export.
#pragma once

#include <windows.h>
#include <psapi.h>

#include <string>
#include <vector>

#include "api_layer_pin_core.h"

namespace edvr::openxr::layer_pin {

inline std::string moduleLeafUtf8(HMODULE module) {
  wchar_t path[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return "(unknown)";
  const wchar_t* leaf = path;
  for (const wchar_t* p = path; *p; ++p)
    if (*p == L'\\' || *p == L'/') leaf = p + 1;
  char out[MAX_PATH * 3] = {};
  if (!WideCharToMultiByte(CP_UTF8, 0, leaf, -1, out, int(sizeof(out)), nullptr, nullptr)) return "(unknown)";
  return out;
}

// Find every loaded API layer and pin it. Call after xrCreateInstance.
inline Scan pinLoadedApiLayers() {
  Scan scan;
  // The list can grow between two calls; retry with room, then keep what fitted.
  std::vector<HMODULE> modules(512);
  DWORD needed = 0;
  for (int attempt = 0;; ++attempt) {
    const DWORD bytes = DWORD(modules.size() * sizeof(HMODULE));
    if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(), bytes, &needed)) {
      scan.enumError = GetLastError();
      if (!scan.enumError) scan.enumError = 1;
      return scan;
    }
    if (needed <= bytes || attempt == 2) {
      modules.resize((needed <= bytes ? needed : bytes) / sizeof(HMODULE));
      break;
    }
    modules.resize(needed / sizeof(HMODULE) + 64);
  }
  for (HMODULE module : modules) {
    if (!module) continue;
    const FARPROC negotiate = GetProcAddress(module, kLayerExport);
    if (!isApiLayer(reinterpret_cast<const void*>(negotiate))) continue;
    Layer layer;
    layer.name = moduleLeafUtf8(module);
    HMODULE held = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(negotiate), &held) &&
        held) {
      layer.pinned = true;
    } else {
      layer.error = GetLastError();
      if (!layer.error) layer.error = 1;
    }
    scan.layers.push_back(layer);
  }
  return scan;
}

}  // namespace edvr::openxr::layer_pin
