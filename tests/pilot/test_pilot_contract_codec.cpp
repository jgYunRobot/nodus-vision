/**
 * @file test_pilot_contract_codec.cpp
 * @brief Pilot public lifecycle JSON codec을 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <stdexcept>

#include "pilot_contract_codec.hpp"

namespace nodus_vision {
namespace {

PilotRegistrationRequest makeRegistrationRequest() {
    PilotRegistrationRequest request;
    request.component_id = "camera.fake";
    request.instance_id = "vision-00112233445566778899aabbccddeeff";
    request.capabilities = {"camera.health.get", "camera.snapshot.color"};
    request.initial_state = {"ready", "", {}};
    request.started_at_ns = 42U;
    request.metadata["device_id"] = "fake";
    request.clock_domain = "monotonic_same_host";
    return request;
}

}  // namespace

TEST(PilotContractCodec, SerializesExactGenericCameraRegistration) {
    const boost::json::value value =
        boost::json::parse(serializeRegistrationRequest(makeRegistrationRequest()));
    ASSERT_TRUE(value.is_object());
    const boost::json::object& object = value.as_object();
    EXPECT_EQ(object.at("component_type"), "camera");
    EXPECT_TRUE(object.at("service_endpoints").as_object().empty());
    EXPECT_EQ(object.at("supported_schema_versions").as_array().at(0), 1);
    EXPECT_EQ(object.at("initial_state").as_object().at("reason"), nullptr);
}

TEST(PilotContractCodec, RejectsUnsortedCapabilitiesAndInvalidResponses) {
    PilotRegistrationRequest request = makeRegistrationRequest();
    request.capabilities = {"camera.snapshot.color", "camera.health.get"};
    EXPECT_THROW(serializeRegistrationRequest(request), std::invalid_argument);

    const std::string valid_response =
        R"json({"session_id":"opaque-session","server_instance_id":"pilot-instance","accepted_protocol_version":1,"accepted_schema_versions":[1],"heartbeat_interval_ms":100,"lease_timeout_ms":200,"server_time":0})json";
    const PilotRegistrationResponse response = parseRegistrationResponse(valid_response);
    EXPECT_EQ(response.heartbeat_interval_ms, 100U);
    EXPECT_THROW(
        parseRegistrationResponse(
            R"json({"session_id":"opaque-session","server_instance_id":"pilot-instance","accepted_protocol_version":1,"accepted_schema_versions":[1],"heartbeat_interval_ms":200,"lease_timeout_ms":100,"server_time":0})json"),
        std::invalid_argument);
}

TEST(PilotContractCodec, UsesValidatedInstanceIdentitySeam) {
    EXPECT_EQ(generateInstanceId([]() { return "vision-00112233445566778899aabbccddeeff"; }),
              "vision-00112233445566778899aabbccddeeff");
    EXPECT_THROW(generateInstanceId([]() { return "not-an-instance"; }), std::invalid_argument);
}

}  // namespace nodus_vision
