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

ConnectFn g_connect = nullptr;
BufferFn g_buffer = nullptr;
unsigned char *g_target = nullptr;
DofBackend g_selectedBackend = DofBackend::IgcsDof;
bool g_igcsDofDetected = false;
bool g_parallaxDetected = false;

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

void setDetected(DofBackend backend) {
    if (backend == DofBackend::Parallax) {
        g_parallaxDetected = true;
    } else {
        g_igcsDofDetected = true;
    }
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
    resetIgcsConnectorLink();
    s.igcsHandshakePending = true;
}

bool isDofBackendDetected(DofBackend backend) {
    return backend == DofBackend::Parallax
        ? g_parallaxDetected
        : g_igcsDofDetected;
}

void resetIgcsConnectorLink() {
    g_connect = nullptr;
    g_buffer = nullptr;
    g_target = nullptr;
    state().igcsConnected = false;
}

void refreshIgcsConnectorLink(bool force) {
    auto &s = state();

    // Critical ordering rule: do not tell a DOF consumer about this bridge
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

    g_igcsDofDetected = false;
    g_parallaxDetected = false;

    HMODULE selectedModule = nullptr;
    ConnectFn selectedConnect = nullptr;
    BufferFn selectedBuffer = nullptr;

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

        setDetected(backend);
        if (backend != g_selectedBackend || selectedModule != nullptr) continue;

        selectedModule = modules[i];
        selectedConnect = connect;
        selectedBuffer = buffer;
    }

    if (selectedModule && selectedConnect && selectedBuffer) {
        // connectFromCameraTools is intentionally called only after ProviderReady.
        // Switching backends only changes which consumer receives CameraToolsData;
        // provider transport and all camera math remain untouched.
        if (selectedConnect()) {
            g_connect = selectedConnect;
            g_buffer = selectedBuffer;
            g_target = selectedBuffer();
        }
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
