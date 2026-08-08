/**
 * @file vision_config.cpp
 * @brief strict Phase 2 Vision configuration parser를 구현한다.
 */

#include "vision_config.hpp"

#include <array>
#include <boost/json.hpp>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace nodus_vision {
namespace {

constexpr double MAX_MOUNT_TRANSLATION_M = 10.0;
constexpr std::array<const char*, 8> SUPPORTED_EULER_TYPES = {
    "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX", "ZXZ", "ZYZ",
};

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

std::uint64_t requireBoundedUint64(const boost::json::object& object, const char* key,
                                   const char* path, std::uint64_t minimum, std::uint64_t maximum) {
    const boost::json::value& value = requireField(object, key, path);
    if (!value.is_int64() && !value.is_uint64()) {
        throw std::invalid_argument(std::string(path) + ": must be an integer.");
    }
    const std::uint64_t parsed =
        value.is_int64()
            ? (value.as_int64() < 0
                   ? throw std::invalid_argument(std::string(path) + ": must be non-negative.")
                   : static_cast<std::uint64_t>(value.as_int64()))
            : value.as_uint64();
    if (parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(std::string(path) + ": is outside the allowed range.");
    }
    return parsed;
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
    if (url.rfind("http://", 0U) != 0U || url.find("0.0.0.0") != std::string::npos ||
        url.find('*') != std::string::npos || url.find('@') != std::string::npos ||
        url.find('?') != std::string::npos || url.find('#') != std::string::npos) {
        return false;
    }
    const std::size_t authority_start = std::string("http://").size();
    const std::size_t path_start = url.find('/', authority_start);
    const std::string authority = url.substr(authority_start, path_start - authority_start);
    const std::size_t port_separator = authority.rfind(':');
    if (authority.empty() || port_separator == std::string::npos || port_separator == 0U ||
        port_separator == authority.size() - 1U) {
        return false;
    }
    for (std::size_t index = port_separator + 1U; index < authority.size(); ++index) {
        if (authority.at(index) < '0' || authority.at(index) > '9') {
            return false;
        }
    }
    const int port = std::stoi(authority.substr(port_separator + 1U));
    return port >= 1 && port <= 65535;
}

int getUrlPort(const std::string& url) {
    const std::size_t authority_start = std::string("http://").size();
    const std::size_t path_start = url.find('/', authority_start);
    const std::string authority = url.substr(authority_start, path_start - authority_start);
    return std::stoi(authority.substr(authority.rfind(':') + 1U));
}

bool isEulerTypeSupported(const std::string& euler_type) {
    for (const char* supported_type : SUPPORTED_EULER_TYPES) {
        if (euler_type == supported_type) {
            return true;
        }
    }
    return false;
}

double requireFiniteNumber(const boost::json::object& object, const char* key, const char* path) {
    const boost::json::value& value = requireField(object, key, path);
    if (!value.is_double() && !value.is_int64() && !value.is_uint64()) {
        throw std::invalid_argument(std::string(path) + ": must be a finite number.");
    }
    const double parsed = value.to_number<double>();
    if (!std::isfinite(parsed)) {
        throw std::invalid_argument(std::string(path) + ": must be a finite number.");
    }
    return parsed;
}

}  // namespace

VisionConfig parseVisionConfig(const std::string& json_text) {
    boost::json::error_code error;
    const boost::json::value value = boost::json::parse(json_text, error);
    if (error || !value.is_object()) {
        throw std::invalid_argument("/: must be a JSON object.");
    }
    const boost::json::object& root = value.as_object();
    rejectUnknownFields(root,
                        {"schema_version", "device_id", "component_id", "device", "calibration",
                         "provider", "pilot", "recording"},
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
    rejectUnknownFields(calibration,
                        {"calibration_id", "sensor_frame", "mount_frame", "mount_local_transform"},
                        "/calibration");
    config.calibration.calibration_id =
        requireString(calibration, "calibration_id", "/calibration/calibration_id");
    config.calibration.sensor_frame =
        requireString(calibration, "sensor_frame", "/calibration/sensor_frame");
    config.calibration.mount_frame =
        requireString(calibration, "mount_frame", "/calibration/mount_frame");
    const boost::json::object& mount_local_transform =
        requireObject(calibration, "mount_local_transform", "/calibration/mount_local_transform");
    rejectUnknownFields(mount_local_transform, {"x", "y", "z", "r1", "r2", "r3", "euler_type"},
                        "/calibration/mount_local_transform");
    config.calibration.mount_local_transform.x =
        requireFiniteNumber(mount_local_transform, "x", "/calibration/mount_local_transform/x");
    config.calibration.mount_local_transform.y =
        requireFiniteNumber(mount_local_transform, "y", "/calibration/mount_local_transform/y");
    config.calibration.mount_local_transform.z =
        requireFiniteNumber(mount_local_transform, "z", "/calibration/mount_local_transform/z");
    config.calibration.mount_local_transform.r1 =
        requireFiniteNumber(mount_local_transform, "r1", "/calibration/mount_local_transform/r1");
    config.calibration.mount_local_transform.r2 =
        requireFiniteNumber(mount_local_transform, "r2", "/calibration/mount_local_transform/r2");
    config.calibration.mount_local_transform.r3 =
        requireFiniteNumber(mount_local_transform, "r3", "/calibration/mount_local_transform/r3");
    config.calibration.mount_local_transform.euler_type = requireString(
        mount_local_transform, "euler_type", "/calibration/mount_local_transform/euler_type");
    if (std::abs(config.calibration.mount_local_transform.x) > MAX_MOUNT_TRANSLATION_M ||
        std::abs(config.calibration.mount_local_transform.y) > MAX_MOUNT_TRANSLATION_M ||
        std::abs(config.calibration.mount_local_transform.z) > MAX_MOUNT_TRANSLATION_M) {
        throw std::invalid_argument(
            "/calibration/mount_local_transform: translation is outside the allowed range.");
    }
    if (!isEulerTypeSupported(config.calibration.mount_local_transform.euler_type)) {
        throw std::invalid_argument(
            "/calibration/mount_local_transform/euler_type: unsupported Euler convention.");
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
    if (getUrlPort(config.provider.advertised_base_url) != config.provider.port) {
        throw std::invalid_argument(
            "/provider/advertised_base_url: port must match /provider/port.");
    }
    const boost::json::object& pilot = requireObject(root, "pilot", "/pilot");
    rejectUnknownFields(pilot,
                        {"enabled", "base_url", "clock_domain", "connect_timeout_ms",
                         "request_timeout_ms", "max_response_bytes", "retry_initial_delay_ms",
                         "retry_max_delay_ms", "shutdown_timeout_ms"},
                        "/pilot");
    const boost::json::value& enabled = requireField(pilot, "enabled", "/pilot/enabled");
    if (!enabled.is_bool()) {
        throw std::invalid_argument("/pilot/enabled: must be a boolean.");
    }
    config.pilot.enabled = enabled.as_bool();
    config.pilot.base_url = requireString(pilot, "base_url", "/pilot/base_url");
    if (!isAdvertisedUrlValid(config.pilot.base_url)) {
        throw std::invalid_argument(
            "/pilot/base_url: must be an absolute HTTP URL without userinfo, query, or fragment.");
    }
    config.pilot.clock_domain = requireString(pilot, "clock_domain", "/pilot/clock_domain");
    if (config.pilot.clock_domain != "monotonic_same_host") {
        throw std::invalid_argument("/pilot/clock_domain: only monotonic_same_host is supported.");
    }
    config.pilot.connect_timeout_ms =
        requireBoundedInt(pilot, "connect_timeout_ms", "/pilot/connect_timeout_ms", 1, 60000);
    config.pilot.request_timeout_ms =
        requireBoundedInt(pilot, "request_timeout_ms", "/pilot/request_timeout_ms", 1, 60000);
    config.pilot.max_response_bytes =
        requireBoundedInt(pilot, "max_response_bytes", "/pilot/max_response_bytes", 1, 10485760);
    config.pilot.retry_initial_delay_ms = requireBoundedInt(
        pilot, "retry_initial_delay_ms", "/pilot/retry_initial_delay_ms", 1, 60000);
    config.pilot.retry_max_delay_ms =
        requireBoundedInt(pilot, "retry_max_delay_ms", "/pilot/retry_max_delay_ms", 1, 60000);
    config.pilot.shutdown_timeout_ms =
        requireBoundedInt(pilot, "shutdown_timeout_ms", "/pilot/shutdown_timeout_ms", 1, 60000);
    if (config.pilot.retry_initial_delay_ms > config.pilot.retry_max_delay_ms) {
        throw std::invalid_argument(
            "/pilot/retry_initial_delay_ms: must not exceed /pilot/retry_max_delay_ms.");
    }
    const boost::json::object& recording = requireObject(root, "recording", "/recording");
    rejectUnknownFields(
        recording,
        {"enabled", "root", "queue_capacity_frames", "max_duration_ms", "minimum_free_bytes",
         "finalize_timeout_ms", "bit_rate_bps", "preset", "tune"},
        "/recording");
    const boost::json::value& recording_enabled =
        requireField(recording, "enabled", "/recording/enabled");
    if (!recording_enabled.is_bool()) {
        throw std::invalid_argument("/recording/enabled: must be a boolean.");
    }
    config.recording.enabled = recording_enabled.as_bool();
    config.recording.root = requireString(recording, "root", "/recording/root");
    if (!std::filesystem::path(config.recording.root).is_absolute()) {
        throw std::invalid_argument("/recording/root: must be an absolute config-owned path.");
    }
    config.recording.queue_capacity_frames = requireBoundedInt(
        recording, "queue_capacity_frames", "/recording/queue_capacity_frames", 1, 4096);
    config.recording.max_duration_ms =
        requireBoundedInt(recording, "max_duration_ms", "/recording/max_duration_ms", 1, 3600000);
    config.recording.minimum_free_bytes = requireBoundedUint64(
        recording, "minimum_free_bytes", "/recording/minimum_free_bytes", 1U, 1099511627776ULL);
    config.recording.finalize_timeout_ms = requireBoundedInt(
        recording, "finalize_timeout_ms", "/recording/finalize_timeout_ms", 1, 600000);
    config.recording.bit_rate_bps =
        requireBoundedInt(recording, "bit_rate_bps", "/recording/bit_rate_bps", 1, 100000000);
    config.recording.preset = requireString(recording, "preset", "/recording/preset");
    config.recording.tune = requireString(recording, "tune", "/recording/tune");
    if (config.recording.preset != "veryfast" || config.recording.tune != "zerolatency") {
        throw std::invalid_argument(
            "/recording: only the Phase 5 veryfast/zerolatency codec profile is supported.");
    }
    const int color_width =
        config.adapter == "fake" ? config.fake.width : config.intel_d435.color_width;
    const int color_height =
        config.adapter == "fake" ? config.fake.height : config.intel_d435.color_height;
    const bool color_enabled = config.adapter == "fake" || config.intel_d435.enable_color;
    if (config.recording.enabled &&
        (!color_enabled || color_width % 2 != 0 || color_height % 2 != 0)) {
        throw std::invalid_argument(
            "/recording/enabled: requires an enabled even-dimension RGB color profile.");
    }
    return config;
}

std::string getVisionConfigSchemaPath() { return "schemas/vision/v1/config.schema.json"; }

}  // namespace nodus_vision
