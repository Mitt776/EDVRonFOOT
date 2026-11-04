// An ordinary DLL for tools/openxr_layer_pin_test: an export, but not a layer's.
#include <windows.h>

extern "C" __declspec(dllexport) int plainFixtureExport() { return 7; }

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
