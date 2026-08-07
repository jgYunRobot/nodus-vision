#include <gtest/gtest.h>
#include <jpeglib.h>

#include <chrono>
#include <csetjmp>
#include <stdexcept>
#include <vector>

#include "fake_camera_adapter.hpp"
#include "jpeg_encoder.hpp"

namespace nodus_vision {
TEST(JpegEncoder, EncodesDeterministicFakeRgbFrame) {
    FakeCameraAdapter adapter({4, 3, 30, 1U, 7U, true, false, false, "fake"});
    adapter.connectCamera();
    adapter.startStream();
    const std::shared_ptr<const CapturedFrame> frame =
        adapter.readFrame(std::chrono::milliseconds(1));
    const std::optional<VideoFrameView> view = frame->getColorFrameView();
    ASSERT_TRUE(view.has_value());
    const std::vector<std::uint8_t> jpeg = encodeRgbJpeg(*view, 90);
    ASSERT_GT(jpeg.size(), 4U);
    EXPECT_EQ(jpeg.at(0), 0xffU);
    EXPECT_EQ(jpeg.at(1), 0xd8U);
    jpeg_decompress_struct decoder{};
    jpeg_error_mgr error{};
    decoder.err = jpeg_std_error(&error);
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, jpeg.data(), jpeg.size());
    ASSERT_EQ(jpeg_read_header(&decoder, TRUE), JPEG_HEADER_OK);
    EXPECT_EQ(decoder.image_width, 4U);
    EXPECT_EQ(decoder.image_height, 3U);
    jpeg_destroy_decompress(&decoder);
}

TEST(JpegEncoder, RejectsInvalidStrideAndQuality) {
    std::vector<std::uint8_t> pixels(12U, 0U);
    VideoFrameView view;
    view.width = 2;
    view.height = 2;
    view.stride_bytes = 5;
    view.format = PixelFormat::e_RGB8;
    view.p_data = pixels.data();
    EXPECT_THROW(encodeRgbJpeg(view, 90), std::invalid_argument);
    view.stride_bytes = 6;
    EXPECT_THROW(encodeRgbJpeg(view, 0), std::invalid_argument);
}
}  // namespace nodus_vision
