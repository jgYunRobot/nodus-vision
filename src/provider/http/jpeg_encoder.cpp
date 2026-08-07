/**
 * @file jpeg_encoder.cpp
 * @brief libjpeg-turbo RGB JPEG encoder를 구현한다.
 */

#include "jpeg_encoder.hpp"

#include <csetjmp>
#include <cstdlib>
#include <stdexcept>

#include <jpeglib.h>

namespace nodus_vision {
std::vector<std::uint8_t> encodeRgbJpeg(const VideoFrameView& frame, int quality)
{
    if (frame.format != PixelFormat::e_RGB8 || frame.p_data == nullptr || frame.width <= 0 || frame.height <= 0 || quality < 1 || quality > 100) {
        throw std::invalid_argument("JPEG requires a bounded RGB8 frame and quality.");
    }
    jpeg_compress_struct encoder{};
    jpeg_error_mgr error{};
    encoder.err = jpeg_std_error(&error);
    jpeg_create_compress(&encoder);
    unsigned char* p_output = nullptr;
    unsigned long output_size = 0;
    jpeg_mem_dest(&encoder, &p_output, &output_size);
    encoder.image_width = static_cast<JDIMENSION>(frame.width);
    encoder.image_height = static_cast<JDIMENSION>(frame.height);
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, quality, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    while (encoder.next_scanline < encoder.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(frame.p_data + static_cast<std::size_t>(encoder.next_scanline) * frame.stride_bytes);
        jpeg_write_scanlines(&encoder, &row, 1);
    }
    jpeg_finish_compress(&encoder);
    std::vector<std::uint8_t> result(p_output, p_output + output_size);
    free(p_output);
    jpeg_destroy_compress(&encoder);
    return result;
}
} // namespace nodus_vision
