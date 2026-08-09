#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include "IgcsConnectorLink.h"
#include "BridgeState.h"
#include <cstring>

namespace bridge {
namespace {
using ConnectFn = bool (*)();
using BufferFn = unsigned char *(*)();
ConnectFn g_connect = nullptr;
BufferFn g_buffer = nullptr;
unsigned char *g_target = nullptr;
}

void resetIgcsConnectorLink() {
    g_connect = nullptr;
    g_buffer = nullptr;
    g_target = nullptr;
    state().igcsConnected = false;
}

void refreshIgcsConnectorLink(bool force) {
    auto &s = state();

    // Critical ordering rule: do not tell IGCS Connector about this bridge
    // until the external provider has supplied HELLO + a valid camera and both
    // transport channels are connected.
    if (!s.providerReady.load()) {
        resetIgcsConnectorLink();
        return;
    }

    if (force) resetIgcsConnectorLink();
    if (g_target) {
        s.igcsConnected = true;
        return;
    }

    HMODULE modules[512]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return;

    for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
        auto connect = reinterpret_cast<ConnectFn>(GetProcAddress(modules[i], "connectFromCameraTools"));
        auto buffer = reinterpret_cast<BufferFn>(GetProcAddress(modules[i], "getDataFromCameraToolsBuffer"));
        if (!connect || !buffer) continue;

        // connectFromCameraTools is intentionally called only after ProviderReady.
        // This recreates the successful 14/06 ordering where launching the Lua
        // provider was the signal that all components were present.
        if (connect()) {
            g_connect = connect;
            g_buffer = buffer;
            g_target = buffer();
        }
        break;
    }

    s.igcsConnected = g_target != nullptr;
}

void publishCameraData() {
    auto &s = state();
    if (!s.providerReady.load()) {
        resetIgcsConnectorLink();
        return;
    }

    if (s.igcsHandshakePending.exchange(false)) {
        refreshIgcsConnectorLink(true);
    } else if (!g_target) {
        refreshIgcsConnectorLink(false);
    }

    if (!g_target) return;

    CameraToolsData copy{};
    {
        std::scoped_lock lock(s.mutex);
        copy = s.camera;
    }
    if (!s.cameraValid) copy.cameraEnabled = 0;
    std::memcpy(g_target, &copy, sizeof(copy));
}
}
