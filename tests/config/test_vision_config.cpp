#include <gtest/gtest.h>

#include <stdexcept>

#include "vision_config.hpp"

namespace nodus_vision {
namespace {

constexpr const char* VALID_CONFIG = R"json({
  "schema_version": 1,
  "device_id": "fake_top",
  "component_id": "camera.fake_top",
  "device": {"adapter": "fake", "fake": {"width": 4, "height": 3, "fps": 30, "start_frame_number": 1, "pattern_seed": 7}},
  "calibration": {"calibration_id": "fake_v1", "sensor_frame": "fake_optical", "mount_frame": "fake_mount", "mount_local_transform": {"x": 0.0, "y": 0.0, "z": 0.06, "r1": 0.0, "r2": 0.0, "r3": 0.0, "euler_type": "XYZ"}},
  "provider": {"bind_host": "127.0.0.1", "port": 0, "advertised_base_url": "http://127.0.0.1:8900", "max_connections": 4, "max_stream_clients": 2, "request_timeout_ms": 1000, "max_header_bytes": 8192, "max_body_bytes": 4096, "max_frame_age_ms": 1000},
  "pilot": {"enabled": false, "base_url": "http://127.0.0.1:8765", "clock_domain": "monotonic_same_host", "connect_timeout_ms": 500, "request_timeout_ms": 1000, "max_response_bytes": 65536, "retry_initial_delay_ms": 100, "retry_max_delay_ms": 5000, "shutdown_timeout_ms": 1500},
  "recording": {"enabled": false, "root": "/tmp/nodus-vision-test-recordings", "queue_capacity_frames": 120, "max_duration_ms": 600000, "minimum_free_bytes": 1073741824, "finalize_timeout_ms": 10000, "bit_rate_bps": 8000000, "preset": "veryfast", "tune": "zerolatency"}
})json";

}  // namespace

TEST(VisionConfig, ParsesStrictFakeProviderConfig) {
    std::string input = VALID_CONFIG;
    input.replace(input.find("\"port\": 0"), std::string("\"port\": 0").size(), "\"port\": 8900");
    const VisionConfig config = parseVisionConfig(input);
    EXPECT_EQ(config.adapter, "fake");
    EXPECT_EQ(config.fake.width, 4);
    EXPECT_EQ(config.provider.port, 8900);
    EXPECT_EQ(config.calibration.mount_local_transform.z, 0.06);
    EXPECT_EQ(config.calibration.mount_local_transform.euler_type, "XYZ");
    EXPECT_FALSE(config.recording.enabled);
}

TEST(VisionConfig, RejectsUnknownAndWildcardAdvertiseFields) {
    std::string unknown = VALID_CONFIG;
    unknown.replace(unknown.find("\"schema_version\""), 0U, "\"unknown\": 1,");
    EXPECT_THROW(parseVisionConfig(unknown), std::invalid_argument);
    std::string wildcard = VALID_CONFIG;
    wildcard.replace(wildcard.find("http://127.0.0.1:8900"), 21U, "http://0.0.0.0:8900");
    EXPECT_THROW(parseVisionConfig(wildcard), std::invalid_argument);
}

TEST(VisionConfig, RejectsInactiveAdapterAndHeaderControlCharacters) {
    std::string inactive = VALID_CONFIG;
    const std::string fake_device_end = "\"pattern_seed\": 7}}";
    inactive.replace(inactive.find(fake_device_end), fake_device_end.size(),
                     "\"pattern_seed\": 7}, \"intel_d435\": {}}");
    EXPECT_THROW(parseVisionConfig(inactive), std::invalid_argument);

    std::string control_character = VALID_CONFIG;
    control_character.replace(control_character.find("camera.fake_top"),
                              std::string("camera.fake_top").size(), "camera.fake_top\\ninvalid");
    EXPECT_THROW(parseVisionConfig(control_character), std::invalid_argument);
}

TEST(VisionConfig, RejectsInvalidPilotConfiguration) {
    std::string invalid_url = VALID_CONFIG;
    invalid_url.replace(invalid_url.find("http://127.0.0.1:8765"), 21U, "https://127.0.0.1:8765");
    EXPECT_THROW(parseVisionConfig(invalid_url), std::invalid_argument);

    std::string invalid_retry = VALID_CONFIG;
    invalid_retry.replace(invalid_retry.find("\"retry_initial_delay_ms\": 100"), 29U,
                          "\"retry_initial_delay_ms\": 5001");
    EXPECT_THROW(parseVisionConfig(invalid_retry), std::invalid_argument);

    std::string invalid_provider_port = VALID_CONFIG;
    invalid_provider_port.replace(invalid_provider_port.find("\"port\": 0"), 9U, "\"port\": 8901");
    EXPECT_THROW(parseVisionConfig(invalid_provider_port), std::invalid_argument);

    std::string invalid_pilot_host = VALID_CONFIG;
    invalid_pilot_host.replace(invalid_pilot_host.find("http://127.0.0.1:8765"), 21U,
                               "http://:8765");
    EXPECT_THROW(parseVisionConfig(invalid_pilot_host), std::invalid_argument);
}

TEST(VisionConfig, ParsesEnabledPilotConfiguration) {
    std::string input = VALID_CONFIG;
    input.replace(input.find("\"port\": 0"), 9U, "\"port\": 8900");
    input.replace(input.find("\"enabled\": false"), 16U, "\"enabled\": true");
    const VisionConfig config = parseVisionConfig(input);
    EXPECT_TRUE(config.pilot.enabled);
    EXPECT_EQ(config.pilot.clock_domain, "monotonic_same_host");
    EXPECT_EQ(config.pilot.max_response_bytes, 65536);
}

TEST(VisionConfig, RequiresSafeExplicitRecordingConfiguration) {
    std::string enabled_odd_profile = VALID_CONFIG;
    enabled_odd_profile.replace(enabled_odd_profile.find("\"enabled\": false, \"root\""), 16U,
                                "\"enabled\": true, \"root\"");
    EXPECT_THROW(parseVisionConfig(enabled_odd_profile), std::invalid_argument);

    std::string relative_root = VALID_CONFIG;
    relative_root.replace(relative_root.find("/tmp/nodus-vision-test-recordings"), 33U,
                          "relative-recordings");
    EXPECT_THROW(parseVisionConfig(relative_root), std::invalid_argument);
}

TEST(VisionConfig, RejectsInvalidMountLocalTransform) {
    std::string unsupported_euler = VALID_CONFIG;
    unsupported_euler.replace(unsupported_euler.find("\"XYZ\""), 5U, "\"ABC\"");
    EXPECT_THROW(parseVisionConfig(unsupported_euler), std::invalid_argument);

    std::string excessive_translation = VALID_CONFIG;
    excessive_translation.replace(excessive_translation.find("\"z\": 0.06"), 9U, "\"z\": 10.1");
    EXPECT_THROW(parseVisionConfig(excessive_translation), std::invalid_argument);

    std::string legacy_link_id = VALID_CONFIG;
    legacy_link_id.replace(legacy_link_id.find("\"mount_local_transform\""), 23U,
                           "\"mount_link_id\": 4, \"mount_local_transform\"");
    EXPECT_THROW(parseVisionConfig(legacy_link_id), std::invalid_argument);
}

}  // namespace nodus_vision
