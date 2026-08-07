/**
 * @file vision_endpoint_catalog.cpp
 * @brief Vision direct data-plane descriptor 생성을 구현한다.
 */

#include "vision_endpoint_catalog.hpp"

#include <algorithm>
#include <utility>

namespace nodus_vision {
namespace {

std::string makeEndpoint(const std::string& base_url, const char* path) {
    if (base_url.back() == '/') {
        return base_url.substr(0U, base_url.size() - 1U) + path;
    }
    return base_url + path;
}

boost::json::object makeMetadata(const VisionConfig& config) {
    boost::json::object metadata;
    metadata["device_id"] = config.device_id;
    metadata["sensor_frame"] = config.calibration.sensor_frame;
    metadata["calibration_id"] = config.calibration.calibration_id;
    metadata["api_version"] = "1.1.0";
    return metadata;
}

PilotEndpointDescriptor makeService(const VisionConfig& config, const char* descriptor_id,
                                    const char* capability, const char* path,
                                    const char* media_type, const char* schema_id,
                                    const char* method, const char* request_schema_id) {
    PilotEndpointDescriptor descriptor;
    descriptor.descriptor_id = descriptor_id;
    descriptor.kind = "service";
    descriptor.capability = capability;
    descriptor.endpoint = makeEndpoint(config.provider.advertised_base_url, path);
    descriptor.media_type = media_type;
    descriptor.schema_id = schema_id;
    descriptor.metadata = makeMetadata(config);
    descriptor.service = {method, request_schema_id, schema_id};
    return descriptor;
}

PilotEndpointDescriptor makeStream(const VisionConfig& config, const char* descriptor_id,
                                   const char* capability, const char* path,
                                   const char* schema_id) {
    PilotEndpointDescriptor descriptor;
    descriptor.descriptor_id = descriptor_id;
    descriptor.kind = "stream";
    descriptor.capability = capability;
    descriptor.endpoint = makeEndpoint(config.provider.advertised_base_url, path);
    descriptor.media_type = "multipart/x-mixed-replace";
    descriptor.schema_id = schema_id;
    descriptor.metadata = makeMetadata(config);
    descriptor.is_stream = true;
    descriptor.stream = {"monotonic_same_host", config.component_id + ".capture"};
    return descriptor;
}

bool isColorEnabled(const VisionConfig& config) {
    return config.adapter == "fake" || config.intel_d435.enable_color;
}

}  // namespace

VisionEndpointCatalog VisionEndpointCatalogBuilder::buildCatalog(const VisionConfig& config) const {
    VisionEndpointCatalog result;
    result.descriptors.push_back(makeService(config, "health", "camera.health.get", "/health",
                                             "application/json", "nodus.vision.health.response.v1",
                                             "GET", ""));
    result.descriptors.push_back(makeService(config, "metadata", "camera.metadata.get", "/metadata",
                                             "application/json",
                                             "nodus.vision.metadata.response.v1", "GET", ""));
    result.descriptors.push_back(makeStream(config, "depth-preview", "camera.stream.depth.preview",
                                            "/stream/depth.mjpg",
                                            "nodus.vision.mjpeg.depth_part.v1"));
    result.descriptors.push_back(
        makeService(config, "depth-snapshot", "camera.snapshot.depth.preview", "/snapshot/depth",
                    "image/jpeg", "nodus.vision.jpeg.depth_preview.v1", "GET", ""));
    result.descriptors.push_back(makeService(
        config, "roi-depth", "camera.query.roi_depth", "/query/roi_depth", "application/json",
        "nodus.vision.roi_depth.response.v1", "POST", "nodus.vision.roi_depth.request.v1"));
    result.descriptors.push_back(makeService(
        config, "pixel-to-point", "camera.query.pixel_to_point", "/query/pixel_to_point",
        "application/json", "nodus.vision.pixel_point.response.v1", "POST",
        "nodus.vision.pixel_point.request.v1"));
    result.descriptors.push_back(makeService(
        config, "pointcloud-binary", "camera.snapshot.pointcloud", "/snapshot/pointcloud.bin",
        "application/octet-stream", "nodus.vision.pointcloud.pcd1.v2", "GET", ""));
    if (isColorEnabled(config)) {
        result.descriptors.push_back(makeStream(config, "color-preview",
                                                "camera.stream.color.preview", "/stream/color.mjpg",
                                                "nodus.vision.mjpeg.color_part.v1"));
        result.descriptors.push_back(makeService(config, "color-snapshot", "camera.snapshot.color",
                                                 "/snapshot/color", "image/jpeg",
                                                 "nodus.vision.jpeg.color.v1", "GET", ""));
    }
    if (config.recording.enabled) {
        result.descriptors.push_back(
            makeService(config, "recording-start", "camera.recording.start", "/recordings/start",
                        "application/json", "nodus.vision.recording.start.response.v1", "POST",
                        "nodus.vision.recording.start.request.v1"));
        result.descriptors.push_back(makeService(config, "recording-stop", "camera.recording.stop",
                                                 "/recordings/stop", "application/json",
                                                 "nodus.vision.recording.stop.response.v1", "POST",
                                                 "nodus.vision.recording.stop.request.v1"));
        result.descriptors.push_back(makeService(
            config, "recording-current", "camera.recording.current", "/recordings/current",
            "application/json", "nodus.vision.recording.current.response.v1", "GET", ""));
    }
    std::sort(result.descriptors.begin(), result.descriptors.end(),
              [](const PilotEndpointDescriptor& lhs, const PilotEndpointDescriptor& rhs) {
                  return lhs.descriptor_id < rhs.descriptor_id;
              });
    for (const PilotEndpointDescriptor& descriptor : result.descriptors) {
        result.capabilities.push_back(descriptor.capability);
    }
    std::sort(result.capabilities.begin(), result.capabilities.end());
    return result;
}

}  // namespace nodus_vision
