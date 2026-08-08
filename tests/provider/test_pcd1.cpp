/**
 * @file test_pcd1.cpp
 * @brief PCD1 v2 exact byte contract와 invalid payload rejection을 검증한다.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "pcd1.hpp"

namespace nodus_vision {
namespace {

PointCloudSnapshot makePointCloud() {
    PointCloudSnapshot snapshot;
    snapshot.identity.frame_number = 0x0102030405060708ULL;
    snapshot.identity.capture_timestamp_ns = 9;
    snapshot.source_profile = {4, 3, 30, PixelFormat::e_Z16};
    snapshot.source_intrinsics.fx = 2.0F;
    snapshot.source_intrinsics.fy = 3.0F;
    snapshot.source_intrinsics.ppx = 1.0F;
    snapshot.source_intrinsics.ppy = 1.5F;
    snapshot.requested_stride_pixels = 1;
    snapshot.stride_pixels = 2;
    snapshot.mount_from_camera_optical_matrix3x4 = {1.0F, 0.0F, 0.0F, 0.0F,  0.0F, 0.0F,
                                                    1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.06F};
    snapshot.points.push_back({{1.0F, 2.0F, 3.0F}, {4U, 5U, 6U}});
    return snapshot;
}

}  // namespace

TEST(Pcd1, WritesExactLittleEndianHeaderAndRoundTrips) {
    const std::vector<std::uint8_t> bytes = writePcd1V2(makePointCloud());
    ASSERT_EQ(bytes.size(), 127U);
    EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 8),
              (std::vector<std::uint8_t>{'P', 'C', 'D', '1', 2U, 0U, 0U, 0U}));
    EXPECT_EQ(std::vector<std::uint8_t>(bytes.begin() + 8, bytes.begin() + 16),
              (std::vector<std::uint8_t>{8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U}));
    EXPECT_EQ(bytes.at(44), 0U);
    EXPECT_EQ(bytes.at(45), 0U);
    EXPECT_EQ(bytes.at(46), 0U);
    EXPECT_EQ(bytes.at(47), 0U);

    const PointCloudSnapshot decoded = readPcd1V2(bytes);
    EXPECT_EQ(decoded.identity.frame_number, 0x0102030405060708ULL);
    ASSERT_EQ(decoded.points.size(), 1U);
    EXPECT_FLOAT_EQ(decoded.points.at(0).optical_point_m.at(2), 3.0F);
    EXPECT_EQ(decoded.points.at(0).color_rgb.at(1), 5U);
    EXPECT_EQ(decoded.mount_from_camera_optical_matrix3x4,
              makePointCloud().mount_from_camera_optical_matrix3x4);
}

TEST(Pcd1, RejectsMalformedAndUnboundedPayloads) {
    std::vector<std::uint8_t> bytes = writePcd1V2(makePointCloud());
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    EXPECT_THROW(readPcd1V2(truncated), std::invalid_argument);
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0U);
    EXPECT_THROW(readPcd1V2(trailing), std::invalid_argument);
    std::vector<std::uint8_t> invalid_reserved = bytes;
    invalid_reserved.at(44) = 1U;
    EXPECT_THROW(readPcd1V2(invalid_reserved), std::invalid_argument);

    PointCloudSnapshot invalid = makePointCloud();
    invalid.source_profile.width = 0;
    EXPECT_THROW(writePcd1V2(invalid), std::invalid_argument);
    invalid = makePointCloud();
    invalid.points.at(0).optical_point_m.at(0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(writePcd1V2(invalid), std::invalid_argument);
    invalid = makePointCloud();
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
