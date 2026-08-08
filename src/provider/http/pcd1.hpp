/**
 * @file pcd1.hpp
 * @brief PCD1 v2 binary point-cloud codec을 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_
#define NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_

#include <cstdint>
#include <nodus_vision/camera_contracts.hpp>
#include <vector>

namespace nodus_vision {

/** @brief raw optical point와 static mount matrix를 PCD1 v2로 직렬화한다. */
std::vector<std::uint8_t> writePcd1V2(const PointCloudSnapshot& snapshot);
/** @brief PCD1 v2의 raw optical point와 static mount matrix를 역직렬화한다. */
PointCloudSnapshot readPcd1V2(const std::vector<std::uint8_t>& bytes);

}  // namespace nodus_vision

#endif  // NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_
