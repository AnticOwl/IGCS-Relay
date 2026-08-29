#include <windows.h>
#include "NamedPipeServer.h"
#include "BridgeState.h"
#include "Protocol.h"
#include "EngineMath.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bridge {
namespace {
std::thread g_inputThread;
std::thread g_outputThread;

std::mutex g_handleMutex;
HANDLE g_inputPipe = INVALID_HANDLE_VALUE;
HANDLE g_outputPipe = INVALID_HANDLE_VALUE;

std::mutex g_queueMutex;
std::condition_variable g_queueCv;
std::deque<std::string> g_outbound;

std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delimiter)) out.push_back(part);
    return out;
}

float number(const std::string &s, float fallback = 0.0f) {
    char *end = nullptr;
    const float value = std::strtof(s.c_str(), &end);
    return end != s.c_str() ? value : fallback;
}

void setError(const std::string &message) {
    auto &s = state();
    std::scoped_lock lock(s.mutex);
    s.lastError = message;
}

void clearInputState() {
    auto &s = state();
    s.providerInputConnected = false;
    s.helloReceived = false;
    s.cameraValid = false;
    {
        std::scoped_lock lock(s.mutex);
        s.sessionBaseValid = false;
        s.sessionBaseRefreshPending = false;
    }
    s.sessionBaseCv.notify_all();
    refreshProviderConnectedState();
}

void clearOutputState() {
    auto &s = state();
    s.providerOutputConnected = false;
    s.sessionActive = false;
    refreshProviderConnectedState();
}

void parseHello(const std::vector<std::string> &parts) {
    auto &s = state();
    {
    std::scoped_lock lock(s.mutex);
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto pos = parts[i].find('=');
        if (pos == std::string::npos) continue;
        const auto key = parts[i].substr(0, pos);
        const auto value = parts[i].substr(pos + 1);
        if (key == "Provider") s.provider = value;
        else if (key == "Game") s.game = value;
        else if (key == "Engine") {
            s.engine = value;
            s.engineProfile = engineProfileFromTag(value);
        }
        else if (key == "EngineVersion") s.engineVersion = value;
        else if (key == "CameraMode") {
            s.cameraInputMode =
                (value == "RAW" || value == "Raw" || value == "raw")
                    ? CameraInputMode::RawEuler
                    : CameraInputMode::NormalizedBasis;
        }
    }
    }
    s.helloReceived = true;
    refreshProviderReadyState();
}

void parseCamera(const std::vector<std::string> &p) {
    if (p.size() < 24) return;

    CameraToolsData data{};
    const bool valid = number(p[1]) != 0.0f;
    data.cameraEnabled = static_cast<std::uint8_t>(number(p[2]) != 0.0f);
    data.cameraMovementLocked = static_cast<std::uint8_t>(number(p[3]) != 0.0f);
    data.coordinates.values[0] = number(p[4]);
    data.coordinates.values[1] = number(p[5]);
    data.coordinates.values[2] = number(p[6]);
    data.lookQuaternion.values[0] = number(p[7]);
    data.lookQuaternion.values[1] = number(p[8]);
    data.lookQuaternion.values[2] = number(p[9]);
    data.lookQuaternion.values[3] = number(p[10], 1.0f);
    data.rotationMatrixRightVector.values[0] = number(p[11]);
    data.rotationMatrixRightVector.values[1] = number(p[12]);
    data.rotationMatrixRightVector.values[2] = number(p[13]);
    data.rotationMatrixUpVector.values[0] = number(p[14]);
    data.rotationMatrixUpVector.values[1] = number(p[15]);
    data.rotationMatrixUpVector.values[2] = number(p[16]);
    data.rotationMatrixForwardVector.values[0] = number(p[17]);
    data.rotationMatrixForwardVector.values[1] = number(p[18]);
    data.rotationMatrixForwardVector.values[2] = number(p[19]);
    data.pitch = number(p[20]);
    data.yaw = number(p[21]);
    data.roll = number(p[22]);
    data.fov = number(p[23], 70.0f);

    auto &s = state();
    {
        std::scoped_lock lock(s.mutex);
        s.camera = data;
        s.lastCameraUpdate = std::chrono::steady_clock::now();
        s.lastError.clear();
    }
    s.cameraValid = valid;
    refreshProviderReadyState();
}

void parseCameraRaw(const std::vector<std::string> &p) {
    if (p.size() < 11) return;

    RawCameraData raw{};
    raw.valid = number(p[1]) != 0.0f;
    raw.enabled = number(p[2]) != 0.0f;
    raw.locked = number(p[3]) != 0.0f;
    raw.x = number(p[4]);
    raw.y = number(p[5]);
    raw.z = number(p[6]);
    raw.pitch = number(p[7]);
    raw.yaw = number(p[8]);
    raw.roll = number(p[9]);
    raw.fov = number(p[10], 70.0f);

    auto &s = state();
    const CameraToolsData data = buildCameraToolsData(raw, s.engineProfile);
    bool freshSessionBaseCaptured = false;
    {
        std::scoped_lock lock(s.mutex);
        s.rawCamera = raw;
        s.camera = data;
        s.cameraInputMode = CameraInputMode::RawEuler;
        s.lastCameraUpdate = std::chrono::steady_clock::now();
        s.lastError.clear();

        if ((s.engineProfile == EngineProfile::IdTech7 ||
             s.engineProfile == EngineProfile::Northlight ||
             s.engineProfile == EngineProfile::Rage) &&
            s.sessionBaseRefreshPending &&
            raw.valid &&
            raw.locked) {
            s.sessionBaseRawCamera = raw;
            s.sessionBaseValid = true;
            s.sessionBaseRefreshPending = false;
            freshSessionBaseCaptured = true;
        }
    }

    if (freshSessionBaseCaptured) {
        s.sessionBaseCv.notify_all();
    }

    s.cameraValid = raw.valid;
    refreshProviderReadyState();
}

void handleLine(const std::string &line) {
    const auto parts = split(line, '|');
    if (parts.empty()) return;

    if (parts[0] == "HELLO") {
        parseHello(parts);
    } else if (parts[0] == "CAMERA") {
        parseCamera(parts);
    } else if (parts[0] == "CAMERA_RAW") {
        parseCameraRaw(parts);
    } else if (parts[0] == "CAMERA_INVALID") {
        state().cameraValid = false;
        refreshProviderReadyState();
    } else if (parts[0] == "ACK" && parts.size() > 1) {
        if (parts[1] == "SESSION_BEGIN") state().sessionActive = true;
        else if (parts[1] == "SESSION_END") state().sessionActive = false;
    }
}

void closePipe(HANDLE &pipe) {
    if (pipe == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    pipe = INVALID_HANDLE_VALUE;
}

bool writeFramed(HANDLE pipe, const std::string &line) {
    const std::string framed = line + "\n";
    DWORD written = 0;
    return WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr) != FALSE
        && written == framed.size();
}

void processIncoming(HANDLE pipe, std::string &pending) {
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
        throw GetLastError();
    }

    pending.append(buffer.data(), read);
    std::size_t eol = 0;
    while ((eol = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, eol);
        pending.erase(0, eol + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) handleLine(line);
    }
}

HANDLE createServerPipe(const wchar_t *name) {
    return CreateNamedPipeW(
        name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        64 * 1024,
        64 * 1024,
        0,
        nullptr);
}

bool waitForClient(HANDLE pipe) {
    return ConnectNamedPipe(pipe, nullptr) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
}

void inputLoop() {
    auto &s = state();

    while (s.running) {
        HANDLE pipe = createServerPipe(kProviderToBridgePipeName);
        if (pipe == INVALID_HANDLE_VALUE) {
            setError("Create ProviderToBridge pipe failed: " + std::to_string(GetLastError()));
            Sleep(1000);
            continue;
        }

        {
            std::scoped_lock lock(g_handleMutex);
            g_inputPipe = pipe;
        }

        if (!waitForClient(pipe)) {
            setError("ProviderToBridge ConnectNamedPipe failed: " + std::to_string(GetLastError()));
            {
                std::scoped_lock lock(g_handleMutex);
                closePipe(g_inputPipe);
            }
            continue;
        }

        s.providerInputConnected = true;
        refreshProviderConnectedState();
        std::string pending;

        while (s.running) {
            if (s.reconnectRequested.load()) break;
            try {
                processIncoming(pipe, pending);
            } catch (DWORD error) {
                if (s.running) setError("ProviderToBridge read failed: " + std::to_string(error));
                break;
            }
        }

        clearInputState();
        {
            std::scoped_lock lock(g_handleMutex);
            closePipe(g_inputPipe);
        }
    }
}

void outputLoop() {
    auto &s = state();

    while (s.running) {
        HANDLE pipe = createServerPipe(kBridgeToProviderPipeName);
        if (pipe == INVALID_HANDLE_VALUE) {
            setError("Create BridgeToProvider pipe failed: " + std::to_string(GetLastError()));
            Sleep(1000);
            continue;
        }

        {
            std::scoped_lock lock(g_handleMutex);
            g_outputPipe = pipe;
        }

        if (!waitForClient(pipe)) {
            setError("BridgeToProvider ConnectNamedPipe failed: " + std::to_string(GetLastError()));
            {
                std::scoped_lock lock(g_handleMutex);
                closePipe(g_outputPipe);
            }
            continue;
        }

        s.providerOutputConnected = true;
        refreshProviderConnectedState();

        {
            std::scoped_lock lock(g_queueMutex);
            g_outbound.clear();
            g_outbound.emplace_back("WELCOME|Protocol=1|Transport=DualPipe");
        }
        g_queueCv.notify_one();

        bool healthy = true;
        while (s.running && healthy) {
            if (s.reconnectRequested.load()) break;

            std::deque<std::string> local;
            {
                std::unique_lock lock(g_queueMutex);
                g_queueCv.wait(lock, [&] {
                    return !g_outbound.empty() || !s.running.load() || s.reconnectRequested.load();
                });
                local.swap(g_outbound);
            }

            for (const auto &line : local) {
                if (!writeFramed(pipe, line)) {
                    if (s.running) setError("BridgeToProvider write failed: " + std::to_string(GetLastError()));
                    healthy = false;
                    break;
                }
            }
        }

        clearOutputState();
        {
            std::scoped_lock lock(g_handleMutex);
            closePipe(g_outputPipe);
        }
        {
            std::scoped_lock lock(g_queueMutex);
            g_outbound.clear();
        }

        s.reconnectRequested = false;
    }
}
} // namespace

bool sendLine(const std::string &line) {
    auto &s = state();
    if (!s.providerOutputConnected || !s.running) return false;

    {
        std::scoped_lock lock(g_queueMutex);
        if (g_outbound.size() >= 256) {
            setError("Outbound command queue overflow");
            return false;
        }
        g_outbound.push_back(line);
    }
    g_queueCv.notify_one();
    return true;
}

void startPipeServer() {
    auto &s = state();
    if (s.running.exchange(true)) return;
    g_inputThread = std::thread(inputLoop);
    g_outputThread = std::thread(outputLoop);
}

void stopPipeServer() {
    auto &s = state();
    if (!s.running.exchange(false)) return;
    s.reconnectRequested = true;
    g_queueCv.notify_all();

    {
        std::scoped_lock lock(g_handleMutex);
        if (g_inputPipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(g_inputPipe, nullptr);
            CloseHandle(g_inputPipe);
            g_inputPipe = INVALID_HANDLE_VALUE;
        }
        if (g_outputPipe != INVALID_HANDLE_VALUE) {
            CancelIoEx(g_outputPipe, nullptr);
            CloseHandle(g_outputPipe);
            g_outputPipe = INVALID_HANDLE_VALUE;
        }
    }

    if (g_inputThread.joinable()) g_inputThread.join();
    if (g_outputThread.joinable()) g_outputThread.join();
}
} // namespace bridge
