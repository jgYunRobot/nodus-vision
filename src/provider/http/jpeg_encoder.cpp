/**
 * @file jpeg_encoder.cpp
 * @brief libjpeg-turbo RGB JPEG encoder를 구현한다.
 */

#include "jpeg_encoder.hpp"

#include <jpeglib.h>

#include <csetjmp>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace nodus_vision {
namespace {

struct JpegErrorManager : jpeg_error_mgr {
    std::jmp_buf jump_buffer;
    char message[JMSG_LENGTH_MAX]{};
};

struct JpegOutputDeleter {
    void operator()(unsigned char* p_output) const noexcept { std::free(p_output); }
};

extern "C" void handleJpegError(j_common_ptr p_common) {
    auto* p_error = static_cast<JpegErrorManager*>(p_common->err);
    p_common->err->format_message(p_common, p_error->message);
    std::longjmp(p_error->jump_buffer, 1);
}

}  // namespace

std::vector<std::uint8_t> encodeRgbJpeg(const VideoFrameView& frame, int quality) {
    const std::int64_t minimum_stride = static_cast<std::int64_t>(frame.width) * 3;
    if (frame.format != PixelFormat::e_RGB8 || frame.p_data == nullptr || frame.width <= 0 ||
        frame.height <= 0 || frame.stride_bytes < minimum_stride || quality < 1 || quality > 100 ||
        static_cast<std::uint64_t>(frame.width) > std::numeric_limits<JDIMENSION>::max() ||
        static_cast<std::uint64_t>(frame.height) > std::numeric_limits<JDIMENSION>::max()) {
        throw std::invalid_argument("JPEG requires a bounded RGB8 frame, stride, and quality.");
    }

    jpeg_compress_struct encoder{};
    JpegErrorManager error{};
    encoder.err = jpeg_std_error(&error);
    error.error_exit = handleJpegError;
    unsigned char* p_output{nullptr};
    unsigned long output_size{0};
    if (setjmp(error.jump_buffer) != 0) {
        jpeg_destroy_compress(&encoder);
        std::free(p_output);
        throw std::runtime_error(std::string("JPEG encoding failed: ") + error.message);
    }

    jpeg_create_compress(&encoder);
    jpeg_mem_dest(&encoder, &p_output, &output_size);
    encoder.image_width = static_cast<JDIMENSION>(frame.width);
    encoder.image_height = static_cast<JDIMENSION>(frame.height);
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, quality, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    while (encoder.next_scanline < encoder.image_height) {
        JSAMPROW row = const_cast<JSAMPLE*>(
            frame.p_data + static_cast<std::size_t>(encoder.next_scanline) * frame.stride_bytes);
        jpeg_write_scanlines(&encoder, &row, 1U);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    std::unique_ptr<unsigned char, JpegOutputDeleter> p_output_owner(p_output);
    if (p_output_owner == nullptr || output_size == 0UL) {
        throw std::runtime_error("JPEG encoding returned an empty payload.");
    }
    std::vector<std::uint8_t> result(p_output_owner.get(), p_output_owner.get() + output_size);
    return result;
}

}  // namespace nodus_vision
