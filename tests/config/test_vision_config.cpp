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
  "calibration": {"calibration_id": "fake_v1", "sensor_frame": "fake_optical", "mount_frame": "fake_mount", "camera_to_mount_matrix4x4": [1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0]},
  "provider": {"bind_host": "127.0.0.1", "port": 0, "advertised_base_url": "http://127.0.0.1:8900", "max_connections": 4, "max_stream_clients": 2, "request_timeout_ms": 1000, "max_header_bytes": 8192, "max_body_bytes": 4096, "max_frame_age_ms": 1000}
})json";

} // namespace

TEST(VisionConfig, ParsesStrictFakeProviderConfig)
{
    std::string input = VALID_CONFIG;
    input.replace(input.find("\"port\": 0"), std::string("\"port\": 0").size(), "\"port\": 8900");
    const VisionConfig config = parseVisionConfig(input);
    EXPECT_EQ(config.adapter, "fake");
    EXPECT_EQ(config.fake.width, 4);
    EXPECT_EQ(config.provider.port, 8900);
    EXPECT_EQ(config.calibration.camera_to_mount_matrix4x4.at(0), 1.0);
}

TEST(VisionConfig, RejectsUnknownAndWildcardAdvertiseFields)
{
    std::string unknown = VALID_CONFIG;
    unknown.replace(unknown.find("\"schema_version\""), 0U, "\"unknown\": 1,");
    EXPECT_THROW(parseVisionConfig(unknown), std::invalid_argument);
    std::string wildcard = VALID_CONFIG;
    wildcard.replace(wildcard.find("http://127.0.0.1:8900"), 21U, "http://0.0.0.0:8900");
    EXPECT_THROW(parseVisionConfig(wildcard), std::invalid_argument);
}

} // namespace nodus_vision
