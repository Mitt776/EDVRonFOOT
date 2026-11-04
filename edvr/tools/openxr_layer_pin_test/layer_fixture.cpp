// A fake OpenXR API layer for tools/openxr_layer_pin_test: it has the one export
// every layer must have, and refuses the negotiation (never called here).
#include <windows.h>

extern "C" __declspec(dllexport) int xrNegotiateLoaderApiLayerInterface(const void*, const char*, void*) {
    return -1;  // XR_ERROR_VALIDATION_FAILURE
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
