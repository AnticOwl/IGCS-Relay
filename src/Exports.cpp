#include "Protocol.h"
#include "NamedPipeServer.h"
#include "BridgeState.h"
#include "IgcsConnectorLink.h"
#include <format>
#include <mutex>
#include <cmath>
#include <intrin.h>

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:IGCS_StartScreenshotSession=_IGCS_StartScreenshotSession")
#pragma comment(linker, "/EXPORT:IGCS_EndScreenshotSession=_IGCS_EndScreenshotSession")
#pragma comment(linker, "/EXPORT:IGCS_MoveCameraPanorama=_IGCS_MoveCameraPanorama")
#pragma comment(linker, "/EXPORT:IGCS_MoveCameraMultishot=_IGCS_MoveCameraMultishot")
#endif


extern "C" __declspec(dllexport) bridge::SessionStartCode __cdecl IGCS_StartScreenshotSession(std::uint8_t type) {
    // IGCSDOF and Parallax both call the same IGCS exports. Resolve the
    // originating addon from the caller address so the Relay follows the DOF
    // backend that actually starts the screenshot session.
    bridge::selectDofBackendFromCallerAddress(_ReturnAddress());

    auto &s = bridge::state();
    if (!s.providerConnected || !s.cameraValid) return bridge::SessionStartCode::CameraFeatureNotAvailable;
    if (!s.camera.cameraEnabled) return bridge::SessionStartCode::CameraNotEnabled;
    if (s.sessionActive) return bridge::SessionStartCode::AlreadySessionActive;

    const bool needsFreshRawBase =
        s.cameraInputMode == bridge::CameraInputMode::RawEuler &&
        (s.engineProfile == bridge::EngineProfile::IdTech7 ||
         s.engineProfile == bridge::EngineProfile::Northlight ||
         s.engineProfile == bridge::EngineProfile::Rage);

    {
        std::scoped_lock lock(s.mutex);
        if (needsFreshRawBase) {
            s.sessionBaseValid = false;
            s.sessionBaseRefreshPending = true;
        } else if (s.cameraInputMode == bridge::CameraInputMode::RawEuler) {
            s.sessionBaseRawCamera = s.rawCamera;
            s.sessionBaseValid = true;
        }
    }

    if (!bridge::sendLine(std::format("SESSION_BEGIN|{}", type))) {
        {
            std::scoped_lock lock(s.mutex);
            s.sessionBaseValid = false;
            s.sessionBaseRefreshPending = false;
        }
        return bridge::SessionStartCode::UnknownError;
    }

    if (needsFreshRawBase) {
        std::unique_lock lock(s.mutex);
        const bool gotFreshBase = s.sessionBaseCv.wait_for(
            lock,
            std::chrono::milliseconds(250),
            [&s] {
                return s.sessionBaseValid &&
                       !s.sessionBaseRefreshPending;
            });

        if (!gotFreshBase) {
            s.sessionBaseValid = false;
            s.sessionBaseRefreshPending = false;
            lock.unlock();
            bridge::sendLine("SESSION_END");
            return bridge::SessionStartCode::UnknownError;
        }
    }

    s.sessionActive = true;
    return bridge::SessionStartCode::AllOk;
}
extern "C" __declspec(dllexport) void __cdecl IGCS_EndScreenshotSession() {
    auto &s = bridge::state();
    bridge::sendLine("SESSION_END");
    s.sessionActive = false;
    {
        std::scoped_lock lock(s.mutex);
        s.sessionBaseValid = false;
        s.sessionBaseRefreshPending = false;
    }
    s.sessionBaseCv.notify_all();
}
extern "C" __declspec(dllexport) void __cdecl IGCS_MoveCameraPanorama(float stepAngle) {
    auto &s = bridge::state();
    if (s.cameraInputMode == bridge::CameraInputMode::RawEuler) {
        // idTech 6 packs native Forward XYZ into the three RAW rotation
        // slots so the universal CE provider can remain math-free. Panorama
        // needs a separate native yaw address and is intentionally left
        // untouched until that optional provider command is added.
        if (s.engineProfile == bridge::EngineProfile::IdTech6) {
            return;
        }

        const float rawDelta = bridge::radiansToRawAngle(stepAngle, s.engineProfile);
        bridge::sendLine(std::format("ROTATE_YAW_RAW|{:.9f}", rawDelta));
        return;
    }
    bridge::sendLine(std::format("MOVE_PANORAMA|{:.9f}", stepAngle));
}
extern "C" __declspec(dllexport) void __cdecl IGCS_MoveCameraMultishot(float lr, float ud, float fov, bool fromStart) {
    auto &s = bridge::state();

    if (s.cameraInputMode == bridge::CameraInputMode::RawEuler && s.sessionBaseValid) {
        bridge::RawCameraData base{};

        {
            std::scoped_lock lock(s.mutex);
            base = s.sessionBaseRawCamera;
        }

        // -----------------------------------------------------------------
        // DOOM 2016 / idTech 6
        // -----------------------------------------------------------------
        if (s.engineProfile == bridge::EngineProfile::IdTech6) {
            constexpr float kDoom2016BokehScale = 2.0f;

            const CameraToolsData basis =
                bridge::buildCameraToolsData(base, s.engineProfile);

            const float scaledLr = lr * kDoom2016BokehScale;
            const float scaledUd = ud * kDoom2016BokehScale;

            const float x =
                base.x +
                basis.rotationMatrixRightVector.values[0] * scaledLr +
                basis.rotationMatrixUpVector.values[0] * scaledUd;
            const float y =
                base.y +
                basis.rotationMatrixRightVector.values[1] * scaledLr +
                basis.rotationMatrixUpVector.values[1] * scaledUd;
            const float z =
                base.z +
                basis.rotationMatrixRightVector.values[2] * scaledLr +
                basis.rotationMatrixUpVector.values[2] * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.9f}|{:.9f}|{:.9f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // -----------------------------------------------------------------
        // DOOM Eternal / idTech 7
        // -----------------------------------------------------------------
        if (s.engineProfile == bridge::EngineProfile::IdTech7) {
            constexpr float kPi = 3.14159265358979323846f;
            constexpr float kDegToRad = kPi / 180.0f;
            constexpr float kDoomBokehScale = 2.0f;

            const float scaledLr = lr * kDoomBokehScale;
            const float scaledUd = ud * kDoomBokehScale;

            const float pitch = -base.pitch * kDegToRad;
            const float yaw = (90.0f - base.yaw) * kDegToRad;
            const float roll = base.roll * kDegToRad;

            const float cp = std::cos(pitch);
            const float sp = std::sin(pitch);
            const float cy = std::cos(yaw);
            const float sy = std::sin(yaw);
            const float cr = std::cos(roll);
            const float sr = std::sin(roll);

            const float rightX = cy * sr * sp - cr * sy;
            const float rightY = sy * sr * sp + cr * cy;
            const float rightZ = -sr * cp;

            const float upX = -cr * cy * sp - sr * sy;
            const float upY = -cr * sy * sp + sr * cy;
            const float upZ = cr * cp;

            const float x = base.x + rightX * scaledLr + upX * scaledUd;
            const float y = base.y + rightY * scaledLr + upY * scaledUd;
            const float z = base.z + rightZ * scaledLr + upZ * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.9f}|{:.9f}|{:.9f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // -----------------------------------------------------------------
        // Northlight / CONTROL
        // -----------------------------------------------------------------
        if (s.engineProfile == bridge::EngineProfile::Northlight) {
            constexpr float kNorthlightDofScale = 0.007f;

            const CameraToolsData basis =
                bridge::buildCameraToolsData(base, s.engineProfile);

            const float scaledLr = lr * kNorthlightDofScale;
            const float scaledUd = ud * kNorthlightDofScale;

            const float x =
                base.x +
                basis.rotationMatrixRightVector.values[0] * scaledLr +
                basis.rotationMatrixUpVector.values[0] * scaledUd;
            const float y =
                base.y +
                basis.rotationMatrixRightVector.values[1] * scaledLr +
                basis.rotationMatrixUpVector.values[1] * scaledUd;
            const float z =
                base.z +
                basis.rotationMatrixRightVector.values[2] * scaledLr +
                basis.rotationMatrixUpVector.values[2] * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.9f}|{:.9f}|{:.9f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // Generic RAW path for UE2.5 / UE3 / UE4 / RAGE.
        const CameraToolsData basis =
            bridge::buildCameraToolsData(base, s.engineProfile);

        const float x =
            base.x +
            basis.rotationMatrixRightVector.values[0] * lr +
            basis.rotationMatrixUpVector.values[0] * ud;
        const float y =
            base.y +
            basis.rotationMatrixRightVector.values[1] * lr +
            basis.rotationMatrixUpVector.values[1] * ud;
        const float z =
            base.z +
            basis.rotationMatrixRightVector.values[2] * lr +
            basis.rotationMatrixUpVector.values[2] * ud;

        bridge::sendLine(std::format(
            "SET_POSITION_RAW|{:.9f}|{:.9f}|{:.9f}|{:.9f}|{}",
            x, y, z, fov, fromStart ? 1 : 0));
        return;
    }

    bridge::sendLine(std::format(
        "MOVE_MULTISHOT|{:.9f}|{:.9f}|{:.9f}|{}",
        lr, ud, fov, fromStart ? 1 : 0));
}
