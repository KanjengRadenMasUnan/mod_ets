// dxgi.cpp
#include <windows.h>
#include <stdio.h>

static HMODULE g_realDxgi = NULL;

static HMODULE LoadRealDxgi() {
    if (!g_realDxgi) {
        char sysDir[MAX_PATH];
        GetSystemDirectoryA(sysDir, MAX_PATH);
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\dxgi.dll", sysDir);
        g_realDxgi = LoadLibraryA(path);
    }
    return g_realDxgi;
}

#define FORWARD(func, ...) \
    typedef decltype(&func) FuncType; \
    FuncType realFunc = (FuncType)GetProcAddress(LoadRealDxgi(), #func); \
    return realFunc ? realFunc(__VA_ARGS__) : E_FAIL;

extern "C" {
    __declspec(dllexport) HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
        FORWARD(CreateDXGIFactory, riid, ppFactory);
    }
    __declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
        FORWARD(CreateDXGIFactory1, riid, ppFactory);
    }
    __declspec(dllexport) HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
        FORWARD(CreateDXGIFactory2, Flags, riid, ppFactory);
    }
    __declspec(dllexport) HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
        FORWARD(DXGIGetDebugInterface1, Flags, riid, pDebug);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(hModule);
    return TRUE;
}
