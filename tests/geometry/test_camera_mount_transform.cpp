/**
 * @file test_camera_mount_transform.cpp
 * @brief 고정 camera optical-to-mount 변환을 검증한다.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "camera_mount_transform.hpp"

namespace nodus_vision {
namespace {

constexpr double HALF_PI = 1.57079632679489661923;

CalibrationConfig makeCalibration() {
    CalibrationConfig calibration;
    calibration.calibration_id = "front_d435_mount_v1";
    calibration.sensor_frame = "front_d435_color_optical_frame";
    calibration.mount_frame = "e_rob_wrist_cam";
    calibration.mount_local_transform = {0.0, 0.0, 0.06, 0.0, 0.0, 0.0, "XYZ"};
    return calibration;
}

}  // namespace

TEST(CameraMountTransform, ComposesOpticalConventionAndStaticTranslation) {
    const CameraMountTransform transform = buildCameraMountTransform(makeCalibration());
    const std::array<double, 16> expected = {1.0, 0.0,  0.0, 0.0,  0.0, 0.0, 1.0, 0.0,
                                             0.0, -1.0, 0.0, 0.06, 0.0, 0.0, 0.0, 1.0};
    EXPECT_EQ(transform.calibration_id, "front_d435_mount_v1");
    EXPECT_EQ(transform.sensor_frame, "front_d435_color_optical_frame");
    EXPECT_EQ(transform.mount_frame, "e_rob_wrist_cam");
    EXPECT_EQ(transform.mount_from_camera_optical_matrix4x4, expected);

    const std::array<float, 3> point_mount =
        transformCameraPointToMount({0.1F, 0.2F, 0.5F}, transform);
    EXPECT_NEAR(point_mount.at(0), 0.1F, 1.0e-6F);
    EXPECT_NEAR(point_mount.at(1), 0.5F, 1.0e-6F);
    EXPECT_NEAR(point_mount.at(2), -0.14F, 1.0e-6F);
}

TEST(CameraMountTransform, AppliesEulerBeforeOpticalConventionAndTranslation) {
    CalibrationConfig calibration = makeCalibration();
    calibration.mount_local_transform = {1.0, 0.0, 0.0, HALF_PI, 0.0, 0.0, "XYZ"};
    const CameraMountTransform transform = buildCameraMountTransform(calibration);

    const std::array<float, 3> point_mount =
        transformCameraPointToMount({0.0F, 1.0F, 0.0F}, transform);
    EXPECT_NEAR(point_mount.at(0), 1.0F, 1.0e-6F);
    EXPECT_NEAR(point_mount.at(1), 1.0F, 1.0e-6F);
    EXPECT_NEAR(point_mount.at(2), 0.0F, 1.0e-6F);
}

TEST(CameraMountTransform, SupportsEveryConfiguredEulerConvention) {
    constexpr const char* EULER_TYPES[] = {"XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX", "ZXZ", "ZYZ"};
    for (const char* euler_type : EULER_TYPES) {
        CalibrationConfig calibration = makeCalibration();
        calibration.mount_local_transform = {0.1, -0.2, 0.3, 0.2, -0.4, 0.6, euler_type};
        const CameraMountTransform transform = buildCameraMountTransform(calibration);
        const std::array<float, 12> matrix3x4 = buildMountFromCameraMatrix3x4(transform);
        EXPECT_TRUE(std::isfinite(matrix3x4.at(0)));
        EXPECT_NEAR(transform.mount_from_camera_optical_matrix4x4.at(15), 1.0, 1.0e-12);
    }
}

TEST(CameraMountTransform, RejectsInvalidCalibrationAndPoints) {
    CalibrationConfig invalid_calibration = makeCalibration();
    invalid_calibration.mount_local_transform.r1 = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(buildCameraMountTransform(invalid_calibration), std::invalid_argument);

    const CameraMountTransform transform = buildCameraMountTransform(makeCalibration());
    EXPECT_THROW(transformCameraPointToMount({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                             transform),
                 std::invalid_argument);
}

}  // namespace nodus_vision
