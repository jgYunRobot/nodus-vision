/**
 * @file test_vision_endpoint_catalog.cpp
 * @brief Vision direct endpoint catalog 생성을 검증한다.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <boost/json.hpp>

#include "vision_endpoint_catalog.hpp"

namespace nodus_vision {
namespace {

VisionConfig makeConfig() {
    VisionConfig config;
    config.component_id = "camera.fake";
    config.device_id = "fake";
    config.adapter = "fake";
    config.provider.advertised_base_url = "http://127.0.0.1:8900";
    config.calibration.calibration_id = "fake-v1";
    config.calibration.sensor_frame = "fake_optical";
    return config;
}

}  // namespace

TEST(VisionEndpointCatalog, BuildsStableDirectColorCatalog) {
    const VisionEndpointCatalog catalog = VisionEndpointCatalogBuilder().buildCatalog(makeConfig());
    ASSERT_EQ(catalog.descriptors.size(), 9U);
    EXPECT_TRUE(std::is_sorted(catalog.capabilities.begin(), catalog.capabilities.end()));
    EXPECT_EQ(catalog.descriptors.at(0).descriptor_id, "color-preview");
    EXPECT_EQ(catalog.descriptors.at(0).endpoint, "http://127.0.0.1:8900/stream/color.mjpg");
    const std::string publication = serializeCatalogPublicationRequest(
        "catalog-vision-00112233445566778899aabbccddeeff-1", 0U, catalog.descriptors);
    const boost::json::object object = boost::json::parse(publication).as_object();
    EXPECT_EQ(object.at("descriptors").as_array().size(), 9U);
    EXPECT_EQ(object.at("descriptors").as_array().at(0).as_object().at("service"), nullptr);
    EXPECT_EQ(object.at("descriptors")
                  .as_array()
                  .at(0)
                  .as_object()
                  .at("metadata")
                  .as_object()
                  .at("api_version"),
              "1.1.0");
}

TEST(VisionEndpointCatalog, OmitsOnlyColorDescriptorsWhenColorIsDisabled) {
    VisionConfig config = makeConfig();
    config.adapter = "intel_d435";
    config.intel_d435.enable_color = false;
    const VisionEndpointCatalog catalog = VisionEndpointCatalogBuilder().buildCatalog(config);
    ASSERT_EQ(catalog.descriptors.size(), 7U);
    for (const PilotEndpointDescriptor& descriptor : catalog.descriptors) {
        EXPECT_NE(descriptor.descriptor_id, "color-preview");
        EXPECT_NE(descriptor.descriptor_id, "color-snapshot");
    }
}

TEST(VisionEndpointCatalog, AddsOnlyDirectRecordingServicesWhenEnabled) {
    VisionConfig config = makeConfig();
    config.recording.enabled = true;
    const VisionEndpointCatalog catalog = VisionEndpointCatalogBuilder().buildCatalog(config);
    ASSERT_EQ(catalog.descriptors.size(), 12U);
    EXPECT_EQ(catalog.descriptors.at(8).descriptor_id, "recording-current");
    EXPECT_EQ(catalog.descriptors.at(9).descriptor_id, "recording-start");
    EXPECT_EQ(catalog.descriptors.at(10).descriptor_id, "recording-stop");
    EXPECT_EQ(catalog.descriptors.at(8).endpoint, "http://127.0.0.1:8900/recordings/current");
}

}  // namespace nodus_vision
