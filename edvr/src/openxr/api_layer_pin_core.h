// The pure half of the OpenXR API-layer pin (api_layer_pin.h): which module is a
// layer, and the one log line that names what was found. No Windows here, so
// tools/openxr_layer_pin_test drives it on lists.
//
// Why the layers are pinned at all: api_layer_pin.h and
// docs/shutdown-freelibrary-race-2026-09-21.md (the exit crash at ...F780).
#pragma once

#include <string>
#include <vector>

namespace edvr::openxr::layer_pin {

// Every OpenXR API layer exports this, by the loader specification; runtimes
// export xrNegotiateLoaderRuntimeInterface instead, and nothing else has it.
inline constexpr const char* kLayerExport = "xrNegotiateLoaderApiLayerInterface";

// A module is a layer when it exports kLayerExport (its address, or null).
inline bool isApiLayer(const void* negotiateExport) { return negotiateExport != nullptr; }

struct Layer {
  std::string name;          // the file name only, never the path
  bool pinned = false;
  unsigned long error = 0;   // GetLastError of a refused pin
};

struct Scan {
  std::vector<Layer> layers;
  unsigned long enumError = 0;  // the module walk itself failed
};

// The file name of a path, with the line's own separators made harmless.
inline std::string fileLeaf(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
  for (char& c : leaf)
    if (c == ',' || c == '|' || c == ':' || c == '\r' || c == '\n') c = '_';
  return leaf.empty() ? std::string("(unknown)") : leaf;
}

// openxr_layers,found=1,pinned=1,list=XR_APILAYER_MBUCCHIA_toolkit.dll:pinned
// openxr_layers,found=2,pinned=1,list=a.dll:pinned|b.dll:error=5
// openxr_layers,found=0,pinned=0
// openxr_layers,found=0,pinned=0,enum_error=6
inline std::string formatLine(const Scan& scan) {
  unsigned pinned = 0;
  for (const Layer& l : scan.layers) pinned += l.pinned ? 1u : 0u;
  std::string line = "openxr_layers,found=" + std::to_string(scan.layers.size()) +
                     ",pinned=" + std::to_string(pinned);
  if (scan.enumError) line += ",enum_error=" + std::to_string(scan.enumError);
  if (!scan.layers.empty()) {
    line += ",list=";
    for (size_t i = 0; i < scan.layers.size(); ++i) {
      const Layer& l = scan.layers[i];
      if (i) line += '|';
      line += fileLeaf(l.name);
      line += l.pinned ? std::string(":pinned") : ":error=" + std::to_string(l.error);
    }
  }
  return line;
}

}  // namespace edvr::openxr::layer_pin
