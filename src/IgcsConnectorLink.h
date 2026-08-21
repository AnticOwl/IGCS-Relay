#pragma once

namespace bridge {
enum class DofBackend {
    IgcsDof = 0,
    Parallax = 1
};

const char *dofBackendDisplayName(DofBackend backend);
DofBackend selectedDofBackend();
void selectDofBackend(DofBackend backend);
bool selectDofBackendFromCallerAddress(const void *address);
bool isDofBackendDetected(DofBackend backend);
bool isDofBackendConnected(DofBackend backend);

void refreshIgcsConnectorLink(bool force = false);
void resetIgcsConnectorLink();
void publishCameraData();
}
