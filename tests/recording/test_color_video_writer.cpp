/** @file test_color_video_writer.cpp @brief synthetic RGB H.264 writer를 검증한다. */

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
}

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

#include "color_video_writer.hpp"

namespace nodus_vision {
namespace {

class TemporaryOutputDirectory {
   public:
    TemporaryOutputDirectory() {
        char template_path[] = "/tmp/nodus-vision-video-XXXXXX";
        char* created = mkdtemp(template_path);
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary video directory.");
        }
        m_path = created;
    }
    ~TemporaryOutputDirectory() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& getPath() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

VideoFrameView makeFrame(int width, int height, std::uint8_t seed) {
    auto p_pixels = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(width * height * 3), seed);
    VideoFrameView frame;
    frame.width = width;
    frame.height = height;
    frame.stride_bytes = width * 3;
    frame.format = PixelFormat::e_RGB8;
    frame.owner = std::shared_ptr<const void>(p_pixels, static_cast<const void*>(p_pixels.get()));
    frame.p_data = p_pixels->data();
    return frame;
}

struct DecodedVideo {
    AVCodecID codec_id{AV_CODEC_ID_NONE};
    int extra_data_size{0};
    int width{0};
    int height{0};
    AVPixelFormat pixel_format{AV_PIX_FMT_NONE};
    int packet_count{0};
    int packet_bytes{0};
    int frame_count{0};
};

DecodedVideo decodeVideo(const std::filesystem::path& path) {
    AVFormatContext* p_format_context = nullptr;
    if (avformat_open_input(&p_format_context, path.c_str(), nullptr, nullptr) != 0 ||
        avformat_find_stream_info(p_format_context, nullptr) != 0) {
        throw std::runtime_error("Cannot open synthetic MP4 for decode validation.");
    }
    const int stream_index =
        av_find_best_stream(p_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        throw std::runtime_error("Synthetic MP4 has no video stream.");
    }
    DecodedVideo decoded;
    const AVCodecParameters* p_parameters = p_format_context->streams[stream_index]->codecpar;
    decoded.codec_id = p_parameters->codec_id;
    decoded.extra_data_size = p_parameters->extradata_size;
    decoded.width = p_parameters->width;
    decoded.height = p_parameters->height;
    decoded.pixel_format = static_cast<AVPixelFormat>(p_parameters->format);
    const AVCodec* p_codec = avcodec_find_decoder(p_parameters->codec_id);
    if (p_codec == nullptr) {
        throw std::runtime_error("Synthetic MP4 decoder is unavailable.");
    }
    AVCodecContext* p_codec_context = avcodec_alloc_context3(p_codec);
    if (p_codec_context == nullptr ||
        avcodec_parameters_to_context(p_codec_context,
                                      p_format_context->streams[stream_index]->codecpar) != 0 ||
        avcodec_open2(p_codec_context, p_codec, nullptr) != 0) {
        throw std::runtime_error("Cannot initialize synthetic MP4 decoder.");
    }
    AVPacket* p_packet = av_packet_alloc();
    AVFrame* p_frame = av_frame_alloc();
    while (av_read_frame(p_format_context, p_packet) >= 0) {
        if (p_packet->stream_index == stream_index) {
            ++decoded.packet_count;
            decoded.packet_bytes += p_packet->size;
            if (avcodec_send_packet(p_codec_context, p_packet) < 0) {
                throw std::runtime_error("Cannot submit synthetic MP4 packet.");
            }
            int result = 0;
            while ((result = avcodec_receive_frame(p_codec_context, p_frame)) >= 0) {
                ++decoded.frame_count;
                av_frame_unref(p_frame);
            }
            if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
                throw std::runtime_error("Cannot decode synthetic MP4 packet.");
            }
        }
        av_packet_unref(p_packet);
    }
    if (avcodec_send_packet(p_codec_context, nullptr) < 0) {
        throw std::runtime_error("Cannot flush synthetic MP4 decoder.");
    }
    int result = 0;
    while ((result = avcodec_receive_frame(p_codec_context, p_frame)) >= 0) {
        ++decoded.frame_count;
        av_frame_unref(p_frame);
    }
    if (result != AVERROR_EOF) {
        throw std::runtime_error("Cannot flush synthetic MP4 decoder.");
    }
    av_frame_free(&p_frame);
    av_packet_free(&p_packet);
    avcodec_free_context(&p_codec_context);
    avformat_close_input(&p_format_context);
    return decoded;
}

}  // namespace

TEST(ColorVideoWriter, MatchesDecoderVisibleFrameCountForShortSyntheticVideos) {
    constexpr int k_width = 64;
    constexpr int k_height = 64;
    for (const int count : {1, 2, 5}) {
        TemporaryOutputDirectory directory;
        const std::filesystem::path output = directory.getPath() / "color.mp4";
        {
            ColorVideoWriter writer({output, k_width, k_height, 30, 100000});
            for (int index = 0; index < count; ++index) {
                EXPECT_EQ(writer.writeFrame(
                              makeFrame(k_width, k_height, static_cast<std::uint8_t>(index))),
                          static_cast<std::uint64_t>(index));
            }
            writer.finalize();
        }
        EXPECT_TRUE(std::filesystem::is_regular_file(output));
        const DecodedVideo decoded = decodeVideo(output);
        EXPECT_EQ(decoded.codec_id, AV_CODEC_ID_H264);
        EXPECT_GT(decoded.extra_data_size, 0);
        EXPECT_EQ(decoded.width, k_width);
        EXPECT_EQ(decoded.height, k_height);
        EXPECT_EQ(decoded.pixel_format, AV_PIX_FMT_YUV420P);
        EXPECT_EQ(decoded.packet_count, count);
        EXPECT_GT(decoded.packet_bytes, 0);
        EXPECT_EQ(decoded.frame_count, count);
    }
}

TEST(ColorVideoWriter, RejectsInvalidProfileAndFrameStride) {
    TemporaryOutputDirectory directory;
    EXPECT_THROW(ColorVideoWriter({directory.getPath() / "odd.mp4", 3, 4, 30, 100000}),
                 std::invalid_argument);
    ColorVideoWriter writer({directory.getPath() / "color.mp4", 4, 4, 30, 100000});
    VideoFrameView frame = makeFrame(4, 4, 0U);
    frame.stride_bytes = 1;
    EXPECT_THROW(writer.writeFrame(frame), std::invalid_argument);
}

}  // namespace nodus_vision
