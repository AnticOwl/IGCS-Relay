#pragma once
#include "CameraToolsData.h"
#include <string>

namespace bridge {

enum class EngineProfile {
    Normalized,
    UnrealLegacy,
    Unreal4,
    IdTech6,
    IdTech7,
    Northlight
};

enum class CameraInputMode {
    NormalizedBasis,
    RawEuler
};

struct RawCameraData {
    bool valid{false};
    bool enabled{false};
    bool locked{false};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float pitch{0.0f};
    float yaw{0.0f};
    float roll{0.0f};
    float fov{70.0f};
};

EngineProfile engineProfileFromTag(const std::string &tag);
const char *engineProfileDisplayName(EngineProfile profile);
float rawAngleToRadians(float value, EngineProfile profile);
float radiansToRawAngle(float value, EngineProfile profile);
CameraToolsData buildCameraToolsData(const RawCameraData &raw, EngineProfile profile);

} // namespace bridge
