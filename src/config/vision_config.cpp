/**
 * @file vision_config.cpp
 * @brief strict Phase 2 Vision configuration parser를 구현한다.
 */

#include "vision_config.hpp"

#include <boost/json.hpp>
#include <cmath>
#include <stdexcept>
#include <string>

namespace nodus_vision {
namespace {

const boost::json::value& requireField(const boost::json::object& object, const char* key,
                                       const char* path) {
    const boost::json::value* value = object.if_contains(key);
    if (value == nullptr) {
        throw std::invalid_argument(std::string(path) + ": required field is missing.");
    }
    return *value;
}

const boost::json::object& requireObject(const boost::json::object& object, const char* key,
                                         const char* path) {
    const boost::json::value& value = requireField(object, key, path);
    if (!value.is_object()) {
        throw std::invalid_argument(std::string(path) + ": must be an object.");
    }
    return value.as_object();
}

std::string requireString(const boost::json::object& object, const char* key, const char* path) {
    const boost::json::value& value = requireField(object, key, path);
    if (!value.is_string() || value.as_string().empty() || value.as_string().size() > 128U) {
        throw std::invalid_argument(std::string(path) + ": must be a non-empty bounded string.");
    }
    const std::string parsed(value.as_string());
    for (const unsigned char character : parsed) {
        if (character < 0x20U || character == 0x7FU) {
            throw std::invalid_argument(std::string(path) +
                                        ": must not contain control characters.");
        }
    }
    return parsed;
}

int requireBoundedInt(const boost::json::object& object, const char* key, const char* path,
                      int minimum, int maximum) {
    const boost::json::value& value = requireField(object, key, path);
    if (!value.is_int64() && !value.is_uint64()) {
        throw std::invalid_argument(std::string(path) + ": must be an integer.");
    }
    const std::int64_t parsed =
        value.is_int64() ? value.as_int64() : static_cast<std::int64_t>(value.as_uint64());
    if (parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(std::string(path) + ": is outside the allowed range.");
    }
    return static_cast<int>(parsed);
}

void rejectUnknownFields(const boost::json::object& object,
                         std::initializer_list<const char*> allowed, const char* path) {
    for (const boost::json::key_value_pair& item : object) {
        bool known = false;
        for (const char* key : allowed) {
            if (item.key() == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw std::invalid_argument(std::string(path) + "/" + std::string(item.key()) +
                                        ": unknown field.");
        }
    }
}

bool isAdvertisedUrlValid(const std::string& url) {
    return url.rfind("http://", 0U) == 0U && url.find("0.0.0.0") == std::string::npos &&
           url.find("*") == std::string::npos;
}

}  // namespace

VisionConfig parseVisionConfig(const std::string& json_text) {
    boost::json::error_code error;
    const boost::json::value value = boost::json::parse(json_text, error);
    if (error || !value.is_object()) {
        throw std::invalid_argument("/: must be a JSON object.");
    }
    const boost::json::object& root = value.as_object();
    rejectUnknownFields(
        root, {"schema_version", "device_id", "component_id", "device", "calibration", "provider"},
        "/");
    VisionConfig config;
    config.schema_version = requireBoundedInt(root, "schema_version", "/schema_version", 1, 1);
    config.device_id = requireString(root, "device_id", "/device_id");
    config.component_id = requireString(root, "component_id", "/component_id");
    const boost::json::object& device = requireObject(root, "device", "/device");
    rejectUnknownFields(device, {"adapter", "fake", "intel_d435"}, "/device");
    config.adapter = requireString(device, "adapter", "/device/adapter");
    if (config.adapter == "fake") {
        if (device.if_contains("intel_d435") != nullptr) {
            throw std::invalid_argument(
                "/device/intel_d435: inactive adapter configuration is not allowed.");
        }
        const boost::json::object& fake = requireObject(device, "fake", "/device/fake");
        rejectUnknownFields(fake, {"width", "height", "fps", "start_frame_number", "pattern_seed"},
                            "/device/fake");
        config.fake.width = requireBoundedInt(fake, "width", "/device/fake/width", 1, 4096);
        config.fake.height = requireBoundedInt(fake, "height", "/device/fake/height", 1, 4096);
        config.fake.fps = requireBoundedInt(fake, "fps", "/device/fake/fps", 1, 240);
        config.fake.start_frame_number = static_cast<std::uint64_t>(requireBoundedInt(
            fake, "start_frame_number", "/device/fake/start_frame_number", 1, 1000000000));
        config.fake.pattern_seed = static_cast<std::uint32_t>(
            requireBoundedInt(fake, "pattern_seed", "/device/fake/pattern_seed", 0, 255));
        config.fake.device_id = config.device_id;
    } else if (config.adapter == "intel_d435") {
        if (device.if_contains("fake") != nullptr) {
            throw std::invalid_argument(
                "/device/fake: inactive adapter configuration is not allowed.");
        }
        const boost::json::object& d435 = requireObject(device, "intel_d435", "/device/intel_d435");
        rejectUnknownFields(
            d435,
            {"serial_number", "depth_width", "depth_height", "depth_fps", "color_width",
             "color_height", "color_fps", "enable_color", "depth_min_m", "depth_max_m"},
            "/device/intel_d435");
        config.intel_d435.serial_number =
            requireString(d435, "serial_number", "/device/intel_d435/serial_number");
        config.intel_d435.depth_width =
            requireBoundedInt(d435, "depth_width", "/device/intel_d435/depth_width", 1, 4096);
        config.intel_d435.depth_height =
            requireBoundedInt(d435, "depth_height", "/device/intel_d435/depth_height", 1, 4096);
        config.intel_d435.depth_fps =
            requireBoundedInt(d435, "depth_fps", "/device/intel_d435/depth_fps", 1, 240);
        config.intel_d435.color_width =
            requireBoundedInt(d435, "color_width", "/device/intel_d435/color_width", 1, 4096);
        config.intel_d435.color_height =
            requireBoundedInt(d435, "color_height", "/device/intel_d435/color_height", 1, 4096);
        config.intel_d435.color_fps =
            requireBoundedInt(d435, "color_fps", "/device/intel_d435/color_fps", 1, 240);
        config.intel_d435.enable_color =
            requireField(d435, "enable_color", "/device/intel_d435/enable_color").as_bool();
        config.intel_d435.depth_min_m = static_cast<float>(
            requireField(d435, "depth_min_m", "/device/intel_d435/depth_min_m").as_double());
        config.intel_d435.depth_max_m = static_cast<float>(
            requireField(d435, "depth_max_m", "/device/intel_d435/depth_max_m").as_double());
        config.intel_d435.device_id = config.device_id;
        validateIntelD435AdapterConfig(config.intel_d435);
    } else {
        throw std::invalid_argument("/device/adapter: unsupported adapter.");
    }
    const boost::json::object& calibration = requireObject(root, "calibration", "/calibration");
    rejectUnknownFields(
        calibration, {"calibration_id", "sensor_frame", "mount_frame", "camera_to_mount_matrix4x4"},
        "/calibration");
    config.calibration.calibration_id =
        requireString(calibration, "calibration_id", "/calibration/calibration_id");
    config.calibration.sensor_frame =
        requireString(calibration, "sensor_frame", "/calibration/sensor_frame");
    config.calibration.mount_frame =
        requireString(calibration, "mount_frame", "/calibration/mount_frame");
    const boost::json::array& matrix = requireField(calibration, "camera_to_mount_matrix4x4",
                                                    "/calibration/camera_to_mount_matrix4x4")
                                           .as_array();
    if (matrix.size() != config.calibration.camera_to_mount_matrix4x4.size()) {
        throw std::invalid_argument(
            "/calibration/camera_to_mount_matrix4x4: must contain 16 finite values.");
    }
    for (std::size_t index = 0; index < matrix.size(); ++index) {
        const double element = matrix.at(index).to_number<double>();
        if (!std::isfinite(element)) {
            throw std::invalid_argument(
                "/calibration/camera_to_mount_matrix4x4: values must be finite.");
        }
        config.calibration.camera_to_mount_matrix4x4.at(index) = element;
    }
    const boost::json::object& provider = requireObject(root, "provider", "/provider");
    rejectUnknownFields(
        provider,
        {"bind_host", "port", "advertised_base_url", "max_connections", "max_stream_clients",
         "request_timeout_ms", "max_header_bytes", "max_body_bytes", "max_frame_age_ms"},
        "/provider");
    config.provider.bind_host = requireString(provider, "bind_host", "/provider/bind_host");
    config.provider.port = requireBoundedInt(provider, "port", "/provider/port", 1, 65535);
    config.provider.advertised_base_url =
        requireString(provider, "advertised_base_url", "/provider/advertised_base_url");
    if (!isAdvertisedUrlValid(config.provider.advertised_base_url)) {
        throw std::invalid_argument(
            "/provider/advertised_base_url: must be an absolute non-wildcard HTTP URL.");
    }
    config.provider.max_connections =
        requireBoundedInt(provider, "max_connections", "/provider/max_connections", 1, 1024);
    config.provider.max_stream_clients =
        requireBoundedInt(provider, "max_stream_clients", "/provider/max_stream_clients", 1,
                          config.provider.max_connections);
    config.provider.request_timeout_ms =
        requireBoundedInt(provider, "request_timeout_ms", "/provider/request_timeout_ms", 1, 60000);
    config.provider.max_header_bytes =
        requireBoundedInt(provider, "max_header_bytes", "/provider/max_header_bytes", 1, 1048576);
    config.provider.max_body_bytes =
        requireBoundedInt(provider, "max_body_bytes", "/provider/max_body_bytes", 1, 10485760);
    config.provider.max_frame_age_ms =
        requireBoundedInt(provider, "max_frame_age_ms", "/provider/max_frame_age_ms", 1, 60000);
    return config;
}

std::string getVisionConfigSchemaPath() { return "schemas/vision/v1/config.schema.json"; }

}  // namespace nodus_vision
