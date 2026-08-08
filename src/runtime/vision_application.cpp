/**
 * @file vision_application.cpp
 * @brief Vision provider application lifecycle을 구현한다.
 */

#include "vision_application.hpp"

#include <unistd.h>

#include <atomic>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nodus_vision/camera_adapter.hpp>
#include <nodus_vision/frame_store.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "camera_mount_transform.hpp"
#include "encoded_preview_cache.hpp"
#include "fake_camera_adapter.hpp"
#include "intel_d435_adapter.hpp"
#include "jpeg_encoder.hpp"
#include "pcd1.hpp"
#include "pilot_integration_client.hpp"
#include "provider_http_server.hpp"
#include "query_serializer.hpp"
#include "recording_manager.hpp"

namespace nodus_vision {
namespace {

constexpr std::size_t POINTCLOUD_MAX_POINTS = 100000U;
constexpr int POINTCLOUD_STRIDE_PIXELS = 1;
constexpr int JPEG_QUALITY = 90;
constexpr int CAPTURE_TIMEOUT_MS = 100;
constexpr int ENCODER_WAIT_MS = 100;
constexpr int CAPTURE_ERROR_RETRY_MS = 10;

struct RoiRequest {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct PixelRequest {
    int x{0};
    int y{0};
};

RecordingStartRequest parseRecordingStartRequest(const std::string& body,
                                                 const VisionConfig& config) {
    const boost::json::value parsed = boost::json::parse(body);
    if (!parsed.is_object()) {
        throw std::invalid_argument("Recording start request must be an object.");
    }
    const boost::json::object& request = parsed.as_object();
    if (request.size() != 6U || request.at("schema_version").to_number<int>() != 1 ||
        request.at("expected_device_id").as_string() != config.device_id ||
        request.at("expected_calibration_id").as_string() != config.calibration.calibration_id ||
        !request.at("expected_profile").is_object()) {
        throw std::invalid_argument("Recording start request shape is invalid.");
    }
    const boost::json::object& profile = request.at("expected_profile").as_object();
    const int width = config.adapter == "fake" ? config.fake.width : config.intel_d435.color_width;
    const int height =
        config.adapter == "fake" ? config.fake.height : config.intel_d435.color_height;
    const int fps = config.adapter == "fake" ? config.fake.fps : config.intel_d435.color_fps;
    if (profile.size() != 4U || profile.at("width").to_number<int>() != width ||
        profile.at("height").to_number<int>() != height ||
        profile.at("fps").to_number<int>() != fps ||
        profile.at("pixel_format").as_string() != "rgb24") {
        throw std::invalid_argument("Recording expected profile does not match Vision.");
    }
    return {std::string(request.at("request_id").as_string()),
            std::string(request.at("recording_id").as_string()), body};
}

RecordingStartRequest parseRecordingStopRequest(const std::string& body) {
    const boost::json::value parsed = boost::json::parse(body);
    if (!parsed.is_object()) {
        throw std::invalid_argument("Recording stop request must be an object.");
    }
    const boost::json::object& request = parsed.as_object();
    if (request.size() != 3U || request.at("schema_version").to_number<int>() != 1) {
        throw std::invalid_argument("Recording stop request shape is invalid.");
    }
    return {std::string(request.at("request_id").as_string()),
            std::string(request.at("recording_id").as_string()), body};
}

const char* toString(RecordingState state) noexcept {
    switch (state) {
        case RecordingState::e_DISABLED:
            return "disabled";
        case RecordingState::e_IDLE:
            return "idle";
        case RecordingState::e_PREPARING:
            return "preparing";
        case RecordingState::e_RECORDING:
            return "recording";
        case RecordingState::e_FINALIZING:
            return "finalizing";
        case RecordingState::e_FINALIZED:
            return "finalized";
        case RecordingState::e_FAULTED:
            return "faulted";
    }
    return "faulted";
}

std::string makeRecordingCurrentJson(const RecordingStatus& status) {
    boost::json::object root;
    root["schema_version"] = 1;
    root["state"] = toString(status.state);
    root["recording_id"] = status.recording_id.empty() ? boost::json::value(nullptr)
                                                       : boost::json::value(status.recording_id);
    root["admitted_frame_count"] = status.admitted_frame_count;
    root["submitted_frame_count"] = status.submitted_frame_count;
    root["recording_drop_count"] = status.recording_drop_count;
    root["started_monotonic_ns"] = status.started_monotonic_ns == 0
                                       ? boost::json::value(nullptr)
                                       : boost::json::value(status.started_monotonic_ns);
    root["started_unix_epoch_ns"] = status.started_unix_epoch_ns == 0
                                        ? boost::json::value(nullptr)
                                        : boost::json::value(status.started_unix_epoch_ns);
    root["stopped_monotonic_ns"] = status.stopped_monotonic_ns == 0
                                       ? boost::json::value(nullptr)
                                       : boost::json::value(status.stopped_monotonic_ns);
    root["stopped_unix_epoch_ns"] = status.stopped_unix_epoch_ns == 0
                                        ? boost::json::value(nullptr)
                                        : boost::json::value(status.stopped_unix_epoch_ns);
    root["artifact_reference"] = status.finalized_artifact_reference.empty()
                                     ? boost::json::value(nullptr)
                                     : boost::json::value(status.finalized_artifact_reference);
    root["last_error"] = nullptr;
    return boost::json::serialize(root);
}

ProviderHttpResponse makeErrorResponse(int status, const std::string& code,
                                       const std::string& message, bool retryable) {
    boost::json::object error;
    error["code"] = code;
    error["message"] = message;
    error["retryable"] = retryable;
    boost::json::object root;
    root["schema_version"] = 1;
    root["error"] = std::move(error);
    return {status, "application/json", boost::json::serialize(root), {}};
}

std::vector<std::pair<std::string, std::string>> makeIdentityHeaders(
    const FrameIdentity& identity, const CameraMountTransform& transform) {
    return {
        {"X-Nodus-Capture-Generation", std::to_string(identity.capture_generation)},
        {"X-Nodus-Frame-Number", std::to_string(identity.frame_number)},
        {"X-Nodus-Capture-Timestamp-Ns", std::to_string(identity.capture_timestamp_ns)},
        {"X-Nodus-Capture-Unix-Epoch-Ns", std::to_string(identity.capture_unix_epoch_ns)},
        {"X-Nodus-Sensor-Frame", transform.sensor_frame},
        {"X-Nodus-Mount-Frame", transform.mount_frame},
        {"X-Nodus-Calibration-Id", transform.calibration_id},
    };
}

bool isColorEnabled(const VisionConfig& config) {
    return config.adapter == "fake" || config.intel_d435.enable_color;
}

RoiRequest parseRoiRequest(const std::string& body) {
    const boost::json::value parsed = boost::json::parse(body);
    if (!parsed.is_object()) {
        throw std::invalid_argument("ROI request must be an object.");
    }
    const boost::json::object& input = parsed.as_object();
    if (input.size() != 4U) {
        throw std::invalid_argument("ROI request field count is invalid.");
    }
    return {
        input.at("x").to_number<int>(),
        input.at("y").to_number<int>(),
        input.at("width").to_number<int>(),
        input.at("height").to_number<int>(),
    };
}

PixelRequest parsePixelRequest(const std::string& body) {
    const boost::json::value parsed = boost::json::parse(body);
    if (!parsed.is_object()) {
        throw std::invalid_argument("Pixel request must be an object.");
    }
    const boost::json::object& input = parsed.as_object();
    if (input.size() != 2U) {
        throw std::invalid_argument("Pixel request field count is invalid.");
    }
    return {input.at("x").to_number<int>(), input.at("y").to_number<int>()};
}

bool isRoiRequestValid(const RoiRequest& request, const StreamProfile& profile) {
    if (request.x < 0 || request.y < 0 || request.width <= 0 || request.height <= 0 ||
        request.x >= profile.width || request.y >= profile.height) {
        return false;
    }
    const std::int64_t requested_area = static_cast<std::int64_t>(request.width) * request.height;
    const std::int64_t frame_area = static_cast<std::int64_t>(profile.width) * profile.height;
    return requested_area <= frame_area;
}

bool isPixelRequestValid(const PixelRequest& request, const StreamProfile& profile) {
    return request.x >= 0 && request.y >= 0 && request.x < profile.width &&
           request.y < profile.height;
}

std::string makeHealthJson(const ProviderHealthSnapshot& health, bool recording_enabled,
                           const RecordingStatus* p_recording) {
    boost::json::object server;
    server["listening"] = health.listening;
    server["active_connections"] = health.active_connections;
    server["max_connections"] = health.max_connections;
    server["active_stream_clients"] = health.active_stream_clients;
    server["max_stream_clients"] = health.max_stream_clients;

    boost::json::object camera;
    camera["state"] = toString(health.camera.lifecycle);
    camera["capture_generation"] = health.camera.latest_identity.capture_generation;
    camera["latest_frame_number"] = health.camera.latest_identity.frame_number;
    camera["latest_frame_age_ms"] = health.camera.latest_frame_age_ms;
    camera["timeout_count"] = health.camera.timeout_count;
    camera["drop_count"] = health.camera.drop_count;

    boost::json::object root;
    root["schema_version"] = 1;
    root["state"] = toString(health.state);
    root["server"] = std::move(server);
    root["camera"] = std::move(camera);
    boost::json::object pilot;
    pilot["enabled"] = health.pilot.enabled;
    pilot["state"] = toString(health.pilot.state);
    pilot["server_instance_id"] = health.pilot.server_instance_id.empty()
                                      ? boost::json::value(nullptr)
                                      : boost::json::value(health.pilot.server_instance_id);
    pilot["catalog_generation"] = health.pilot.catalog_generation;
    pilot["descriptor_count"] = health.pilot.descriptor_count;
    pilot["retry_count"] = health.pilot.retry_count;
    pilot["last_success_age_ms"] = health.pilot.last_success_age_ms < 0
                                       ? boost::json::value(nullptr)
                                       : boost::json::value(health.pilot.last_success_age_ms);
    pilot["last_error"] = health.pilot.last_error.empty()
                              ? boost::json::value(nullptr)
                              : boost::json::value(health.pilot.last_error);
    root["pilot"] = std::move(pilot);
    boost::json::object recording;
    recording["enabled"] = recording_enabled;
    if (p_recording == nullptr) {
        recording["state"] = "disabled";
        recording["recording_id"] = nullptr;
        recording["queue_depth"] = 0;
        recording["queue_capacity"] = 0;
        recording["admitted_frame_count"] = 0;
        recording["submitted_frame_count"] = 0;
        recording["recording_drop_count"] = 0;
        recording["orphan_staging_count"] = 0;
    } else {
        recording["state"] = toString(p_recording->state);
        recording["recording_id"] = p_recording->recording_id.empty()
                                        ? boost::json::value(nullptr)
                                        : boost::json::value(p_recording->recording_id);
        recording["queue_depth"] = p_recording->queue_depth;
        recording["queue_capacity"] = p_recording->queue_capacity;
        recording["admitted_frame_count"] = p_recording->admitted_frame_count;
        recording["submitted_frame_count"] = p_recording->submitted_frame_count;
        recording["recording_drop_count"] = p_recording->recording_drop_count;
        recording["orphan_staging_count"] = p_recording->orphan_staging_count;
    }
    recording["last_error"] = nullptr;
    root["recording"] = std::move(recording);
    root["last_error"] = health.last_error.empty() ? boost::json::value(nullptr)
                                                   : boost::json::value(health.last_error);
    return boost::json::serialize(root);
}

std::string makeMetadataJson(const VisionConfig& config, const CameraMountTransform& transform) {
    boost::json::array endpoints;
    endpoints.emplace_back("/health");
    endpoints.emplace_back("/metadata");
    endpoints.emplace_back("/stream/depth.mjpg");
    endpoints.emplace_back("/snapshot/depth");
    endpoints.emplace_back("/query/roi_depth");
    endpoints.emplace_back("/query/pixel_to_point");
    endpoints.emplace_back("/snapshot/pointcloud.bin");
    if (isColorEnabled(config)) {
        endpoints.emplace_back("/stream/color.mjpg");
        endpoints.emplace_back("/snapshot/color");
    }
    if (config.recording.enabled) {
        endpoints.emplace_back("/recordings/start");
        endpoints.emplace_back("/recordings/stop");
        endpoints.emplace_back("/recordings/current");
    }

    boost::json::object calibration;
    calibration["calibration_id"] = transform.calibration_id;
    calibration["sensor_frame"] = transform.sensor_frame;
    calibration["mount_frame"] = transform.mount_frame;
    boost::json::array matrix;
    for (const double value : transform.mount_from_camera_optical_matrix4x4) {
        matrix.emplace_back(value);
    }
    calibration["mount_from_camera_optical_matrix4x4"] = std::move(matrix);

    boost::json::object root;
    root["schema_version"] = 1;
    root["api_version"] = "1.3.0";
    root["device_id"] = config.device_id;
    root["adapter"] = config.adapter;
    root["advertised_base_url"] = config.provider.advertised_base_url;
    root["calibration"] = std::move(calibration);
    root["endpoints"] = std::move(endpoints);
    return boost::json::serialize(root);
}

std::shared_ptr<const ProviderStreamFrame> makeStreamFrame(
    const std::shared_ptr<const EncodedPreview>& preview, const CameraMountTransform& transform) {
    if (preview == nullptr || preview->jpeg.empty()) {
        return nullptr;
    }
    auto p_frame = std::make_shared<ProviderStreamFrame>();
    p_frame->identity = preview->identity;
    p_frame->jpeg_data = std::shared_ptr<const std::vector<std::uint8_t>>(preview, &preview->jpeg);
    p_frame->headers = makeIdentityHeaders(preview->identity, transform);
    return p_frame;
}

}  // namespace

class VisionApplication::Impl {
   public:
    explicit Impl(VisionConfig config) : m_config(std::move(config)) {}

    VisionConfig m_config;
    CameraMountTransform m_camera_mount_transform;
    std::unique_ptr<CameraAdapter> m_p_adapter;
    std::unique_ptr<ProviderHttpServer> m_p_server;
    std::unique_ptr<PilotIntegrationClient> m_p_pilot_client;
    std::unique_ptr<RecordingManager> m_p_recording_manager;
    FrameStore m_frame_store;
    EncodedPreviewCache m_preview_cache;
    std::thread m_capture_thread;
    std::thread m_encoder_thread;
    std::atomic<bool> m_stop_requested{false};
    mutable std::mutex m_mutex;
    ProviderHealthSnapshot m_health;
    bool m_started{false};
};

VisionApplication::VisionApplication(VisionConfig config)
    : m_p_impl(std::make_unique<Impl>(std::move(config))) {}

VisionApplication::~VisionApplication() { stopApplication(); }

void VisionApplication::startApplication() {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_started) {
        return;
    }

    m_p_impl->m_stop_requested.store(false);
    m_p_impl->m_frame_store.clear();
    m_p_impl->m_preview_cache.clear();
    m_p_impl->m_health = {};
    m_p_impl->m_health.state = ProviderState::e_STARTING;
    m_p_impl->m_health.max_connections = m_p_impl->m_config.provider.max_connections;
    m_p_impl->m_health.max_stream_clients = m_p_impl->m_config.provider.max_stream_clients;
    m_p_impl->m_camera_mount_transform = buildCameraMountTransform(m_p_impl->m_config.calibration);

    if (m_p_impl->m_config.adapter == "fake") {
        m_p_impl->m_p_adapter = std::make_unique<FakeCameraAdapter>(m_p_impl->m_config.fake);
    } else {
        m_p_impl->m_p_adapter = std::make_unique<IntelD435Adapter>(m_p_impl->m_config.intel_d435);
    }

    const std::chrono::milliseconds max_frame_age(m_p_impl->m_config.provider.max_frame_age_ms);
    if (m_p_impl->m_config.recording.enabled) {
        const StreamProfile color_profile =
            m_p_impl->m_config.adapter == "fake"
                ? StreamProfile{m_p_impl->m_config.fake.width, m_p_impl->m_config.fake.height,
                                m_p_impl->m_config.fake.fps, PixelFormat::e_RGB8}
                : StreamProfile{m_p_impl->m_config.intel_d435.color_width,
                                m_p_impl->m_config.intel_d435.color_height,
                                m_p_impl->m_config.intel_d435.color_fps, PixelFormat::e_RGB8};
        m_p_impl->m_p_recording_manager = std::make_unique<RecordingManager>(RecordingManagerConfig{
            m_p_impl->m_config.recording.root,
            static_cast<std::size_t>(m_p_impl->m_config.recording.queue_capacity_frames),
            color_profile.width, color_profile.height, color_profile.fps,
            m_p_impl->m_config.recording.bit_rate_bps, m_p_impl->m_config.recording.max_duration_ms,
            m_p_impl->m_config.recording.minimum_free_bytes,
            m_p_impl->m_config.recording.finalize_timeout_ms, m_p_impl->m_config.recording.preset,
            m_p_impl->m_config.recording.tune, m_p_impl->m_config.calibration.sensor_frame,
            m_p_impl->m_config.calibration.calibration_id, m_p_impl->m_config.component_id,
            m_p_impl->m_config.device_id, "vision-" + std::to_string(getpid())});
    }
    ProviderHttpRoutes routes;
    routes.get_health = [this]() {
        if (m_p_impl->m_p_recording_manager == nullptr) {
            return ProviderHttpResponse{
                200, "application/json", makeHealthJson(getHealthSnapshot(), false, nullptr), {}};
        }
        const RecordingStatus recording_status = m_p_impl->m_p_recording_manager->getStatus();
        return ProviderHttpResponse{200,
                                    "application/json",
                                    makeHealthJson(getHealthSnapshot(), true, &recording_status),
                                    {}};
    };
    routes.get_metadata = [this]() {
        return ProviderHttpResponse{
            200,
            "application/json",
            makeMetadataJson(m_p_impl->m_config, m_p_impl->m_camera_mount_transform),
            {}};
    };
    routes.post_recording_start = [this](const std::string& body) {
        if (m_p_impl->m_p_recording_manager == nullptr) {
            return makeErrorResponse(503, "recording_disabled", "Recording is disabled.", false);
        }
        try {
            const RecordingStartResult result = m_p_impl->m_p_recording_manager->startOrReplay(
                parseRecordingStartRequest(body, m_p_impl->m_config));
            return ProviderHttpResponse{result == RecordingStartResult::e_STARTED ? 201 : 200,
                                        "application/json",
                                        "{\"schema_version\":1}",
                                        {}};
        } catch (const std::invalid_argument&) {
            return makeErrorResponse(400, "invalid_request", "Recording request is invalid.",
                                     false);
        } catch (const std::exception&) {
            return makeErrorResponse(409, "recording_conflict", "Recording request conflicts.",
                                     false);
        }
    };
    routes.post_recording_stop = [this](const std::string& body) {
        if (m_p_impl->m_p_recording_manager == nullptr) {
            return makeErrorResponse(503, "recording_disabled", "Recording is disabled.", false);
        }
        try {
            const RecordingStartRequest request = parseRecordingStopRequest(body);
            const RecordingStopResult result =
                m_p_impl->m_p_recording_manager->finalizeOrReplay(request);
            return ProviderHttpResponse{result == RecordingStopResult::e_ACCEPTED ? 202 : 200,
                                        "application/json",
                                        "{\"schema_version\":1}",
                                        {}};
        } catch (const std::invalid_argument&) {
            return makeErrorResponse(400, "invalid_request", "Recording request is invalid.",
                                     false);
        } catch (const RecordingNotFoundError&) {
            return makeErrorResponse(404, "recording_not_found", "Recording is unknown.", false);
        } catch (const std::exception&) {
            return makeErrorResponse(409, "recording_conflict", "Recording cannot stop.", false);
        }
    };
    routes.get_recording_current = [this]() {
        if (m_p_impl->m_p_recording_manager == nullptr) {
            return ProviderHttpResponse{
                200, "application/json", "{\"schema_version\":1,\"state\":\"disabled\"}", {}};
        }
        return ProviderHttpResponse{
            200,
            "application/json",
            makeRecordingCurrentJson(m_p_impl->m_p_recording_manager->getStatus()),
            {}};
    };
    if (isColorEnabled(m_p_impl->m_config)) {
        routes.get_color_snapshot = [this, max_frame_age]() {
            const std::shared_ptr<const EncodedPreview> preview =
                m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_COLOR, max_frame_age);
            if (preview == nullptr) {
                return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                         true);
            }
            return ProviderHttpResponse{
                200, "image/jpeg", std::string(preview->jpeg.begin(), preview->jpeg.end()),
                makeIdentityHeaders(preview->identity, m_p_impl->m_camera_mount_transform)};
        };
        routes.get_color_stream_frame = [this, max_frame_age]() {
            return makeStreamFrame(
                m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_COLOR, max_frame_age),
                m_p_impl->m_camera_mount_transform);
        };
    }
    routes.get_depth_snapshot = [this, max_frame_age]() {
        const std::shared_ptr<const EncodedPreview> preview =
            m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_DEPTH, max_frame_age);
        if (preview == nullptr) {
            return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                     true);
        }
        return ProviderHttpResponse{
            200, "image/jpeg", std::string(preview->jpeg.begin(), preview->jpeg.end()),
            makeIdentityHeaders(preview->identity, m_p_impl->m_camera_mount_transform)};
    };
    routes.get_depth_stream_frame = [this, max_frame_age]() {
        return makeStreamFrame(
            m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_DEPTH, max_frame_age),
            m_p_impl->m_camera_mount_transform);
    };
    routes.get_pointcloud_snapshot = [this, max_frame_age]() {
        const std::shared_ptr<const CapturedFrame> frame =
            m_p_impl->m_frame_store.acquireFreshFrame(max_frame_age);
        if (frame == nullptr) {
            return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                     true);
        }
        PointCloudSnapshot cloud =
            frame->buildPointCloudSnapshot(POINTCLOUD_MAX_POINTS, POINTCLOUD_STRIDE_PIXELS);
        cloud.mount_from_camera_optical_matrix3x4 =
            buildMountFromCameraMatrix3x4(m_p_impl->m_camera_mount_transform);
        const std::vector<std::uint8_t> bytes = writePcd1V2(cloud);
        return ProviderHttpResponse{
            200, "application/octet-stream", std::string(bytes.begin(), bytes.end()),
            makeIdentityHeaders(cloud.identity, m_p_impl->m_camera_mount_transform)};
    };
    routes.post_roi_depth = [this, max_frame_age](const std::string& body) {
        RoiRequest request;
        try {
            request = parseRoiRequest(body);
        } catch (const std::exception&) {
            return makeErrorResponse(400, "invalid_request", "ROI request is invalid.", false);
        }
        const std::shared_ptr<const CapturedFrame> frame =
            m_p_impl->m_frame_store.acquireFreshFrame(max_frame_age);
        if (frame == nullptr) {
            return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                     true);
        }
        if (!isRoiRequestValid(request, frame->getSnapshot().depth_profile)) {
            return makeErrorResponse(400, "invalid_request",
                                     "ROI request is outside the allowed bounds.", false);
        }
        try {
            const RoiDepthResult result =
                frame->queryDepthInRoi(request.x, request.y, request.width, request.height);
            return ProviderHttpResponse{
                200, "application/json",
                serializeRoiDepthResult(result, m_p_impl->m_camera_mount_transform),
                makeIdentityHeaders(result.identity, m_p_impl->m_camera_mount_transform)};
        } catch (const std::exception&) {
            return makeErrorResponse(409, "query_failed", "ROI query failed.", true);
        }
    };
    routes.post_pixel_point = [this, max_frame_age](const std::string& body) {
        PixelRequest request;
        try {
            request = parsePixelRequest(body);
        } catch (const std::exception&) {
            return makeErrorResponse(400, "invalid_request", "Pixel request is invalid.", false);
        }
        const std::shared_ptr<const CapturedFrame> frame =
            m_p_impl->m_frame_store.acquireFreshFrame(max_frame_age);
        if (frame == nullptr) {
            return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                     true);
        }
        if (!isPixelRequestValid(request, frame->getSnapshot().depth_profile)) {
            return makeErrorResponse(400, "invalid_request", "Pixel request is outside the image.",
                                     false);
        }
        try {
            std::optional<PixelPointResult> result = frame->queryPixelPoint(request.x, request.y);
            if (!result.has_value()) {
                PixelPointResult invalid;
                invalid.identity = frame->getSnapshot().identity;
                invalid.pixel_x = request.x;
                invalid.pixel_y = request.y;
                invalid.invalid_reason = "invalid_depth";
                result = std::move(invalid);
            }
            return ProviderHttpResponse{
                200, "application/json",
                serializePixelPointResult(*result, m_p_impl->m_camera_mount_transform),
                makeIdentityHeaders(result->identity, m_p_impl->m_camera_mount_transform)};
        } catch (const std::exception&) {
            return makeErrorResponse(409, "query_failed", "Pixel query failed.", true);
        }
    };

    m_p_impl->m_p_server = std::make_unique<ProviderHttpServer>(
        m_p_impl->m_config.provider.bind_host, m_p_impl->m_config.provider.port,
        m_p_impl->m_config.provider.max_connections, m_p_impl->m_config.provider.max_stream_clients,
        m_p_impl->m_config.provider.request_timeout_ms,
        m_p_impl->m_config.provider.max_header_bytes, m_p_impl->m_config.provider.max_body_bytes,
        m_p_impl->m_config.provider.allowed_origins, std::move(routes));
    m_p_impl->m_p_server->startServer();
    m_p_impl->m_health.listening = true;
    m_p_impl->m_started = true;

    try {
        m_p_impl->m_p_adapter->connectCamera();
        m_p_impl->m_p_adapter->startStream();
        m_p_impl->m_health.state = ProviderState::e_READY;

        m_p_impl->m_encoder_thread = std::thread([this]() {
            FrameIdentity last_encoded_identity;
            while (!m_p_impl->m_stop_requested.load()) {
                const std::shared_ptr<const CapturedFrame> frame =
                    m_p_impl->m_frame_store.waitForFrameAfter(
                        last_encoded_identity, std::chrono::milliseconds(ENCODER_WAIT_MS));
                if (frame == nullptr) {
                    continue;
                }
                last_encoded_identity = frame->getSnapshot().identity;
                m_p_impl->m_preview_cache.invalidateGeneration(
                    last_encoded_identity.capture_generation);

                const std::optional<VideoFrameView> color = frame->getColorFrameView();
                if (color.has_value()) {
                    try {
                        auto p_preview = std::make_shared<EncodedPreview>();
                        p_preview->identity = color->identity;
                        p_preview->jpeg = encodeRgbJpeg(*color, JPEG_QUALITY);
                        if (m_p_impl->m_preview_cache.publishPreview(PreviewKind::e_COLOR,
                                                                     p_preview)) {
                            m_p_impl->m_p_server->notifyStreamFrame(ProviderStreamKind::e_COLOR);
                        }
                    } catch (const std::exception&) {
                        std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
                        m_p_impl->m_health.last_error = "Color preview JPEG encoding failed.";
                    }
                }

                const std::optional<VideoFrameView> depth = frame->getDepthPreviewFrameView();
                if (depth.has_value()) {
                    try {
                        auto p_preview = std::make_shared<EncodedPreview>();
                        p_preview->identity = depth->identity;
                        p_preview->jpeg = encodeRgbJpeg(*depth, JPEG_QUALITY);
                        if (m_p_impl->m_preview_cache.publishPreview(PreviewKind::e_DEPTH,
                                                                     p_preview)) {
                            m_p_impl->m_p_server->notifyStreamFrame(ProviderStreamKind::e_DEPTH);
                        }
                    } catch (const std::exception&) {
                        std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
                        m_p_impl->m_health.last_error = "Depth preview JPEG encoding failed.";
                    }
                }
            }
        });

        m_p_impl->m_capture_thread = std::thread([this]() {
            const bool throttle_fake = m_p_impl->m_config.adapter == "fake";
            const std::chrono::milliseconds fake_frame_period(
                throttle_fake ? 1000 / m_p_impl->m_config.fake.fps : 0);
            while (!m_p_impl->m_stop_requested.load()) {
                try {
                    const std::shared_ptr<const CapturedFrame> frame =
                        m_p_impl->m_p_adapter->readFrame(
                            std::chrono::milliseconds(CAPTURE_TIMEOUT_MS));
                    m_p_impl->m_frame_store.publishFrame(frame);
                    if (m_p_impl->m_p_recording_manager != nullptr) {
                        m_p_impl->m_p_recording_manager->trySubmitFrame(frame);
                    }
                    if (throttle_fake) {
                        std::this_thread::sleep_for(fake_frame_period);
                    }
                } catch (const std::exception&) {
                    if (!m_p_impl->m_stop_requested.load()) {
                        bool publish_degraded_state = false;
                        {
                            std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
                            m_p_impl->m_health.last_error =
                                "Camera frame capture failed; retrying.";
                            if (m_p_impl->m_health.state != ProviderState::e_DEGRADED) {
                                m_p_impl->m_health.state = ProviderState::e_DEGRADED;
                                publish_degraded_state = true;
                            }
                        }
                        if (publish_degraded_state && m_p_impl->m_p_pilot_client != nullptr) {
                            m_p_impl->m_p_pilot_client->updateProviderState(
                                ProviderState::e_DEGRADED);
                        }
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(CAPTURE_ERROR_RETRY_MS));
                    }
                }
            }
        });
    } catch (const std::exception&) {
        m_p_impl->m_health.state = ProviderState::e_DEGRADED;
        m_p_impl->m_health.last_error = "Camera adapter connection or stream start failed.";
    }
    m_p_impl->m_p_pilot_client = std::make_unique<PilotIntegrationClient>(m_p_impl->m_config);
    m_p_impl->m_p_pilot_client->startClient(m_p_impl->m_health.state);
}

void VisionApplication::stopApplication() noexcept {
    ProviderHttpServer* p_server = nullptr;
    CameraAdapter* p_adapter = nullptr;
    PilotIntegrationClient* p_pilot_client = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
        if (!m_p_impl->m_started) {
            return;
        }
        m_p_impl->m_started = false;
        m_p_impl->m_health.state = ProviderState::e_STOPPING;
        m_p_impl->m_stop_requested.store(true);
        p_server = m_p_impl->m_p_server.get();
        p_adapter = m_p_impl->m_p_adapter.get();
        p_pilot_client = m_p_impl->m_p_pilot_client.get();
    }

    if (p_pilot_client != nullptr) {
        p_pilot_client->updateProviderState(ProviderState::e_STOPPING);
        p_pilot_client->stopClient();
    }

    if (p_server != nullptr) {
        p_server->stopServer();
    }
    if (m_p_impl->m_capture_thread.joinable()) {
        m_p_impl->m_capture_thread.join();
    }
    if (m_p_impl->m_encoder_thread.joinable()) {
        m_p_impl->m_encoder_thread.join();
    }
    if (m_p_impl->m_p_recording_manager != nullptr) {
        try {
            m_p_impl->m_p_recording_manager->finalize();
        } catch (const std::exception&) {
        }
    }
    if (p_adapter != nullptr) {
        p_adapter->stopStream();
        p_adapter->disconnectCamera();
    }
    m_p_impl->m_frame_store.clear();
    m_p_impl->m_preview_cache.clear();

    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_health.listening = false;
}

ProviderHealthSnapshot VisionApplication::getHealthSnapshot() const {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    ProviderHealthSnapshot health = m_p_impl->m_health;
    if (m_p_impl->m_p_adapter != nullptr) {
        health.camera = m_p_impl->m_p_adapter->getHealthSnapshot();
    }
    if (m_p_impl->m_p_server != nullptr) {
        health.active_connections = m_p_impl->m_p_server->getActiveConnectionCount();
        health.active_stream_clients = m_p_impl->m_p_server->getActiveStreamClientCount();
    }
    if (m_p_impl->m_p_pilot_client != nullptr) {
        health.pilot = m_p_impl->m_p_pilot_client->getSnapshot();
    }
    return health;
}

int VisionApplication::getBoundPort() const {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    return m_p_impl->m_p_server == nullptr ? 0 : m_p_impl->m_p_server->getBoundPort();
}

}  // namespace nodus_vision
