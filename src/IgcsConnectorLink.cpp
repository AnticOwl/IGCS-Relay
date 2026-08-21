#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include "IgcsConnectorLink.h"
#include "BridgeState.h"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <string>

namespace bridge {
namespace {
using ConnectFn = bool (*)();
using BufferFn = unsigned char *(*)();

struct DofConsumer {
    ConnectFn connect{};
    BufferFn buffer{};
    unsigned char *target{};
    bool detected{};
};

DofConsumer g_igcsDof{};
DofConsumer g_parallax{};
DofBackend g_selectedBackend = DofBackend::IgcsDof;

std::wstring moduleFileName(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};

    std::wstring fileName(path, length);
    const auto slash = fileName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) fileName.erase(0, slash + 1);

    std::transform(
        fileName.begin(),
        fileName.end(),
        fileName.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); }
    );
    return fileName;
}

bool classifyBackend(HMODULE module, DofBackend &backend) {
    const std::wstring fileName = moduleFileName(module);
    if (fileName.empty()) return false;

    if (fileName.find(L"martysmods_parallaxdof") != std::wstring::npos) {
        backend = DofBackend::Parallax;
        return true;
    }

    if (fileName.find(L"igcsconnector") != std::wstring::npos) {
        backend = DofBackend::IgcsDof;
        return true;
    }

    return false;
}

DofConsumer &consumerFor(DofBackend backend) {
    return backend == DofBackend::Parallax ? g_parallax : g_igcsDof;
}

const DofConsumer &consumerFor(DofBackend backend, int) {
    return backend == DofBackend::Parallax ? g_parallax : g_igcsDof;
}

void clearConnection(DofConsumer &consumer) {
    consumer.connect = nullptr;
    consumer.buffer = nullptr;
    consumer.target = nullptr;
    consumer.detected = false;
}

void connectConsumer(DofConsumer &consumer) {
    if (consumer.target || !consumer.connect || !consumer.buffer) return;

    if (consumer.connect()) {
        consumer.target = consumer.buffer();
    }
}

void updateActiveConnectionState() {
    state().igcsConnected = consumerFor(g_selectedBackend).target != nullptr;
}
}

const char *dofBackendDisplayName(DofBackend backend) {
    switch (backend) {
    case DofBackend::Parallax:
        return "Parallax DOF";
    case DofBackend::IgcsDof:
    default:
        return "IGCSDOF";
    }
}

DofBackend selectedDofBackend() {
    return g_selectedBackend;
}

void selectDofBackend(DofBackend backend) {
    auto &s = state();
    if (s.sessionActive.load() || backend == g_selectedBackend) return;

    g_selectedBackend = backend;
    updateActiveConnectionState();

    // Both consumers stay connected. The next publish switches cameraEnabled
    // atomically: selected consumer gets live camera data, the other gets a
    // disabled camera. No reconnect and no engine/provider math changes.
}

bool selectDofBackendFromCallerAddress(const void *address) {
    if (!address || state().sessionActive.load()) return false;

    HMODULE callerModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &callerModule
        )) {
        return false;
    }

    DofBackend backend{};
    if (!classifyBackend(callerModule, backend)) return false;

    g_selectedBackend = backend;
    updateActiveConnectionState();
    return true;
}

bool isDofBackendDetected(DofBackend backend) {
    return consumerFor(backend, 0).detected;
}

bool isDofBackendConnected(DofBackend backend) {
    return consumerFor(backend, 0).target != nullptr;
}

void resetIgcsConnectorLink() {
    clearConnection(g_igcsDof);
    clearConnection(g_parallax);
    state().igcsConnected = false;
}

void refreshIgcsConnectorLink(bool force) {
    auto &s = state();

    // Critical ordering rule: do not tell DOF consumers about this bridge
    // until the external provider has supplied HELLO + a valid camera and both
    // transport channels are connected.
    if (!s.providerReady.load()) {
        resetIgcsConnectorLink();
        return;
    }

    if (force) resetIgcsConnectorLink();

    // If both consumers are already connected there is nothing to rescan.
    if (g_igcsDof.target && g_parallax.target) {
        updateActiveConnectionState();
        return;
    }

    HMODULE modules[512]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        updateActiveConnectionState();
        return;
    }

    // Detection is refreshed on every scan. Existing live targets are kept
    // unless a forced handshake reset was requested.
    g_igcsDof.detected = false;
    g_parallax.detected = false;

    for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
        auto connect = reinterpret_cast<ConnectFn>(
            GetProcAddress(modules[i], "connectFromCameraTools")
        );
        auto buffer = reinterpret_cast<BufferFn>(
            GetProcAddress(modules[i], "getDataFromCameraToolsBuffer")
        );
        if (!connect || !buffer) continue;

        DofBackend backend{};
        if (!classifyBackend(modules[i], backend)) continue;

        DofConsumer &consumer = consumerFor(backend);
        consumer.detected = true;
        consumer.connect = connect;
        consumer.buffer = buffer;
    }

    // Connect every compatible consumer that is present. Selection only
    // controls which one receives an enabled camera, so switching is instant.
    connectConsumer(g_igcsDof);
    connectConsumer(g_parallax);
    updateActiveConnectionState();
}

void publishCameraData() {
    auto &s = state();
    if (!s.providerReady.load()) {
        resetIgcsConnectorLink();
        return;
    }

    if (s.igcsHandshakePending.exchange(false)) {
        refreshIgcsConnectorLink(true);
    } else if (!g_igcsDof.target || !g_parallax.target) {
        // Re-scan while one consumer is missing. ReShade loads addons at
        // startup, so this normally stops immediately once both are found.
        refreshIgcsConnectorLink(false);
    }

    DofConsumer &active = consumerFor(g_selectedBackend);
    updateActiveConnectionState();
    if (!active.target) return;

    CameraToolsData live{};
    {
        std::scoped_lock lock(s.mutex);
        live = s.camera;
    }
    if (!s.cameraValid) live.cameraEnabled = 0;

    CameraToolsData inactive = live;
    inactive.cameraEnabled = 0;
    inactive.cameraMovementLocked = 1;

    // Both buffers are kept current. Only the selected backend is allowed to
    // see an enabled camera; the other remains connected and can be activated
    // on the next frame without a new handshake.
    if (g_igcsDof.target) {
        const CameraToolsData &data =
            g_selectedBackend == DofBackend::IgcsDof ? live : inactive;
        std::memcpy(g_igcsDof.target, &data, sizeof(data));
    }

    if (g_parallax.target) {
        const CameraToolsData &data =
            g_selectedBackend == DofBackend::Parallax ? live : inactive;
        std::memcpy(g_parallax.target, &data, sizeof(data));
    }
}
}
