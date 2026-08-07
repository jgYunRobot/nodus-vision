/**
 * @file vision_application.cpp
 * @brief Vision provider application lifecycle을 구현한다.
 */

#include "vision_application.hpp"

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

#include "encoded_preview_cache.hpp"
#include "fake_camera_adapter.hpp"
#include "intel_d435_adapter.hpp"
#include "jpeg_encoder.hpp"
#include "pcd1.hpp"
#include "pilot_integration_client.hpp"
#include "provider_http_server.hpp"
#include "query_serializer.hpp"

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
    const FrameIdentity& identity, const CalibrationConfig& calibration) {
    return {
        {"X-Nodus-Capture-Generation", std::to_string(identity.capture_generation)},
        {"X-Nodus-Frame-Number", std::to_string(identity.frame_number)},
        {"X-Nodus-Capture-Timestamp-Ns", std::to_string(identity.capture_timestamp_ns)},
        {"X-Nodus-Capture-Unix-Epoch-Ns", std::to_string(identity.capture_unix_epoch_ns)},
        {"X-Nodus-Sensor-Frame", calibration.sensor_frame},
        {"X-Nodus-Calibration-Id", calibration.calibration_id},
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

std::string makeHealthJson(const ProviderHealthSnapshot& health) {
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
    root["last_error"] = health.last_error.empty() ? boost::json::value(nullptr)
                                                   : boost::json::value(health.last_error);
    return boost::json::serialize(root);
}

std::string makeMetadataJson(const VisionConfig& config) {
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

    boost::json::object calibration;
    calibration["calibration_id"] = config.calibration.calibration_id;
    calibration["sensor_frame"] = config.calibration.sensor_frame;
    calibration["mount_frame"] = config.calibration.mount_frame;

    boost::json::object root;
    root["schema_version"] = 1;
    root["api_version"] = "1.0.0";
    root["device_id"] = config.device_id;
    root["adapter"] = config.adapter;
    root["advertised_base_url"] = config.provider.advertised_base_url;
    root["calibration"] = std::move(calibration);
    root["endpoints"] = std::move(endpoints);
    return boost::json::serialize(root);
}

std::shared_ptr<const ProviderStreamFrame> makeStreamFrame(
    const std::shared_ptr<const EncodedPreview>& preview, const CalibrationConfig& calibration) {
    if (preview == nullptr || preview->jpeg.empty()) {
        return nullptr;
    }
    auto p_frame = std::make_shared<ProviderStreamFrame>();
    p_frame->identity = preview->identity;
    p_frame->jpeg_data = std::shared_ptr<const std::vector<std::uint8_t>>(preview, &preview->jpeg);
    p_frame->headers = makeIdentityHeaders(preview->identity, calibration);
    return p_frame;
}

}  // namespace

class VisionApplication::Impl {
   public:
    explicit Impl(VisionConfig config) : m_config(std::move(config)) {}

    VisionConfig m_config;
    std::unique_ptr<CameraAdapter> m_p_adapter;
    std::unique_ptr<ProviderHttpServer> m_p_server;
    std::unique_ptr<PilotIntegrationClient> m_p_pilot_client;
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

    if (m_p_impl->m_config.adapter == "fake") {
        m_p_impl->m_p_adapter = std::make_unique<FakeCameraAdapter>(m_p_impl->m_config.fake);
    } else {
        m_p_impl->m_p_adapter = std::make_unique<IntelD435Adapter>(m_p_impl->m_config.intel_d435);
    }

    const std::chrono::milliseconds max_frame_age(m_p_impl->m_config.provider.max_frame_age_ms);
    ProviderHttpRoutes routes;
    routes.get_health = [this]() {
        return ProviderHttpResponse{
            200, "application/json", makeHealthJson(getHealthSnapshot()), {}};
    };
    routes.get_metadata = [this]() {
        return ProviderHttpResponse{
            200, "application/json", makeMetadataJson(m_p_impl->m_config), {}};
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
                makeIdentityHeaders(preview->identity, m_p_impl->m_config.calibration)};
        };
        routes.get_color_stream_frame = [this, max_frame_age]() {
            return makeStreamFrame(
                m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_COLOR, max_frame_age),
                m_p_impl->m_config.calibration);
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
            makeIdentityHeaders(preview->identity, m_p_impl->m_config.calibration)};
    };
    routes.get_depth_stream_frame = [this, max_frame_age]() {
        return makeStreamFrame(
            m_p_impl->m_preview_cache.acquireFreshPreview(PreviewKind::e_DEPTH, max_frame_age),
            m_p_impl->m_config.calibration);
    };
    routes.get_pointcloud_snapshot = [this, max_frame_age]() {
        const std::shared_ptr<const CapturedFrame> frame =
            m_p_impl->m_frame_store.acquireFreshFrame(max_frame_age);
        if (frame == nullptr) {
            return makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.",
                                     true);
        }
        const PointCloudSnapshot cloud =
            frame->buildPointCloudSnapshot(POINTCLOUD_MAX_POINTS, POINTCLOUD_STRIDE_PIXELS);
        const std::vector<std::uint8_t> bytes = writePcd1V2(cloud);
        return ProviderHttpResponse{
            200, "application/octet-stream", std::string(bytes.begin(), bytes.end()),
            makeIdentityHeaders(cloud.identity, m_p_impl->m_config.calibration)};
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
                200, "application/json", serializeRoiDepthResult(result),
                makeIdentityHeaders(result.identity, m_p_impl->m_config.calibration)};
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
                200, "application/json", serializePixelPointResult(*result),
                makeIdentityHeaders(result->identity, m_p_impl->m_config.calibration)};
        } catch (const std::exception&) {
            return makeErrorResponse(409, "query_failed", "Pixel query failed.", true);
        }
    };

    m_p_impl->m_p_server = std::make_unique<ProviderHttpServer>(
        m_p_impl->m_config.provider.bind_host, m_p_impl->m_config.provider.port,
        m_p_impl->m_config.provider.max_connections, m_p_impl->m_config.provider.max_stream_clients,
        m_p_impl->m_config.provider.request_timeout_ms,
        m_p_impl->m_config.provider.max_header_bytes, m_p_impl->m_config.provider.max_body_bytes,
        std::move(routes));
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
                    if (throttle_fake) {
                        std::this_thread::sleep_for(fake_frame_period);
                    }
                } catch (const std::exception&) {
                    if (!m_p_impl->m_stop_requested.load()) {
                        {
                            std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
                            m_p_impl->m_health.last_error =
                                "Camera frame capture failed; retrying.";
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
