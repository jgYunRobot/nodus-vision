/**
 * @file pilot_contract_codec.cpp
 * @brief pinned Pilot public lifecycle DTO JSON codec을 구현한다.
 */

#include "pilot_contract_codec.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace nodus_vision {
namespace {

constexpr std::size_t IDENTIFIER_MAX_BYTES = 128U;
constexpr std::uint64_t MAX_INT64_VALUE = 9223372036854775807ULL;

void validateIdentifier(const std::string& value, const char* field) {
    if (value.empty() || value.size() > IDENTIFIER_MAX_BYTES) {
        throw std::invalid_argument(std::string(field) +
                                    " must be a bounded non-empty identifier.");
    }
}

std::uint64_t requireNonNegativeInteger(const boost::json::object& object, const char* key) {
    const boost::json::value* p_value = object.if_contains(key);
    if (p_value == nullptr || (!p_value->is_int64() && !p_value->is_uint64())) {
        throw std::invalid_argument(std::string(key) + " must be a non-negative integer.");
    }
    const std::uint64_t value =
        p_value->is_int64()
            ? (p_value->as_int64() < 0
                   ? throw std::invalid_argument(std::string(key) + " must be non-negative.")
                   : static_cast<std::uint64_t>(p_value->as_int64()))
            : p_value->as_uint64();
    if (value > MAX_INT64_VALUE) {
        throw std::invalid_argument(std::string(key) + " exceeds the public integer range.");
    }
    return value;
}

std::string requireIdentifier(const boost::json::object& object, const char* key) {
    const boost::json::value* p_value = object.if_contains(key);
    if (p_value == nullptr || !p_value->is_string()) {
        throw std::invalid_argument(std::string(key) + " must be an identifier.");
    }
    const std::string value(p_value->as_string());
    validateIdentifier(value, key);
    return value;
}

boost::json::object serializeCommonState(const PilotCommonState& state) {
    if (state.health != "starting" && state.health != "ready" && state.health != "degraded" &&
        state.health != "faulted" && state.health != "stopping") {
        throw std::invalid_argument("Pilot health must be a public CommonState value.");
    }
    if (state.reason.size() > 256U) {
        throw std::invalid_argument("Pilot state reason exceeds the public bound.");
    }
    boost::json::object result;
    result["health"] = state.health;
    result["reason"] =
        state.reason.empty() ? boost::json::value(nullptr) : boost::json::value(state.reason);
    result["details"] = state.details;
    return result;
}

boost::json::object serializeDescriptor(const PilotEndpointDescriptor& descriptor) {
    validateIdentifier(descriptor.descriptor_id, "descriptor_id");
    validateIdentifier(descriptor.capability, "capability");
    if (descriptor.contract_version == 0U || descriptor.contract_version > MAX_INT64_VALUE ||
        descriptor.endpoint.empty() || descriptor.media_type.empty() ||
        descriptor.schema_id.empty()) {
        throw std::invalid_argument("Pilot endpoint descriptor is incomplete.");
    }
    boost::json::object result;
    result["descriptor_id"] = descriptor.descriptor_id;
    result["kind"] = descriptor.is_stream ? "stream" : "service";
    result["capability"] = descriptor.capability;
    result["contract_version"] = descriptor.contract_version;
    result["protocol"] = "http";
    result["endpoint"] = descriptor.endpoint;
    result["media_type"] = descriptor.media_type;
    result["schema_id"] = descriptor.schema_id;
    result["metadata"] = descriptor.metadata;
    if (descriptor.is_stream) {
        result["service"] = nullptr;
        boost::json::object stream;
        stream["clock_domain"] = descriptor.stream.clock_domain;
        stream["stream_group_id"] = descriptor.stream.stream_group_id.empty()
                                        ? boost::json::value(nullptr)
                                        : boost::json::value(descriptor.stream.stream_group_id);
        result["stream"] = std::move(stream);
    } else {
        boost::json::object service;
        service["method"] = descriptor.service.method;
        service["request_schema_id"] =
            descriptor.service.request_schema_id.empty()
                ? boost::json::value(nullptr)
                : boost::json::value(descriptor.service.request_schema_id);
        service["response_schema_id"] =
            descriptor.service.response_schema_id.empty()
                ? boost::json::value(nullptr)
                : boost::json::value(descriptor.service.response_schema_id);
        result["service"] = std::move(service);
        result["stream"] = nullptr;
    }
    return result;
}

bool isInstanceIdValid(const std::string& value) {
    if (value.size() != 39U || value.rfind("vision-", 0U) != 0U) {
        return false;
    }
    for (std::size_t index = 7U; index < value.size(); ++index) {
        const char character = value.at(index);
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string generateInstanceId(const InstanceIdGenerator& generator) {
    if (generator) {
        const std::string value = generator();
        if (!isInstanceIdValid(value)) {
            throw std::invalid_argument("Injected Pilot instance ID has an invalid format.");
        }
        return value;
    }
    std::random_device random_device;
    std::array<unsigned char, 16> bytes{};
    for (unsigned char& byte : bytes) {
        byte = static_cast<unsigned char>(random_device());
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::string result("vision-");
    result.reserve(39U);
    for (const unsigned char byte : bytes) {
        result.push_back(HEX[(byte >> 4U) & 0x0FU]);
        result.push_back(HEX[byte & 0x0FU]);
    }
    return result;
}

std::string serializeRegistrationRequest(const PilotRegistrationRequest& request) {
    validateIdentifier(request.component_id, "component_id");
    if (!isInstanceIdValid(request.instance_id) || request.clock_domain != "monotonic_same_host") {
        throw std::invalid_argument("Pilot registration identity or clock domain is invalid.");
    }
    if (!std::is_sorted(request.capabilities.begin(), request.capabilities.end()) ||
        std::adjacent_find(request.capabilities.begin(), request.capabilities.end()) !=
            request.capabilities.end()) {
        throw std::invalid_argument(
            "Pilot registration capabilities must be stable sorted and unique.");
    }
    boost::json::array capabilities;
    for (const std::string& capability : request.capabilities) {
        validateIdentifier(capability, "capability");
        capabilities.emplace_back(capability);
    }
    boost::json::object result;
    result["component_id"] = request.component_id;
    result["instance_id"] = request.instance_id;
    result["component_type"] = "camera";
    result["protocol_version"] = 1;
    result["supported_schema_versions"] = {1};
    result["capabilities"] = std::move(capabilities);
    result["service_endpoints"] = boost::json::object();
    result["initial_state"] = serializeCommonState(request.initial_state);
    result["started_at"] = request.started_at_ns;
    result["metadata"] = request.metadata;
    result["clock_domain"] = request.clock_domain;
    return boost::json::serialize(result);
}

std::string serializeCatalogPublicationRequest(
    const std::string& publication_id, std::uint64_t expected_catalog_generation,
    const std::vector<PilotEndpointDescriptor>& descriptors) {
    validateIdentifier(publication_id, "publication_id");
    if (expected_catalog_generation > MAX_INT64_VALUE) {
        throw std::invalid_argument("Expected catalog generation exceeds the public range.");
    }
    boost::json::array values;
    std::string previous_id;
    for (const PilotEndpointDescriptor& descriptor : descriptors) {
        if (!previous_id.empty() && descriptor.descriptor_id <= previous_id) {
            throw std::invalid_argument(
                "Pilot catalog descriptors must be stable sorted and unique.");
        }
        previous_id = descriptor.descriptor_id;
        values.emplace_back(serializeDescriptor(descriptor));
    }
    boost::json::object result;
    result["schema_version"] = 1;
    result["publication_id"] = publication_id;
    result["expected_catalog_generation"] = expected_catalog_generation;
    result["descriptors"] = std::move(values);
    return boost::json::serialize(result);
}

PilotRegistrationResponse parseRegistrationResponse(const std::string& response_body) {
    boost::json::error_code error;
    const boost::json::value value = boost::json::parse(response_body, error);
    if (error || !value.is_object()) {
        throw std::invalid_argument("Pilot registration response must be a JSON object.");
    }
    const boost::json::object& object = value.as_object();
    if (object.size() != 7U) {
        throw std::invalid_argument("Pilot registration response has unknown or missing fields.");
    }
    const std::uint64_t protocol = requireNonNegativeInteger(object, "accepted_protocol_version");
    if (protocol != 1U) {
        throw std::invalid_argument("Pilot did not accept protocol version 1.");
    }
    const boost::json::value* p_versions = object.if_contains("accepted_schema_versions");
    if (p_versions == nullptr || !p_versions->is_array() || p_versions->as_array().empty()) {
        throw std::invalid_argument("Pilot accepted schema versions are invalid.");
    }
    bool accepted_schema_one = false;
    for (const boost::json::value& version : p_versions->as_array()) {
        if (!version.is_int64() && !version.is_uint64()) {
            throw std::invalid_argument("Pilot accepted schema versions are invalid.");
        }
        const std::uint64_t numeric_version =
            version.is_int64()
                ? (version.as_int64() < 0
                       ? throw std::invalid_argument("Pilot accepted schema versions are invalid.")
                       : static_cast<std::uint64_t>(version.as_int64()))
                : version.as_uint64();
        if (numeric_version > 16U) {
            throw std::invalid_argument("Pilot accepted schema versions are invalid.");
        }
        accepted_schema_one = accepted_schema_one || numeric_version == 1U;
    }
    PilotRegistrationResponse response;
    response.session_id = requireIdentifier(object, "session_id");
    response.server_instance_id = requireIdentifier(object, "server_instance_id");
    response.heartbeat_interval_ms = requireNonNegativeInteger(object, "heartbeat_interval_ms");
    response.lease_timeout_ms = requireNonNegativeInteger(object, "lease_timeout_ms");
    response.server_time_ns = requireNonNegativeInteger(object, "server_time");
    if (!accepted_schema_one || response.heartbeat_interval_ms == 0U ||
        response.lease_timeout_ms == 0U ||
        response.heartbeat_interval_ms >= response.lease_timeout_ms) {
        throw std::invalid_argument(
            "Pilot registration response has an invalid lease configuration.");
    }
    return response;
}

PilotCatalogAcceptedResponse parseCatalogAcceptedResponse(const std::string& response_body,
                                                          const std::string& component_id,
                                                          int descriptor_count) {
    boost::json::error_code error;
    const boost::json::value value = boost::json::parse(response_body, error);
    if (error || !value.is_object()) {
        throw std::invalid_argument("Pilot catalog response must be a JSON object.");
    }
    const boost::json::object& object = value.as_object();
    if (object.size() != 7U || !object.if_contains("status") || object.at("status") != "accepted") {
        throw std::invalid_argument("Pilot catalog response is invalid.");
    }
    PilotCatalogAcceptedResponse response;
    response.server_instance_id = requireIdentifier(object, "server_instance_id");
    response.component_id = requireIdentifier(object, "component_id");
    response.catalog_generation = requireNonNegativeInteger(object, "catalog_generation");
    const std::uint64_t parsed_count = requireNonNegativeInteger(object, "descriptor_count");
    const std::uint64_t session_generation =
        requireNonNegativeInteger(object, "session_generation");
    const std::uint64_t catalog_revision = requireNonNegativeInteger(object, "catalog_revision");
    if (response.component_id != component_id || response.catalog_generation == 0U ||
        session_generation == 0U || catalog_revision == 0U ||
        parsed_count != static_cast<std::uint64_t>(descriptor_count)) {
        throw std::invalid_argument("Pilot catalog response does not match the publication.");
    }
    response.descriptor_count = descriptor_count;
    return response;
}

std::string serializeHeartbeatRequest(std::uint64_t sequence) {
    if (sequence > MAX_INT64_VALUE) {
        throw std::invalid_argument("Heartbeat sequence exceeds the public range.");
    }
    boost::json::object result;
    result["sequence"] = sequence;
    return boost::json::serialize(result);
}

std::string serializeStateUpdateRequest(std::uint64_t sequence, const PilotCommonState& state) {
    if (sequence > MAX_INT64_VALUE) {
        throw std::invalid_argument("State sequence exceeds the public range.");
    }
    boost::json::object result;
    result["sequence"] = sequence;
    result["state"] = serializeCommonState(state);
    return boost::json::serialize(result);
}

std::string serializeDisconnectRequest() { return "{}"; }

}  // namespace nodus_vision
