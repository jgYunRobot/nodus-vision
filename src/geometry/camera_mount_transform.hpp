/**
 * @file camera_mount_transform.hpp
 * @brief 카메라 optical 좌표를 mount frame으로 정규화하는 고정 변환을 제공한다.
 */

#ifndef NODUS_VISION_GEOMETRY_CAMERA_MOUNT_TRANSFORM_HPP_
#define NODUS_VISION_GEOMETRY_CAMERA_MOUNT_TRANSFORM_HPP_

#include <array>
#include <string>

#include "vision_config.hpp"

namespace nodus_vision {

/** @brief camera optical frame에서 named mount frame으로 향하는 정규화된 고정 변환이다. */
struct CameraMountTransform {
    std::string calibration_id;
    std::string sensor_frame;
    std::string mount_frame;
    std::array<double, 16> mount_from_camera_optical_matrix4x4{};
};

/** @brief config의 local pose와 optical convention을 하나의 고정 변환으로 합성한다. */
CameraMountTransform buildCameraMountTransform(const CalibrationConfig& calibration);
/** @brief camera optical point에 정규화된 고정 mount transform을 한 번 적용한다. */
std::array<float, 3> transformCameraPointToMount(const std::array<float, 3>& point_camera_optical,
                                                 const CameraMountTransform& transform);
/** @brief PCD1 v2 matrix3x4 슬롯용 row-major 고정 변환을 반환한다. */
std::array<float, 12> buildMountFromCameraMatrix3x4(const CameraMountTransform& transform);

}  // namespace nodus_vision

#endif  // NODUS_VISION_GEOMETRY_CAMERA_MOUNT_TRANSFORM_HPP_
