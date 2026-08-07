/**
 * @file vision_config.hpp
 * @brief strict Phase 2 Vision provider JSON configuration을 제공한다.
 */

#ifndef NODUS_VISION_CONFIG_VISION_CONFIG_HPP_
#define NODUS_VISION_CONFIG_VISION_CONFIG_HPP_

#include <array>
#include <cstdint>
#include <string>

#include "fake_camera_adapter.hpp"
#include "intel_d435_adapter.hpp"

namespace nodus_vision {

/** @brief strict provider runtime bounds다. */
struct ProviderConfig {
    std::string bind_host;
    int port{0};
    std::string advertised_base_url;
    int max_connections{0};
    int max_stream_clients{0};
    int request_timeout_ms{0};
    int max_header_bytes{0};
    int max_body_bytes{0};
    int max_frame_age_ms{0};
};

/** @brief Pilot public lifecycle client의 strict runtime bounds다. */
struct PilotConfig {
    bool enabled{false};
    std::string base_url;
    std::string clock_domain;
    int connect_timeout_ms{0};
    int request_timeout_ms{0};
    int max_response_bytes{0};
    int retry_initial_delay_ms{0};
    int retry_max_delay_ms{0};
    int shutdown_timeout_ms{0};
};

/** @brief Vision-owned RGB recording의 strict runtime bounds다. */
struct RecordingConfig {
    bool enabled{false};
    std::string root;
    int queue_capacity_frames{0};
    int max_duration_ms{0};
    std::uint64_t minimum_free_bytes{0U};
    int finalize_timeout_ms{0};
    int bit_rate_bps{0};
    std::string preset;
    std::string tune;
};

/** @brief immutable calibration configuration이다. */
struct CalibrationConfig {
    std::string calibration_id;
    std::string sensor_frame;
    std::string mount_frame;
    std::array<double, 16> camera_to_mount_matrix4x4{};
};

/** @brief strict parsed Vision configuration이다. */
struct VisionConfig {
    int schema_version{0};
    std::string device_id;
    std::string component_id;
    std::string adapter;
    FakeCameraAdapterConfig fake;
    IntelD435AdapterConfig intel_d435;
    CalibrationConfig calibration;
    ProviderConfig provider;
    PilotConfig pilot;
    RecordingConfig recording;
};

/** @brief JSON document를 strict typed VisionConfig로 parse한다. */
VisionConfig parseVisionConfig(const std::string& json_text);
/** @brief schema file location을 반환한다. */
std::string getVisionConfigSchemaPath();

}  // namespace nodus_vision

#endif  // NODUS_VISION_CONFIG_VISION_CONFIG_HPP_
