/**
 * @file test_pcd1.cpp
 * @brief PCD1 v2 exact byte contract와 invalid payload rejection을 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pcd1.hpp"

namespace nodus_vision {
namespace {

std::string getFixturePath(const char* file_name) {
    return std::string(NODUS_VISION_SOURCE_DIR) + "/schemas/vision/v1/fixtures/" + file_name;
}

boost::json::object loadExpectedFixture() {
    std::ifstream input(getFixturePath("pointcloud_pcd1_v2_camera_mount_expected.json"));
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open the PCD1 expected fixture.");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return boost::json::parse(contents.str()).as_object();
}

std::vector<std::uint8_t> loadBinaryFixture() {
    std::ifstream input(getFixturePath("pointcloud_pcd1_v2_camera_mount.bin"), std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open the PCD1 binary fixture.");
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
    return {bytes.begin(), bytes.end()};
}

PointCloudSnapshot makePointCloud(const boost::json::object& expected) {
    PointCloudSnapshot snapshot;
    snapshot.identity.frame_number = expected.at("frame_number").to_number<std::uint64_t>();
    snapshot.identity.capture_timestamp_ns =
        expected.at("capture_timestamp_ns").to_number<std::int64_t>();
    const boost::json::object& source_profile = expected.at("source_profile").as_object();
    snapshot.source_profile = {source_profile.at("width").to_number<int>(),
                               source_profile.at("height").to_number<int>(), 30,
                               PixelFormat::e_Z16};
    const boost::json::object& intrinsics = expected.at("source_intrinsics").as_object();
    snapshot.source_intrinsics.fx = intrinsics.at("fx").to_number<float>();
    snapshot.source_intrinsics.fy = intrinsics.at("fy").to_number<float>();
    snapshot.source_intrinsics.ppx = intrinsics.at("ppx").to_number<float>();
    snapshot.source_intrinsics.ppy = intrinsics.at("ppy").to_number<float>();
    snapshot.requested_stride_pixels = expected.at("requested_stride_pixels").to_number<int>();
    snapshot.stride_pixels = expected.at("stride_pixels").to_number<int>();
    const boost::json::array& matrix =
        expected.at("mount_from_camera_optical_matrix3x4").as_array();
    for (std::size_t index = 0; index < snapshot.mount_from_camera_optical_matrix3x4.size();
         ++index) {
        snapshot.mount_from_camera_optical_matrix3x4.at(index) =
            matrix.at(index).to_number<float>();
    }
    const boost::json::object& point = expected.at("point_camera_optical_m").as_object();
    const boost::json::array& color = expected.at("color_rgb").as_array();
    snapshot.points.push_back(
        {{point.at("x").to_number<float>(), point.at("y").to_number<float>(),
          point.at("z").to_number<float>()},
         {static_cast<std::uint8_t>(color.at(0).to_number<unsigned int>()),
          static_cast<std::uint8_t>(color.at(1).to_number<unsigned int>()),
          static_cast<std::uint8_t>(color.at(2).to_number<unsigned int>())}});
    return snapshot;
}

}  // namespace

TEST(Pcd1, WritesExactLittleEndianHeaderAndRoundTrips) {
    const boost::json::object expected = loadExpectedFixture();
    const PointCloudSnapshot point_cloud = makePointCloud(expected);
    const std::vector<std::uint8_t> bytes = writePcd1V2(point_cloud);
    const std::vector<std::uint8_t> golden_bytes = loadBinaryFixture();
    ASSERT_EQ(bytes.size(), 127U);
    EXPECT_EQ(bytes, golden_bytes);
    EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 8),
              (std::vector<std::uint8_t>{'P', 'C', 'D', '1', 2U, 0U, 0U, 0U}));
    EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin() + 8, bytes.begin() + 16),
              (std::vector<std::uint8_t>{8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U}));
    EXPECT_EQ(bytes.at(44), 0U);
    EXPECT_EQ(bytes.at(45), 0U);
    EXPECT_EQ(bytes.at(46), 0U);
    EXPECT_EQ(bytes.at(47), 0U);

    const PointCloudSnapshot decoded = readPcd1V2(golden_bytes);
    EXPECT_EQ(decoded.identity.frame_number,
              expected.at("frame_number").to_number<std::uint64_t>());
    ASSERT_EQ(decoded.points.size(), 1U);
    EXPECT_FLOAT_EQ(decoded.points.at(0).optical_point_m.at(2), 0.5F);
    EXPECT_EQ(decoded.points.at(0).color_rgb.at(1), 5U);
    EXPECT_EQ(decoded.mount_from_camera_optical_matrix3x4,
              point_cloud.mount_from_camera_optical_matrix3x4);

    const boost::json::object& expected_mount = expected.at("point_mount_m").as_object();
    for (int row = 0; row < 3; ++row) {
        float point_mount = decoded.mount_from_camera_optical_matrix3x4.at(row * 4 + 3);
        for (int column = 0; column < 3; ++column) {
            point_mount += decoded.mount_from_camera_optical_matrix3x4.at(row * 4 + column) *
                           decoded.points.at(0).optical_point_m.at(column);
        }
        const char* coordinate = row == 0 ? "x" : (row == 1 ? "y" : "z");
        EXPECT_NEAR(point_mount, expected_mount.at(coordinate).to_number<float>(), 1.0e-6F);
    }
}

TEST(Pcd1, RejectsMalformedAndUnboundedPayloads) {
    const boost::json::object expected = loadExpectedFixture();
    std::vector<std::uint8_t> bytes = writePcd1V2(makePointCloud(expected));
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    EXPECT_THROW(readPcd1V2(truncated), std::invalid_argument);
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0U);
    EXPECT_THROW(readPcd1V2(trailing), std::invalid_argument);
    std::vector<std::uint8_t> invalid_reserved = bytes;
    invalid_reserved.at(44) = 1U;
    EXPECT_THROW(readPcd1V2(invalid_reserved), std::invalid_argument);

    PointCloudSnapshot invalid = makePointCloud(expected);
    invalid.source_profile.width = 0;
    EXPECT_THROW(writePcd1V2(invalid), std::invalid_argument);
    invalid = makePointCloud(expected);
    invalid.points.at(0).optical_point_m.at(0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(writePcd1V2(invalid), std::invalid_argument);
    invalid = makePointCloud(expected);
    invalid.mount_from_camera_optical_matrix3x4.at(0) = 2.0F;
    EXPECT_THROW(writePcd1V2(invalid), std::invalid_argument);

    std::vector<std::uint8_t> invalid_matrix = bytes;
    invalid_matrix.at(64) = 0U;
    invalid_matrix.at(65) = 0U;
    invalid_matrix.at(66) = 0xC0U;
    invalid_matrix.at(67) = 0x7FU;
    EXPECT_THROW(readPcd1V2(invalid_matrix), std::invalid_argument);
}

}  // namespace nodus_vision
