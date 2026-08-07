/**
 * @file pilot_contract_codec.hpp
 * @brief pinned Pilot public lifecycle DTO의 JSON codec을 제공한다.
 */

#ifndef NODUS_VISION_PILOT_CONTRACT_CODEC_HPP_
#define NODUS_VISION_PILOT_CONTRACT_CODEC_HPP_

#include <boost/json.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nodus_vision {

/** @brief Pilot CommonState public DTO다. */
struct PilotCommonState {
    std::string health;
    std::string reason;
    boost::json::object details;
};

/** @brief Pilot endpoint service contract DTO다. */
struct PilotServiceContract {
    std::string method;
    std::string request_schema_id;
    std::string response_schema_id;
};

/** @brief Pilot endpoint stream contract DTO다. */
struct PilotStreamContract {
    std::string clock_domain;
    std::string stream_group_id;
};

/** @brief Pilot endpoint descriptor public DTO다. */
struct PilotEndpointDescriptor {
    std::string descriptor_id;
    std::string kind;
    std::string capability;
    std::uint64_t contract_version{1U};
    std::string endpoint;
    std::string media_type;
    std::string schema_id;
    boost::json::object metadata;
    bool is_stream{false};
    PilotServiceContract service;
    PilotStreamContract stream;
};

/** @brief generic component registration request DTO다. */
struct PilotRegistrationRequest {
    std::string component_id;
    std::string instance_id;
    std::vector<std::string> capabilities;
    PilotCommonState initial_state;
    std::uint64_t started_at_ns{0U};
    boost::json::object metadata;
    std::string clock_domain;
};

/** @brief strict validated registration response DTO다. */
struct PilotRegistrationResponse {
    std::string session_id;
    std::string server_instance_id;
    std::uint64_t heartbeat_interval_ms{0U};
    std::uint64_t lease_timeout_ms{0U};
    std::uint64_t server_time_ns{0U};
};

/** @brief strict validated endpoint catalog accepted response DTO다. */
struct PilotCatalogAcceptedResponse {
    std::string server_instance_id;
    std::string component_id;
    std::uint64_t catalog_generation{0U};
    int descriptor_count{0};
};

/** @brief strict validated lifecycle accepted response DTO다. */
struct PilotLifecycleAcceptedResponse {
    std::string server_instance_id;
};

/** @brief deterministic production/test instance ID generator seam이다. */
using InstanceIdGenerator = std::function<std::string()>;

/** @brief new process instance identity를 생성하거나 injected identity를 검증한다. */
std::string generateInstanceId(const InstanceIdGenerator& generator = {});
/** @brief registration request를 exact public JSON으로 serialize한다. */
std::string serializeRegistrationRequest(const PilotRegistrationRequest& request);
/** @brief endpoint catalog publication request를 exact public JSON으로 serialize한다. */
std::string serializeCatalogPublicationRequest(
    const std::string& publication_id, std::uint64_t expected_catalog_generation,
    const std::vector<PilotEndpointDescriptor>& descriptors);
/** @brief strict public registration response를 parse하고 validate한다. */
PilotRegistrationResponse parseRegistrationResponse(const std::string& response_body);
/** @brief strict public catalog accepted response를 parse하고 validate한다. */
PilotCatalogAcceptedResponse parseCatalogAcceptedResponse(const std::string& response_body,
                                                          const std::string& component_id,
                                                          const std::string& server_instance_id,
                                                          int descriptor_count);
/** @brief strict public lifecycle accepted response를 parse하고 validate한다. */
PilotLifecycleAcceptedResponse parseLifecycleAcceptedResponse(const std::string& response_body);
/** @brief heartbeat request를 serialize한다. */
std::string serializeHeartbeatRequest(std::uint64_t sequence);
/** @brief coarse state update request를 serialize한다. */
std::string serializeStateUpdateRequest(std::uint64_t sequence, const PilotCommonState& state);
/** @brief empty disconnect request를 serialize한다. */
std::string serializeDisconnectRequest();

}  // namespace nodus_vision

#endif  // NODUS_VISION_PILOT_CONTRACT_CODEC_HPP_
