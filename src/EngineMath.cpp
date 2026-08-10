#include "EngineMath.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace bridge {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kLegacyUnitsPerTurn = 65536.0f;

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

float length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}
}

EngineProfile engineProfileFromTag(const std::string &tag) {
    const std::string value = upper(tag);
    if (value == "UE3" || value == "UE2.5" || value == "UE25" ||
        value == "UNREAL2.5" || value == "UNREAL3" ||
        value == "UNREAL ENGINE 2.5" || value == "UNREAL ENGINE 3") {
        return EngineProfile::UnrealLegacy;
    }
    if (value == "UE4" || value == "UNREAL4" ||
        value == "UNREAL ENGINE 4") {
        return EngineProfile::Unreal4;
    }
    if (value == "DOOM_2016" || value == "DOOM 2016" ||
        value == "IDTECH6" || value == "IDTECH 6" ||
        value == "ID TECH 6") {
        return EngineProfile::IdTech6;
    }
    if (value == "DOOM_ETERNAL" || value == "DOOM ETERNAL" ||
        value == "IDTECH7" || value == "IDTECH 7" ||
        value == "ID TECH 7") {
        return EngineProfile::IdTech7;
    }
    if (value == "NORTHLIGHT" || value == "NORTHLIGHT ENGINE") {
        return EngineProfile::Northlight;
    }
    return EngineProfile::Normalized;
}

const char *engineProfileDisplayName(EngineProfile profile) {
    switch (profile) {
    case EngineProfile::UnrealLegacy:
        return "Unreal Engine 2.5 / 3";
    case EngineProfile::Unreal4:
        return "Unreal Engine 4";
    case EngineProfile::IdTech6:
        return "idTech 6";
    case EngineProfile::IdTech7:
        return "idTech 7";
    case EngineProfile::Northlight:
        return "Northlight";
    default:
        return "Provider normalized";
    }
}

float rawAngleToRadians(float value, EngineProfile profile) {
    if (profile == EngineProfile::UnrealLegacy) {
        return value * (2.0f * kPi / kLegacyUnitsPerTurn);
    }
    if (profile == EngineProfile::Unreal4) {
        return value * (kPi / 180.0f);
    }
    return value;
}

float radiansToRawAngle(float value, EngineProfile profile) {
    if (profile == EngineProfile::UnrealLegacy) {
        return value * (kLegacyUnitsPerTurn / (2.0f * kPi));
    }
    if (profile == EngineProfile::Unreal4 ||
        profile == EngineProfile::IdTech7) {
        return value * (180.0f / kPi);
    }
    return value;
}

CameraToolsData buildCameraToolsData(
    const RawCameraData &raw,
    EngineProfile profile
) {
    CameraToolsData data{};
    data.cameraEnabled = raw.enabled ? 1 : 0;
    data.cameraMovementLocked = raw.locked ? 1 : 0;
    data.coordinates.values[0] = raw.x;
    data.coordinates.values[1] = raw.y;
    data.coordinates.values[2] = raw.z;

    // -----------------------------------------------------------------
    // DOOM 2016 / idTech 6 native-basis convention.
    //
    // To keep the CE provider core universal, the existing three RAW
    // rotation slots carry the native Forward vector for this profile:
    //
    //   raw.pitch = Forward X
    //   raw.yaw   = Forward Y
    //   raw.roll  = Forward Z
    //
    // DOOM 2016 Photo Mode has no native roll. Right is horizontal and
    // can therefore be reconstructed from Forward, then Up is built with
    // a strict cross product. This keeps every multishot displacement in
    // the camera Right/Up plane and removes Forward leakage on U/D moves.
    // -----------------------------------------------------------------
    if (profile == EngineProfile::IdTech6) {
        float fx = raw.pitch;
        float fy = raw.yaw;
        float fz = raw.roll;

        const float forwardLength = length3(fx, fy, fz);
        if (forwardLength > 0.000001f) {
            fx /= forwardLength;
            fy /= forwardLength;
            fz /= forwardLength;
        } else {
            fx = 1.0f;
            fy = 0.0f;
            fz = 0.0f;
        }

        const float horizontal = std::sqrt(fx * fx + fy * fy);

        float rx = 0.0f;
        float ry = 1.0f;
        float rz = 0.0f;

        if (horizontal > 0.000001f) {
            // Validated DOOM 2016 handedness:
            // Right = (ForwardY, -ForwardX, 0) at zero roll.
            rx = fy / horizontal;
            ry = -fx / horizontal;
        }

        // Up = Right x Forward.
        float ux = ry * fz - rz * fy;
        float uy = rz * fx - rx * fz;
        float uz = rx * fy - ry * fx;

        const float upLength = length3(ux, uy, uz);
        if (upLength > 0.000001f) {
            ux /= upLength;
            uy /= upLength;
            uz /= upLength;
        } else {
            ux = 0.0f;
            uy = 0.0f;
            uz = 1.0f;
        }

        // Rebuild Right from Forward x Up to guarantee an orthonormal basis.
        rx = fy * uz - fz * uy;
        ry = fz * ux - fx * uz;
        rz = fx * uy - fy * ux;

        const float rightLength = length3(rx, ry, rz);
        if (rightLength > 0.000001f) {
            rx /= rightLength;
            ry /= rightLength;
            rz /= rightLength;
        }

        data.rotationMatrixRightVector.values[0] = rx;
        data.rotationMatrixRightVector.values[1] = ry;
        data.rotationMatrixRightVector.values[2] = rz;

        data.rotationMatrixUpVector.values[0] = ux;
        data.rotationMatrixUpVector.values[1] = uy;
        data.rotationMatrixUpVector.values[2] = uz;

        data.rotationMatrixForwardVector.values[0] = fx;
        data.rotationMatrixForwardVector.values[1] = fy;
        data.rotationMatrixForwardVector.values[2] = fz;

        data.lookQuaternion.values[0] = 0.0f;
        data.lookQuaternion.values[1] = 0.0f;
        data.lookQuaternion.values[2] = 0.0f;
        data.lookQuaternion.values[3] = 1.0f;

        data.pitch = std::atan2(fz, horizontal);
        data.yaw = std::atan2(fy, fx);
        data.roll = 0.0f;
        data.fov = raw.fov;
        return data;
    }

    float pitch = rawAngleToRadians(raw.pitch, profile);
    float yaw = rawAngleToRadians(raw.yaw, profile);
    float roll = rawAngleToRadians(raw.roll, profile);

    // DOOM Eternal / idTech 7 validated convention.
    if (profile == EngineProfile::IdTech7) {
        // idTech 7 Photo Mode stores Pitch/Yaw/Roll as degrees.
        pitch = -raw.pitch * (kPi / 180.0f);
        yaw = (90.0f - raw.yaw) * (kPi / 180.0f);
        roll = raw.roll * (kPi / 180.0f);
    }

    // Northlight / CONTROL validated raw convention:
    //   yaw, pitch, roll are already radians.
    //   Forward at zero = +X
    //   Right   at zero = +Y
    //   Up      at zero = +Z
    //
    // The validated CONTROL Lua applies positive roll with:
    //   Right' = Right*cos(r) + Up*sin(r)
    //   Up'    = Up*cos(r) - Right*sin(r)
    //
    // The generic basis formula below uses the opposite roll sign,
    // therefore negate raw roll for Northlight before evaluating it.
    if (profile == EngineProfile::Northlight) {
        pitch = raw.pitch;
        yaw = raw.yaw;
        roll = -raw.roll;
    }

    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);

    // Same Unreal basis convention validated in the existing CE providers.
    data.rotationMatrixRightVector.values[0] = cy * sr * sp - cr * sy;
    data.rotationMatrixRightVector.values[1] = sy * sr * sp + cr * cy;
    data.rotationMatrixRightVector.values[2] = -sr * cp;

    data.rotationMatrixUpVector.values[0] = -cr * cy * sp - sr * sy;
    data.rotationMatrixUpVector.values[1] = -cr * sy * sp + sr * cy;
    data.rotationMatrixUpVector.values[2] = cr * cp;

    data.rotationMatrixForwardVector.values[0] = cp * cy;
    data.rotationMatrixForwardVector.values[1] = cp * sy;
    data.rotationMatrixForwardVector.values[2] = sp;

    // Quaternion is not required by the current IGCS DoF path.
    data.lookQuaternion.values[0] = 0.0f;
    data.lookQuaternion.values[1] = 0.0f;
    data.lookQuaternion.values[2] = 0.0f;
    data.lookQuaternion.values[3] = 1.0f;

    data.pitch = pitch;
    data.yaw = yaw;
    data.roll = roll;
    data.fov = raw.fov;
    return data;
}

} // namespace bridge
