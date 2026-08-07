/** @file pcd1.hpp @brief PCD1 v2 binary point-cloud codec을 제공한다. */
#ifndef NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_
#define NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_
#include <cstdint>
#include <vector>
#include <nodus_vision/camera_contracts.hpp>
namespace nodus_vision {
std::vector<std::uint8_t> writePcd1V2(const PointCloudSnapshot& snapshot);
PointCloudSnapshot readPcd1V2(const std::vector<std::uint8_t>& bytes);
}
#endif // NODUS_VISION_PROVIDER_HTTP_PCD1_HPP_
