/**
 * @file query_serializer.hpp
 * @brief immutable frame query JSON response serialization을 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_
#define NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_

#include <nodus_vision/camera_contracts.hpp>
#include <string>

namespace nodus_vision {

/** @brief pixel query 결과를 versioned JSON으로 직렬화한다. */
std::string serializePixelPointResult(const PixelPointResult& result);
/** @brief ROI depth query 결과를 versioned JSON으로 직렬화한다. */
std::string serializeRoiDepthResult(const RoiDepthResult& result);

}  // namespace nodus_vision

#endif  // NODUS_VISION_PROVIDER_HTTP_QUERY_SERIALIZER_HPP_
