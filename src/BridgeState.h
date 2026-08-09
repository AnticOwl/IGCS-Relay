#pragma once
#include "CameraToolsData.h"
#include "EngineMath.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace bridge {
struct State {
    std::atomic_bool running{false};
    std::atomic_bool providerInputConnected{false};
    std::atomic_bool providerOutputConnected{false};
    std::atomic_bool providerConnected{false};
    std::atomic_bool helloReceived{false};
    std::atomic_bool providerReady{false};
    std::atomic_bool igcsHandshakePending{false};
    std::atomic_bool igcsConnected{false};
    std::atomic_bool cameraValid{false};
    std::atomic_bool sessionActive{false};
    std::atomic_bool reconnectRequested{false};
    std::mutex mutex;
    std::condition_variable sessionBaseCv;
    CameraToolsData camera{};
    RawCameraData rawCamera{};
    RawCameraData sessionBaseRawCamera{};
    EngineProfile engineProfile{EngineProfile::Normalized};
    CameraInputMode cameraInputMode{CameraInputMode::NormalizedBasis};
    bool sessionBaseValid{false};
    bool sessionBaseRefreshPending{false};
    std::string provider{"Not connected"};
    std::string game{"Not available"};
    std::string engine{"Not available"};
    std::string engineVersion{};
    std::string lastError{};
    std::chrono::steady_clock::time_point lastCameraUpdate{};
};
State &state();
void refreshProviderConnectedState();
void refreshProviderReadyState();
}
