/**
 * @file jpeg_encoder.hpp
 * @brief immutable RGB frame view를 bounded JPEG bytes로 encode한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_JPEG_ENCODER_HPP_
#define NODUS_VISION_PROVIDER_HTTP_JPEG_ENCODER_HPP_

#include <cstdint>
#include <vector>

#include <nodus_vision/camera_contracts.hpp>

namespace nodus_vision {
std::vector<std::uint8_t> encodeRgbJpeg(const VideoFrameView& frame, int quality);
}

#endif // NODUS_VISION_PROVIDER_HTTP_JPEG_ENCODER_HPP_
