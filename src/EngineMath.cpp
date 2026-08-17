#include "EngineMath.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace bridge {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLegacyUnitsPerTurn = 65536.0;

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
    if (value == "UE5" || value == "UNREAL5" ||
        value == "UNREAL ENGINE 5") {
        return EngineProfile::Unreal5;
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
    case EngineProfile::Unreal5:
        return "Unreal Engine 5";
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

double rawAngleToRadians(double value, EngineProfile profile) {
    if (profile == EngineProfile::UnrealLegacy) {
        return value * (2.0 * kPi / kLegacyUnitsPerTurn);
    }
    if (profile == EngineProfile::Unreal4 ||
        profile == EngineProfile::Unreal5) {
        return value * (kPi / 180.0);
    }
    return value;
}

double radiansToRawAngle(double value, EngineProfile profile) {
    if (profile == EngineProfile::UnrealLegacy) {
        return value * (kLegacyUnitsPerTurn / (2.0 * kPi));
    }
    if (profile == EngineProfile::Unreal4 ||
        profile == EngineProfile::Unreal5 ||
        profile == EngineProfile::IdTech7) {
        return value * (180.0 / kPi);
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
    data.coordinates.values[0] = static_cast<float>(raw.x);
    data.coordinates.values[1] = static_cast<float>(raw.y);
    data.coordinates.values[2] = static_cast<float>(raw.z);

    // DOOM 2016 / idTech 6 native-basis convention.
    if (profile == EngineProfile::IdTech6) {
        float fx = static_cast<float>(raw.pitch);
        float fy = static_cast<float>(raw.yaw);
        float fz = static_cast<float>(raw.roll);

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
            rx = fy / horizontal;
            ry = -fx / horizontal;
        }

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
        data.fov = static_cast<float>(raw.fov);
        return data;
    }

    double pitch = rawAngleToRadians(raw.pitch, profile);
    double yaw = rawAngleToRadians(raw.yaw, profile);
    double roll = rawAngleToRadians(raw.roll, profile);

    if (profile == EngineProfile::IdTech7) {
        pitch = -raw.pitch * (kPi / 180.0);
        yaw = (90.0 - raw.yaw) * (kPi / 180.0);
        roll = raw.roll * (kPi / 180.0);
    }

    if (profile == EngineProfile::Northlight) {
        pitch = raw.pitch;
        yaw = raw.yaw;
        roll = -raw.roll;
    }

    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);

    // Same Unreal basis convention validated in the existing UE4 providers.
    // UE5 deliberately starts with this exact convention; only the raw
    // position precision differs for the first validation pass.
    data.rotationMatrixRightVector.values[0] = static_cast<float>(cy * sr * sp - cr * sy);
    data.rotationMatrixRightVector.values[1] = static_cast<float>(sy * sr * sp + cr * cy);
    data.rotationMatrixRightVector.values[2] = static_cast<float>(-sr * cp);

    data.rotationMatrixUpVector.values[0] = static_cast<float>(-cr * cy * sp - sr * sy);
    data.rotationMatrixUpVector.values[1] = static_cast<float>(-cr * sy * sp + sr * cy);
    data.rotationMatrixUpVector.values[2] = static_cast<float>(cr * cp);

    data.rotationMatrixForwardVector.values[0] = static_cast<float>(cp * cy);
    data.rotationMatrixForwardVector.values[1] = static_cast<float>(cp * sy);
    data.rotationMatrixForwardVector.values[2] = static_cast<float>(sp);

    data.lookQuaternion.values[0] = 0.0f;
    data.lookQuaternion.values[1] = 0.0f;
    data.lookQuaternion.values[2] = 0.0f;
    data.lookQuaternion.values[3] = 1.0f;

    data.pitch = static_cast<float>(pitch);
    data.yaw = static_cast<float>(yaw);
    data.roll = static_cast<float>(roll);
    data.fov = static_cast<float>(raw.fov);
    return data;
}

} // namespace bridge
