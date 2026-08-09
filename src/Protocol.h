#pragma once
#include <cstdint>

namespace bridge {
inline constexpr wchar_t kProviderToBridgePipeName[] = L"\\\\.\\pipe\\IGCSDOF_ProviderToBridge_v1";
inline constexpr wchar_t kBridgeToProviderPipeName[] = L"\\\\.\\pipe\\IGCSDOF_BridgeToProvider_v1";
inline constexpr std::uint32_t kProtocolVersion = 1;

enum class SessionStartCode : int {
    AllOk = 0,
    CameraNotEnabled = 1,
    CameraPathPlaying = 2,
    AlreadySessionActive = 3,
    CameraFeatureNotAvailable = 4,
    UnknownError = 5
};
}
