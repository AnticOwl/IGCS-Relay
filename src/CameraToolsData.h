#pragma once
#include <cstdint>

struct Vec3 { float values[3]{}; };
struct Vec4 { float values[4]{}; };

// Binary layout used by IGCS Connector 2.4+.
struct CameraToolsData {
    std::uint8_t cameraEnabled{};
    std::uint8_t cameraMovementLocked{};
    std::uint8_t reserved1{};
    std::uint8_t reserved2{};
    float fov{70.0f};
    Vec3 coordinates{};
    Vec4 lookQuaternion{{0.0f, 0.0f, 0.0f, 1.0f}};
    Vec3 rotationMatrixUpVector{{0.0f, 1.0f, 0.0f}};
    Vec3 rotationMatrixRightVector{{1.0f, 0.0f, 0.0f}};
    Vec3 rotationMatrixForwardVector{{0.0f, 0.0f, 1.0f}};
    float pitch{};
    float yaw{};
    float roll{};
};
static_assert(sizeof(CameraToolsData) == 84, "Unexpected CameraToolsData layout");
