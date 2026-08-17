#pragma once
#include "CameraToolsData.h"
#include <string>

namespace bridge {

enum class EngineProfile {
    Normalized,
    UnrealLegacy,
    Unreal4,
    Unreal5,
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
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double pitch{0.0};
    double yaw{0.0};
    double roll{0.0};
    double fov{70.0};
};

EngineProfile engineProfileFromTag(const std::string &tag);
const char *engineProfileDisplayName(EngineProfile profile);
double rawAngleToRadians(double value, EngineProfile profile);
double radiansToRawAngle(double value, EngineProfile profile);
CameraToolsData buildCameraToolsData(const RawCameraData &raw, EngineProfile profile);

} // namespace bridge
