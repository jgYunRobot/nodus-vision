/** @file query_serializer.hpp @brief immutable frame query JSON response serialization을 제공한다. */
#ifndef NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_
#define NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_
#include <string>
#include <nodus_vision/camera_contracts.hpp>
namespace nodus_vision {
std::string serializePixelPointResult(const PixelPointResult& result);
std::string serializeRoiDepthResult(const RoiDepthResult& result);
}
#endif // NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_
