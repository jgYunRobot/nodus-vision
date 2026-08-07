/**
 * @file color_video_writer.hpp
 * @brief RGB24 frame을 fixed-profile H.264 MP4로 기록하는 writer를 제공한다.
 */

#ifndef NODUS_VISION_RECORDING_COLOR_VIDEO_WRITER_HPP_
#define NODUS_VISION_RECORDING_COLOR_VIDEO_WRITER_HPP_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <nodus_vision/camera_contracts.hpp>

namespace nodus_vision {

/** @brief one RGB recording stream의 immutable writer configuration이다. */
struct ColorVideoWriterConfig {
    std::filesystem::path output_path;
    int width{0};
    int height{0};
    int fps{0};
    int bit_rate_bps{0};
};

/** @brief worker-owned RGB24-to-YUV420P H.264 MP4 writer다. */
class ColorVideoWriter {
   public:
    /** @brief output path와 fixed stream profile을 검증하고 encoder를 연다. */
    explicit ColorVideoWriter(ColorVideoWriterConfig config);
    ~ColorVideoWriter();
    ColorVideoWriter(const ColorVideoWriter&) = delete;
    ColorVideoWriter& operator=(const ColorVideoWriter&) = delete;

    /** @brief immutable RGB frame을 순차 PTS로 제출하고 input index를 반환한다. */
    std::uint64_t writeFrame(const VideoFrameView& frame);
    /** @brief delayed packet과 MP4 trailer를 flush한다. */
    void finalize();

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_COLOR_VIDEO_WRITER_HPP_
