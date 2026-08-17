#include "Protocol.h"
#include "NamedPipeServer.h"
#include "BridgeState.h"
#include <format>
#include <mutex>
#include <cmath>

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:IGCS_StartScreenshotSession=_IGCS_StartScreenshotSession")
#pragma comment(linker, "/EXPORT:IGCS_EndScreenshotSession=_IGCS_EndScreenshotSession")
#pragma comment(linker, "/EXPORT:IGCS_MoveCameraPanorama=_IGCS_MoveCameraPanorama")
#pragma comment(linker, "/EXPORT:IGCS_MoveCameraMultishot=_IGCS_MoveCameraMultishot")
#endif

extern "C" __declspec(dllexport) bridge::SessionStartCode __cdecl IGCS_StartScreenshotSession(std::uint8_t type) {
    auto &s = bridge::state();
    if (!s.providerConnected || !s.cameraValid) return bridge::SessionStartCode::CameraFeatureNotAvailable;
    if (!s.camera.cameraEnabled) return bridge::SessionStartCode::CameraNotEnabled;
    if (s.sessionActive) return bridge::SessionStartCode::AlreadySessionActive;

    const bool needsFreshRawBase =
        s.cameraInputMode == bridge::CameraInputMode::RawEuler &&
        (s.engineProfile == bridge::EngineProfile::IdTech7 ||
         s.engineProfile == bridge::EngineProfile::Northlight);

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
        // slots so panorama needs a separate native yaw address.
        if (s.engineProfile == bridge::EngineProfile::IdTech6) {
            return;
        }

        const double rawDelta = bridge::radiansToRawAngle(
            static_cast<double>(stepAngle),
            s.engineProfile);
        bridge::sendLine(std::format("ROTATE_YAW_RAW|{:.12f}", rawDelta));
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

        // DOOM 2016 / idTech 6.
        if (s.engineProfile == bridge::EngineProfile::IdTech6) {
            constexpr double kDoom2016BokehScale = 2.0;

            const CameraToolsData basis =
                bridge::buildCameraToolsData(base, s.engineProfile);

            const double scaledLr = static_cast<double>(lr) * kDoom2016BokehScale;
            const double scaledUd = static_cast<double>(ud) * kDoom2016BokehScale;

            const double x =
                base.x +
                static_cast<double>(basis.rotationMatrixRightVector.values[0]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[0]) * scaledUd;
            const double y =
                base.y +
                static_cast<double>(basis.rotationMatrixRightVector.values[1]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[1]) * scaledUd;
            const double z =
                base.z +
                static_cast<double>(basis.rotationMatrixRightVector.values[2]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[2]) * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.12f}|{:.12f}|{:.12f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // DOOM Eternal / idTech 7.
        if (s.engineProfile == bridge::EngineProfile::IdTech7) {
            constexpr double kPi = 3.14159265358979323846;
            constexpr double kDegToRad = kPi / 180.0;
            constexpr double kDoomBokehScale = 2.0;

            const double scaledLr = static_cast<double>(lr) * kDoomBokehScale;
            const double scaledUd = static_cast<double>(ud) * kDoomBokehScale;

            const double pitch = -base.pitch * kDegToRad;
            const double yaw = (90.0 - base.yaw) * kDegToRad;
            const double roll = base.roll * kDegToRad;

            const double cp = std::cos(pitch);
            const double sp = std::sin(pitch);
            const double cy = std::cos(yaw);
            const double sy = std::sin(yaw);
            const double cr = std::cos(roll);
            const double sr = std::sin(roll);

            const double rightX = cy * sr * sp - cr * sy;
            const double rightY = sy * sr * sp + cr * cy;
            const double rightZ = -sr * cp;

            const double upX = -cr * cy * sp - sr * sy;
            const double upY = -cr * sy * sp + sr * cy;
            const double upZ = cr * cp;

            const double x = base.x + rightX * scaledLr + upX * scaledUd;
            const double y = base.y + rightY * scaledLr + upY * scaledUd;
            const double z = base.z + rightZ * scaledLr + upZ * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.12f}|{:.12f}|{:.12f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // Northlight / CONTROL.
        if (s.engineProfile == bridge::EngineProfile::Northlight) {
            constexpr double kNorthlightDofScale = 0.007;

            const CameraToolsData basis =
                bridge::buildCameraToolsData(base, s.engineProfile);

            const double scaledLr = static_cast<double>(lr) * kNorthlightDofScale;
            const double scaledUd = static_cast<double>(ud) * kNorthlightDofScale;

            const double x =
                base.x +
                static_cast<double>(basis.rotationMatrixRightVector.values[0]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[0]) * scaledUd;
            const double y =
                base.y +
                static_cast<double>(basis.rotationMatrixRightVector.values[1]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[1]) * scaledUd;
            const double z =
                base.z +
                static_cast<double>(basis.rotationMatrixRightVector.values[2]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[2]) * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.12f}|{:.12f}|{:.12f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // Unreal Engine 5.
        //
        // First validation profile: EXACTLY the UE4 basis convention and
        // bokeh scale, but all absolute camera positions stay double from
        // CAMERA_RAW parsing through the final SET_POSITION_RAW command.
        // This avoids LWC precision loss at large world coordinates.
        if (s.engineProfile == bridge::EngineProfile::Unreal5) {
            constexpr double kUE5BokehScale = 1.0;

            const CameraToolsData basis =
                bridge::buildCameraToolsData(base, s.engineProfile);

            const double scaledLr = static_cast<double>(lr) * kUE5BokehScale;
            const double scaledUd = static_cast<double>(ud) * kUE5BokehScale;

            const double x =
                base.x +
                static_cast<double>(basis.rotationMatrixRightVector.values[0]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[0]) * scaledUd;
            const double y =
                base.y +
                static_cast<double>(basis.rotationMatrixRightVector.values[1]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[1]) * scaledUd;
            const double z =
                base.z +
                static_cast<double>(basis.rotationMatrixRightVector.values[2]) * scaledLr +
                static_cast<double>(basis.rotationMatrixUpVector.values[2]) * scaledUd;

            bridge::sendLine(std::format(
                "SET_POSITION_RAW|{:.12f}|{:.12f}|{:.12f}|{:.9f}|{}",
                x, y, z, fov, fromStart ? 1 : 0));
            return;
        }

        // Generic RAW path for UE2.5 / UE3 / UE4.
        const CameraToolsData basis =
            bridge::buildCameraToolsData(base, s.engineProfile);

        const double x =
            base.x +
            static_cast<double>(basis.rotationMatrixRightVector.values[0]) * static_cast<double>(lr) +
            static_cast<double>(basis.rotationMatrixUpVector.values[0]) * static_cast<double>(ud);
        const double y =
            base.y +
            static_cast<double>(basis.rotationMatrixRightVector.values[1]) * static_cast<double>(lr) +
            static_cast<double>(basis.rotationMatrixUpVector.values[1]) * static_cast<double>(ud);
        const double z =
            base.z +
            static_cast<double>(basis.rotationMatrixRightVector.values[2]) * static_cast<double>(lr) +
            static_cast<double>(basis.rotationMatrixUpVector.values[2]) * static_cast<double>(ud);

        bridge::sendLine(std::format(
            "SET_POSITION_RAW|{:.12f}|{:.12f}|{:.12f}|{:.9f}|{}",
            x, y, z, fov, fromStart ? 1 : 0));
        return;
    }

    bridge::sendLine(std::format(
        "MOVE_MULTISHOT|{:.9f}|{:.9f}|{:.9f}|{}",
        lr, ud, fov, fromStart ? 1 : 0));
}
