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
