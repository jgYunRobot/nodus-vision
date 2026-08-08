/**
 * @file test_query_serializer.cpp
 * @brief query JSON body와 immutable frame identity 계약을 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cstdint>

#include "query_serializer.hpp"

namespace nodus_vision {
namespace {

CameraMountTransform makeTransform() {
    CameraMountTransform transform;
    transform.calibration_id = "fake_v1";
    transform.sensor_frame = "fake_optical";
    transform.mount_frame = "fake_mount";
    transform.mount_from_camera_optical_matrix4x4 = {1.0, 0.0,  0.0, 0.0,  0.0, 0.0, 1.0, 0.0,
                                                     0.0, -1.0, 0.0, 0.06, 0.0, 0.0, 0.0, 1.0};
    return transform;
}

}  // namespace

TEST(QuerySerializer, SerializesPixelIdentityAndInvalidDepth) {
    PixelPointResult result;
    result.identity = {2U, 9U, 11, 12, 0.0, DeviceTimestampDomain::e_UNKNOWN};
    result.pixel_x = 3;
    result.pixel_y = 4;
    result.invalid_reason = "invalid_depth";

    const boost::json::object body =
        boost::json::parse(serializePixelPointResult(result, makeTransform())).as_object();
    EXPECT_EQ(body.at("schema_version").as_int64(), 1);
    EXPECT_EQ(body.at("frame").as_object().at("capture_generation").to_number<std::uint64_t>(), 2U);
    EXPECT_EQ(body.at("frame").as_object().at("frame_number").to_number<std::uint64_t>(), 9U);
    EXPECT_EQ(body.at("pixel").as_object().at("x").as_int64(), 3);
    EXPECT_FALSE(body.at("valid").as_bool());
    EXPECT_EQ(body.at("geometry").as_object().at("mount_frame"), "fake_mount");
    EXPECT_EQ(body.at("reason").as_string(), "invalid_depth");
}

TEST(QuerySerializer, SerializesCameraAndMountPointExactlyOnce) {
    PixelPointResult result;
    result.identity = {2U, 9U, 11, 12, 0.0, DeviceTimestampDomain::e_UNKNOWN};
    result.pixel_x = 3;
    result.pixel_y = 4;
    result.valid = true;
    result.depth_m = 0.5F;
    result.optical_point_m = {0.1F, 0.2F, 0.5F};

    const boost::json::object body =
        boost::json::parse(serializePixelPointResult(result, makeTransform())).as_object();
    EXPECT_NEAR(body.at("point_camera_m").as_object().at("y").to_number<float>(), 0.2F, 1.0e-6F);
    EXPECT_NEAR(body.at("point_mount_m").as_object().at("y").to_number<float>(), 0.5F, 1.0e-6F);
    EXPECT_NEAR(body.at("point_mount_m").as_object().at("z").to_number<float>(), -0.14F, 1.0e-6F);
}

TEST(QuerySerializer, SerializesCompleteRoiStatistics) {
    RoiDepthResult result;
    result.identity = {3U, 10U, 21, 22, 0.0, DeviceTimestampDomain::e_UNKNOWN};
    result.requested_roi = {1, 2, 3, 4};
    result.clamped_roi = {1, 2, 2, 2};
    result.pixel_count = 4U;
    result.valid_pixel_count = 3U;
    result.fill_rate = 0.75F;
    result.valid = true;
    result.median_pixel_x = 2;
    result.median_pixel_y = 3;

    const boost::json::object body =
        boost::json::parse(serializeRoiDepthResult(result, makeTransform())).as_object();
    EXPECT_EQ(body.at("frame").as_object().at("capture_generation").to_number<std::uint64_t>(), 3U);
    EXPECT_EQ(body.at("requested_roi").as_object().at("width").as_int64(), 3);
    EXPECT_EQ(body.at("roi").as_object().at("width").as_int64(), 2);
    EXPECT_EQ(body.at("stats").as_object().at("valid_pixel_count").to_number<std::uint64_t>(), 3U);
    EXPECT_EQ(body.at("median_pixel").as_object().at("y").as_int64(), 3);
    EXPECT_EQ(body.at("geometry").as_object().at("calibration_id"), "fake_v1");
    EXPECT_TRUE(body.if_contains("median_point_mount_m") != nullptr);
}

}  // namespace nodus_vision
