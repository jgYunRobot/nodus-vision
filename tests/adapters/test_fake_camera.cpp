#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

#include "fake_camera_adapter.hpp"

namespace nodus_vision {

TEST(FakeCameraAdapter, ProducesDeterministicRgbDepthAndGeometry)
{
    FakeCameraAdapter adapter({4, 3, 30, 10U, 7U, false, false, false, "fake"});
    adapter.connectCamera();
    adapter.startStream();
    adapter.advanceFrame();
    const std::shared_ptr<const CapturedFrame> frame = adapter.readFrame(std::chrono::milliseconds(1));
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->getSnapshot().identity.capture_generation, 1U);
    EXPECT_EQ(frame->getSnapshot().identity.frame_number, 10U);
    const std::optional<VideoFrameView> color = frame->getColorFrameView();
    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color->p_data[0], 17U);
    const std::optional<PixelPointResult> point = frame->queryPixelPoint(1, 1);
    ASSERT_TRUE(point.has_value());
    EXPECT_TRUE(point->valid);
    EXPECT_FLOAT_EQ(point->depth_m, 1.3F);
    EXPECT_FLOAT_EQ(point->optical_point_m[0], 0.013F);
    EXPECT_FLOAT_EQ(point->optical_point_m[1], 0.013F);
}

TEST(FakeCameraAdapter, ClampsRoiAndBoundsPointCloud)
{
    FakeCameraAdapter adapter({4, 3, 30, 1U, 1U, true, false, false, "fake"});
    adapter.connectCamera();
    adapter.startStream();
    const std::shared_ptr<const CapturedFrame> frame = adapter.readFrame(std::chrono::milliseconds(1));
    const RoiDepthResult roi = frame->queryDepthInRoi(-1, -1, 3, 3);
    EXPECT_EQ(roi.clamped_roi.width, 2);
    EXPECT_EQ(roi.clamped_roi.height, 2);
    EXPECT_TRUE(roi.valid);
    const PointCloudSnapshot cloud = frame->buildPointCloudSnapshot(3U, 1);
    EXPECT_LE(cloud.points.size(), 3U);
    EXPECT_GE(cloud.stride_pixels, 1);
}

TEST(FakeCameraAdapter, RequiresCallerAdvanceInManualMode)
{
    FakeCameraAdapter adapter({4, 3, 30, 1U, 1U, false, false, false, "fake"});
    adapter.connectCamera();
    adapter.startStream();
    EXPECT_THROW(adapter.readFrame(std::chrono::milliseconds(1)), std::runtime_error);
    adapter.advanceFrame();
    EXPECT_NE(adapter.readFrame(std::chrono::milliseconds(1)), nullptr);
    adapter.stopStream();
    adapter.disconnectCamera();
}

} // namespace nodus_vision
