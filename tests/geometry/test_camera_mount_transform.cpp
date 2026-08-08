/**
 * @file test_camera_mount_transform.cpp
 * @brief 고정 camera optical-to-mount 변환을 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
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

std::array<double, 3> transformPoint(const boost::json::array& matrix,
                                     const boost::json::object& point) {
    std::array<double, 3> result{};
    const std::array<double, 3> input = {point.at("x").to_number<double>(),
                                         point.at("y").to_number<double>(),
                                         point.at("z").to_number<double>()};
    for (int row = 0; row < 3; ++row) {
        result.at(row) = matrix.at(row * 4 + 3).to_number<double>();
        for (int column = 0; column < 3; ++column) {
            result.at(row) += matrix.at(row * 4 + column).to_number<double>() * input.at(column);
        }
    }
    return result;
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

TEST(CameraMountTransform, MatchesPaControlNodusRmXyzEulerGoldenMatrix) {
    CalibrationConfig calibration = makeCalibration();
    calibration.mount_local_transform = {0.0, 0.0, 0.0, 0.2, -0.4, 0.6, "XYZ"};
    const CameraMountTransform transform = buildCameraMountTransform(calibration);

    const std::array<double, 9> expected_rotation_with_optical_convention = {
        0.760184441854691, 0.389418342308651, -0.520070157801479,
        0.489534729385742, 0.182986571299987, 0.852567688485262,
        0.427171350967384, -0.902701096375460, -0.051530258249325,
    };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            EXPECT_NEAR(transform.mount_from_camera_optical_matrix4x4.at(row * 4 + column),
                        expected_rotation_with_optical_convention.at(row * 3 + column), 1.0e-12);
        }
    }
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

TEST(CameraMountTransform, FixtureAppliesStaticAndDynamicTransformsExactlyOnce) {
    const std::string fixture_path = std::string(NODUS_VISION_SOURCE_DIR) +
                                     "/schemas/vision/v1/fixtures/camera_mount_geometry_v1.json";
    std::ifstream input(fixture_path);
    ASSERT_TRUE(input.is_open());
    std::ostringstream contents;
    contents << input.rdbuf();
    const boost::json::object fixture = boost::json::parse(contents.str()).as_object();
    ASSERT_EQ(fixture.at("mount_from_camera_optical_matrix4x4").as_array().size(), 16U);
    const std::array<double, 3> point_mount =
        transformPoint(fixture.at("mount_from_camera_optical_matrix4x4").as_array(),
                       fixture.at("point_camera_optical_m").as_object());
    const boost::json::object& expected_mount = fixture.at("point_mount_m").as_object();
    EXPECT_NEAR(point_mount.at(0), expected_mount.at("x").to_number<double>(), 1.0e-12);
    EXPECT_NEAR(point_mount.at(1), expected_mount.at("y").to_number<double>(), 1.0e-12);
    EXPECT_NEAR(point_mount.at(2), expected_mount.at("z").to_number<double>(), 1.0e-12);

    const boost::json::array& dynamic_poses = fixture.at("dynamic_mount_poses").as_array();
    ASSERT_EQ(dynamic_poses.size(), 2U);
    for (const boost::json::value& value : dynamic_poses) {
        const boost::json::object& pose = value.as_object();
        const std::array<double, 3> point_root = transformPoint(
            pose.at("root_from_mount_matrix4x4").as_array(),
            {{"x", point_mount.at(0)}, {"y", point_mount.at(1)}, {"z", point_mount.at(2)}});
        const boost::json::object& expected_root = pose.at("point_root_m").as_object();
        EXPECT_NEAR(point_root.at(0), expected_root.at("x").to_number<double>(), 1.0e-12);
        EXPECT_NEAR(point_root.at(1), expected_root.at("y").to_number<double>(), 1.0e-12);
        EXPECT_NEAR(point_root.at(2), expected_root.at("z").to_number<double>(), 1.0e-12);
    }
}

}  // namespace nodus_vision
