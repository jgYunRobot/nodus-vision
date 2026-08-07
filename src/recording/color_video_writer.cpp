/**
 * @file color_video_writer.cpp
 * @brief libavcodec/libswscale 기반 H.264 MP4 writer를 구현한다.
 */

#include "color_video_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <stdexcept>
#include <string>
#include <utility>

namespace nodus_vision {
namespace {

void checkFfmpegResult(int result, const char* operation) {
    if (result >= 0) {
        return;
    }
    char error_buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, error_buffer, sizeof(error_buffer));
    throw std::runtime_error(std::string(operation) + ": " + error_buffer);
}

}  // namespace

class ColorVideoWriter::Impl {
   public:
    explicit Impl(ColorVideoWriterConfig config) : m_config(std::move(config)) {
        try {
            initialize();
        } catch (...) {
            releaseResources();
            throw;
        }
    }

    ~Impl() {
        if (!m_finalized && m_header_written) {
            try {
                finalize();
            } catch (...) {
            }
        }
        releaseResources();
    }

    void initialize() {
        if (m_config.output_path.empty() || m_config.width <= 0 || m_config.height <= 0 ||
            m_config.width % 2 != 0 || m_config.height % 2 != 0 || m_config.fps <= 0 ||
            m_config.bit_rate_bps <= 0) {
            throw std::invalid_argument("Color writer profile is invalid.");
        }
        checkFfmpegResult(avformat_alloc_output_context2(&m_p_format_context, nullptr, "mp4",
                                                         m_config.output_path.c_str()),
                          "Cannot allocate MP4 output context");
        const AVCodec* p_codec = avcodec_find_encoder_by_name("libx264");
        if (p_codec == nullptr) {
            throw std::runtime_error("FFmpeg libx264 encoder is unavailable.");
        }
        m_p_codec_context = avcodec_alloc_context3(p_codec);
        if (m_p_codec_context == nullptr) {
            throw std::runtime_error("Cannot allocate H.264 codec context.");
        }
        m_p_codec_context->codec_id = p_codec->id;
        m_p_codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
        m_p_codec_context->width = m_config.width;
        m_p_codec_context->height = m_config.height;
        m_p_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
        m_p_codec_context->time_base = AVRational{1, m_config.fps};
        m_p_codec_context->framerate = AVRational{m_config.fps, 1};
        m_p_codec_context->bit_rate = m_config.bit_rate_bps;
        m_p_codec_context->gop_size = m_config.fps;
        m_p_codec_context->max_b_frames = 0;
        if ((m_p_format_context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
            m_p_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        checkFfmpegResult(av_opt_set(m_p_codec_context->priv_data, "preset", "veryfast", 0),
                          "Cannot configure H.264 preset");
        checkFfmpegResult(av_opt_set(m_p_codec_context->priv_data, "tune", "zerolatency", 0),
                          "Cannot configure H.264 tune");
        checkFfmpegResult(avcodec_open2(m_p_codec_context, p_codec, nullptr),
                          "Cannot open H.264 encoder");
        m_p_stream = avformat_new_stream(m_p_format_context, nullptr);
        if (m_p_stream == nullptr) {
            throw std::runtime_error("Cannot create MP4 video stream.");
        }
        m_p_stream->time_base = m_p_codec_context->time_base;
        m_p_stream->avg_frame_rate = m_p_codec_context->framerate;
        checkFfmpegResult(avcodec_parameters_from_context(m_p_stream->codecpar, m_p_codec_context),
                          "Cannot copy H.264 stream parameters");
        checkFfmpegResult(
            avio_open(&m_p_format_context->pb, m_config.output_path.c_str(), AVIO_FLAG_WRITE),
            "Cannot open MP4 output");
        checkFfmpegResult(avformat_write_header(m_p_format_context, nullptr),
                          "Cannot write MP4 header");
        m_header_written = true;
        m_p_frame = av_frame_alloc();
        m_p_packet = av_packet_alloc();
        if (m_p_frame == nullptr || m_p_packet == nullptr) {
            throw std::runtime_error("Cannot allocate FFmpeg frame or packet.");
        }
        m_p_frame->format = AV_PIX_FMT_YUV420P;
        m_p_frame->width = m_config.width;
        m_p_frame->height = m_config.height;
        checkFfmpegResult(av_frame_get_buffer(m_p_frame, 32), "Cannot allocate YUV frame buffer");
        m_p_sws_context = sws_getContext(m_config.width, m_config.height, AV_PIX_FMT_RGB24,
                                         m_config.width, m_config.height, AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (m_p_sws_context == nullptr) {
            throw std::runtime_error("Cannot allocate RGB-to-YUV converter.");
        }
    }

    void releaseResources() noexcept {
        sws_freeContext(m_p_sws_context);
        av_frame_free(&m_p_frame);
        av_packet_free(&m_p_packet);
        avcodec_free_context(&m_p_codec_context);
        if (m_p_format_context != nullptr && m_p_format_context->pb != nullptr) {
            avio_closep(&m_p_format_context->pb);
        }
        avformat_free_context(m_p_format_context);
        m_p_format_context = nullptr;
    }

    std::uint64_t writeFrame(const VideoFrameView& frame) {
        if (m_finalized || frame.format != PixelFormat::e_RGB8 || frame.width != m_config.width ||
            frame.height != m_config.height || frame.stride_bytes < m_config.width * 3 ||
            frame.p_data == nullptr || frame.owner == nullptr) {
            throw std::invalid_argument("Color frame does not match the recording profile.");
        }
        checkFfmpegResult(av_frame_make_writable(m_p_frame), "Cannot make YUV frame writable");
        const std::uint8_t* input_data[] = {frame.p_data};
        const int input_stride[] = {frame.stride_bytes};
        if (sws_scale(m_p_sws_context, input_data, input_stride, 0, m_config.height,
                      m_p_frame->data, m_p_frame->linesize) != m_config.height) {
            throw std::runtime_error("Cannot convert RGB frame to YUV.");
        }
        m_p_frame->pts = static_cast<std::int64_t>(m_submitted_frame_count);
        m_p_frame->duration = 1;
        checkFfmpegResult(avcodec_send_frame(m_p_codec_context, m_p_frame),
                          "Cannot submit H.264 frame");
        writeAvailablePackets();
        return m_submitted_frame_count++;
    }

    void finalize() {
        if (m_finalized) {
            return;
        }
        checkFfmpegResult(avcodec_send_frame(m_p_codec_context, nullptr),
                          "Cannot flush H.264 encoder");
        writeAvailablePackets();
        checkFfmpegResult(av_write_trailer(m_p_format_context), "Cannot write MP4 trailer");
        avio_flush(m_p_format_context->pb);
        checkFfmpegResult(avio_closep(&m_p_format_context->pb), "Cannot close MP4 output");
        m_finalized = true;
    }

   private:
    void writeAvailablePackets() {
        while (true) {
            const int result = avcodec_receive_packet(m_p_codec_context, m_p_packet);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checkFfmpegResult(result, "Cannot receive H.264 packet");
            av_packet_rescale_ts(m_p_packet, m_p_codec_context->time_base, m_p_stream->time_base);
            m_p_packet->stream_index = m_p_stream->index;
            checkFfmpegResult(av_interleaved_write_frame(m_p_format_context, m_p_packet),
                              "Cannot write MP4 packet");
            av_packet_unref(m_p_packet);
        }
    }

    ColorVideoWriterConfig m_config;
    AVFormatContext* m_p_format_context{nullptr};
    AVCodecContext* m_p_codec_context{nullptr};
    AVStream* m_p_stream{nullptr};
    AVFrame* m_p_frame{nullptr};
    AVPacket* m_p_packet{nullptr};
    SwsContext* m_p_sws_context{nullptr};
    std::uint64_t m_submitted_frame_count{0U};
    bool m_header_written{false};
    bool m_finalized{false};
};

ColorVideoWriter::ColorVideoWriter(ColorVideoWriterConfig config)
    : m_p_impl(std::make_unique<Impl>(std::move(config))) {}
ColorVideoWriter::~ColorVideoWriter() = default;
std::uint64_t ColorVideoWriter::writeFrame(const VideoFrameView& frame) {
    return m_p_impl->writeFrame(frame);
}
void ColorVideoWriter::finalize() { m_p_impl->finalize(); }

}  // namespace nodus_vision
