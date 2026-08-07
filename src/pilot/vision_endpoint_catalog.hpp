/**
 * @file vision_endpoint_catalog.hpp
 * @brief Vision direct data-plane의 deterministic Pilot catalog을 제공한다.
 */

#ifndef NODUS_VISION_PILOT_VISION_ENDPOINT_CATALOG_HPP_
#define NODUS_VISION_PILOT_VISION_ENDPOINT_CATALOG_HPP_

#include <string>
#include <vector>

#include "pilot_contract_codec.hpp"
#include "vision_config.hpp"

namespace nodus_vision {

/** @brief full-replacement Vision endpoint catalog 결과다. */
struct VisionEndpointCatalog {
    std::vector<PilotEndpointDescriptor> descriptors;
    std::vector<std::string> capabilities;
};

/** @brief Vision config에서 Pilot endpoint descriptors를 결정적으로 생성한다. */
class VisionEndpointCatalogBuilder {
   public:
    /** @brief configured direct endpoint만 stable sorted catalog으로 생성한다. */
    VisionEndpointCatalog buildCatalog(const VisionConfig& config) const;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_PILOT_VISION_ENDPOINT_CATALOG_HPP_
