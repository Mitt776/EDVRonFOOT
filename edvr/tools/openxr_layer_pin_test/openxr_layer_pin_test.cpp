// Build gate for the OpenXR API-layer pin (src\openxr\api_layer_pin.h, the exit crash at ...F780,
// docs\shutdown-freelibrary-race-2026-09-21.md).
//
// Drives the real Windows half on two fake DLLs: layer_fixture.dll exports xrNegotiateLoaderApiLayerInterface,
// plain_fixture.dll does not. Before they load the process has no layer; after, the scan finds exactly the layer
// and pins it; after two FreeLibrary calls each, the layer is still mapped and the plain DLL is gone; a second
// scan is harmless. Then the pure half: the decision, the file leaf, and the log line for 0, 1 and 2 layers,
// a refused pin and a failed module walk.
//
//   openxr_layer_pin_test <dir holding the two fixtures>
#include "../../src/openxr/api_layer_pin.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace edvr::openxr::layer_pin;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void partProcess(const std::wstring& dir) {
    const std::wstring layerPath = dir + L"\\layer_fixture.dll";
    const std::wstring plainPath = dir + L"\\plain_fixture.dll";

    Scan before = pinLoadedApiLayers();
    check(before.enumError == 0, "the module walk works");
    check(before.layers.empty(), "no layer before the fixtures load");
    check(formatLine(before) == "openxr_layers,found=0,pinned=0", "the line with no layer");

    HMODULE layer = LoadLibraryW(layerPath.c_str());
    HMODULE plain = LoadLibraryW(plainPath.c_str());
    HMODULE plain2 = LoadLibraryW(plainPath.c_str());
    check(layer && plain && plain2, "both fixtures load");
    if (!layer || !plain || !plain2) return;

    Scan scan = pinLoadedApiLayers();
    check(scan.enumError == 0, "the walk works with the fixtures");
    check(scan.layers.size() == 1, "exactly one layer found");
    if (scan.layers.size() == 1) {
        check(scan.layers[0].name == "layer_fixture.dll", "the layer is layer_fixture.dll");
        check(scan.layers[0].pinned && scan.layers[0].error == 0, "the layer is pinned");
    }
    check(formatLine(scan) == "openxr_layers,found=1,pinned=1,list=layer_fixture.dll:pinned", "the line for the fixture");

    Scan again = pinLoadedApiLayers();
    check(again.layers.size() == 1 && again.layers[0].pinned, "a second scan is harmless");

    FreeLibrary(layer);
    FreeLibrary(layer);
    FreeLibrary(plain);
    FreeLibrary(plain2);
    check(GetModuleHandleW(L"layer_fixture.dll") != nullptr, "the pinned layer stays mapped after two FreeLibrary");
    check(GetModuleHandleW(L"plain_fixture.dll") == nullptr, "the plain DLL unloads (the rig can tell)");
    HMODULE stillLayer = GetModuleHandleW(L"layer_fixture.dll");
    check(stillLayer && GetProcAddress(stillLayer, kLayerExport) != nullptr, "the layer's code is still there");
}

void partPure() {
    int x = 0;
    check(isApiLayer(&x), "an export makes a layer");
    check(!isApiLayer(nullptr), "no export, no layer");
    check(std::strcmp(kLayerExport, "xrNegotiateLoaderApiLayerInterface") == 0, "the export's name");

    check(fileLeaf("C:\\Program Files\\OpenXR-Toolkit\\XR_APILAYER_MBUCCHIA_toolkit.dll") ==
              "XR_APILAYER_MBUCCHIA_toolkit.dll", "the leaf drops the path");
    check(fileLeaf("D:/a/b/OpenKneeboard-OpenXR.dll") == "OpenKneeboard-OpenXR.dll", "forward slashes");
    check(fileLeaf("plain.dll") == "plain.dll", "a bare name");
    check(fileLeaf("x,y|z:w.dll") == "x_y_z_w.dll", "the line's separators made harmless");
    check(fileLeaf("C:\\dir\\") == "(unknown)", "an empty leaf");

    Scan one;
    one.layers.push_back({"C:\\Program Files\\OpenXR-Toolkit\\XR_APILAYER_MBUCCHIA_toolkit.dll", true, 0});
    check(formatLine(one) == "openxr_layers,found=1,pinned=1,list=XR_APILAYER_MBUCCHIA_toolkit.dll:pinned",
          "one layer, the design's line");

    Scan two;
    two.layers.push_back({"XR_APILAYER_MBUCCHIA_toolkit.dll", true, 0});
    two.layers.push_back({"OpenKneeboard-OpenXR.dll", true, 0});
    check(formatLine(two) ==
              "openxr_layers,found=2,pinned=2,list=XR_APILAYER_MBUCCHIA_toolkit.dll:pinned|OpenKneeboard-OpenXR.dll:pinned",
          "two layers, both named");

    Scan refused;
    refused.layers.push_back({"a.dll", true, 0});
    refused.layers.push_back({"b.dll", false, 5});
    check(formatLine(refused) == "openxr_layers,found=2,pinned=1,list=a.dll:pinned|b.dll:error=5", "a refused pin");

    Scan walk;
    walk.enumError = 6;
    check(formatLine(walk) == "openxr_layers,found=0,pinned=0,enum_error=6", "a failed walk");

    check(formatLine(Scan{}) == "openxr_layers,found=0,pinned=0", "no layers, no list");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: openxr_layer_pin_test <fixture dir>\n");
        return 2;
    }
    partProcess(argv[1]);
    partPure();
    std::printf("%s: openxr_layer_pin_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
