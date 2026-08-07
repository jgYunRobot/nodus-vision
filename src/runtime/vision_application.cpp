/**
 * @file vision_application.cpp
 * @brief Vision provider application lifecycle을 구현한다.
 */

#include "vision_application.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <nodus_vision/camera_adapter.hpp>
#include <nodus_vision/frame_store.hpp>

#include "fake_camera_adapter.hpp"
#include "intel_d435_adapter.hpp"
#include "provider_http_server.hpp"
#include "encoded_preview_cache.hpp"
#include "jpeg_encoder.hpp"
#include "pcd1.hpp"

namespace nodus_vision {
namespace {
std::vector<std::pair<std::string, std::string>> makeIdentityHeaders(const FrameIdentity& identity)
{
    return {{"X-Nodus-Capture-Generation", std::to_string(identity.capture_generation)},
            {"X-Nodus-Frame-Number", std::to_string(identity.frame_number)},
            {"X-Nodus-Capture-Timestamp-Ns", std::to_string(identity.capture_timestamp_ns)},
            {"X-Nodus-Capture-Unix-Epoch-Ns", std::to_string(identity.capture_unix_epoch_ns)}};
}
std::string makeHealthJson(const ProviderHealthSnapshot& health)
{
    std::ostringstream output;
    output << "{\"schema_version\":1,\"state\":\"" << toString(health.state)
           << "\",\"server\":{\"listening\":" << (health.listening ? "true" : "false")
           << ",\"active_connections\":" << health.active_connections
           << ",\"max_connections\":" << health.max_connections << "},\"camera\":{\"state\":\""
           << toString(health.camera.lifecycle) << "\",\"capture_generation\":"
           << health.camera.latest_identity.capture_generation << ",\"latest_frame_number\":"
           << health.camera.latest_identity.frame_number << "},\"last_error\":";
    if (health.last_error.empty()) { output << "null"; } else { output << "\"degraded\""; }
    output << "}";
    return output.str();
}
} // namespace

class VisionApplication::Impl {
public:
    explicit Impl(VisionConfig config) : m_config(std::move(config)) {}
    VisionConfig m_config;
    std::unique_ptr<CameraAdapter> m_p_adapter;
    std::unique_ptr<ProviderHttpServer> m_p_server;
    FrameStore m_frame_store;
    EncodedPreviewCache m_preview_cache;
    std::thread m_capture_thread;
    std::atomic<bool> m_stop_requested{false};
    mutable std::mutex m_mutex;
    ProviderHealthSnapshot m_health;
    bool m_started{false};
};

VisionApplication::VisionApplication(VisionConfig config) : m_p_impl(std::make_unique<Impl>(std::move(config))) {}
VisionApplication::~VisionApplication() { stopApplication(); }

void VisionApplication::startApplication()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_started) { return; }
    if (m_p_impl->m_config.adapter == "fake") { m_p_impl->m_p_adapter = std::make_unique<FakeCameraAdapter>(m_p_impl->m_config.fake); }
    else { m_p_impl->m_p_adapter = std::make_unique<IntelD435Adapter>(m_p_impl->m_config.intel_d435); }
    m_p_impl->m_health.state = ProviderState::e_STARTING;
    m_p_impl->m_health.max_connections = m_p_impl->m_config.provider.max_connections;
    m_p_impl->m_p_server = std::make_unique<ProviderHttpServer>(m_p_impl->m_config.provider.bind_host, m_p_impl->m_config.provider.port, m_p_impl->m_config.provider.max_connections, m_p_impl->m_config.provider.request_timeout_ms, m_p_impl->m_config.provider.max_header_bytes, m_p_impl->m_config.provider.max_body_bytes, ProviderHttpRoutes{
        [this]() { return ProviderHttpResponse{200, "application/json", makeHealthJson(getHealthSnapshot()), {}}; },
        [this]() { return ProviderHttpResponse{200, "application/json", "{\"schema_version\":1,\"endpoints\":[\"/health\",\"/metadata\",\"/snapshot/color\",\"/snapshot/pointcloud.bin\"],\"advertised_base_url\":\"" + m_p_impl->m_config.provider.advertised_base_url + "\"}", {}}; },
        [this]() {
            const std::shared_ptr<const EncodedPreview> preview = m_p_impl->m_preview_cache.acquirePreview(PreviewKind::e_COLOR);
            if (preview == nullptr) { return ProviderHttpResponse{503, "application/json", "{\"schema_version\":1,\"error\":{\"code\":\"no_fresh_frame\",\"message\":\"No captured frame is available.\",\"retryable\":true}}", {}}; }
            return ProviderHttpResponse{200, "image/jpeg", std::string(preview->jpeg.begin(), preview->jpeg.end()), makeIdentityHeaders(preview->identity)};
        },
        [this]() {
            const std::shared_ptr<const CapturedFrame> frame = m_p_impl->m_frame_store.acquireLatestFrame();
            if (frame == nullptr) { return ProviderHttpResponse{503, "application/json", "{\"schema_version\":1,\"error\":{\"code\":\"no_fresh_frame\",\"message\":\"No captured frame is available.\",\"retryable\":true}}", {}}; }
            const PointCloudSnapshot cloud = frame->buildPointCloudSnapshot(100000U, 1);
            const std::vector<std::uint8_t> bytes = writePcd1V2(cloud);
            return ProviderHttpResponse{200, "application/octet-stream", std::string(bytes.begin(), bytes.end()), makeIdentityHeaders(cloud.identity)};
        },
    });
    m_p_impl->m_p_server->startServer();
    m_p_impl->m_health.listening = true;
    try {
        m_p_impl->m_p_adapter->connectCamera();
        m_p_impl->m_p_adapter->startStream();
        m_p_impl->m_health.state = ProviderState::e_READY;
        m_p_impl->m_capture_thread = std::thread([this]() {
            while (!m_p_impl->m_stop_requested.load()) {
                try {
                    const std::shared_ptr<const CapturedFrame> frame =
                        m_p_impl->m_p_adapter->readFrame(std::chrono::milliseconds(100));
                    m_p_impl->m_frame_store.publishFrame(frame);
                    const std::optional<VideoFrameView> color = frame->getColorFrameView();
                    if (color.has_value()) {
                        std::shared_ptr<EncodedPreview> preview = std::make_shared<EncodedPreview>();
                        preview->identity = color->identity;
                        preview->jpeg = encodeRgbJpeg(*color, 90);
                        m_p_impl->m_preview_cache.publishPreview(PreviewKind::e_COLOR, preview);
                    }
                }
                catch (const std::exception&) { break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 / m_p_impl->m_config.fake.fps));
            }
        });
    } catch (const std::exception&) {
        m_p_impl->m_health.state = ProviderState::e_DEGRADED;
        m_p_impl->m_health.last_error = "Camera adapter connection or stream start failed.";
    }
    m_p_impl->m_started = true;
}

void VisionApplication::stopApplication() noexcept
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_started) { return; }
    m_p_impl->m_health.state = ProviderState::e_STOPPING;
    if (m_p_impl->m_p_server != nullptr) { m_p_impl->m_p_server->stopServer(); m_p_impl->m_health.listening = false; }
    m_p_impl->m_stop_requested.store(true);
    if (m_p_impl->m_capture_thread.joinable()) { m_p_impl->m_capture_thread.join(); }
    if (m_p_impl->m_p_adapter != nullptr) { m_p_impl->m_p_adapter->stopStream(); m_p_impl->m_p_adapter->disconnectCamera(); }
    m_p_impl->m_started = false;
}

ProviderHealthSnapshot VisionApplication::getHealthSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    ProviderHealthSnapshot health = m_p_impl->m_health;
    if (m_p_impl->m_p_adapter != nullptr) { health.camera = m_p_impl->m_p_adapter->getHealthSnapshot(); }
    if (m_p_impl->m_p_server != nullptr) { health.active_connections = m_p_impl->m_p_server->getActiveConnectionCount(); }
    return health;
}
int VisionApplication::getBoundPort() const { std::lock_guard<std::mutex> lock(m_p_impl->m_mutex); return m_p_impl->m_p_server == nullptr ? 0 : m_p_impl->m_p_server->getBoundPort(); }

} // namespace nodus_vision
