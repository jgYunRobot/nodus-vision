/**
 * @file intel_d435_adapter.cpp
 * @brief Intel D435 adapter lifecycle와 immutable SDK frame ownership을 구현한다.
 */

#include "intel_d435_adapter.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <librealsense2/rs.hpp>

namespace nodus_vision {
namespace {

DeviceTimestampDomain toTimestampDomain(rs2_timestamp_domain domain)
{
    switch (domain) {
    case RS2_TIMESTAMP_DOMAIN_HARDWARE_CLOCK:
        return DeviceTimestampDomain::e_HARDWARE_CLOCK;
    case RS2_TIMESTAMP_DOMAIN_SYSTEM_TIME:
        return DeviceTimestampDomain::e_SYSTEM_TIME;
    case RS2_TIMESTAMP_DOMAIN_GLOBAL_TIME:
        return DeviceTimestampDomain::e_GLOBAL_TIME;
    default:
        return DeviceTimestampDomain::e_UNKNOWN;
    }
}

PixelFormat toPixelFormat(rs2_format format)
{
    switch (format) {
    case RS2_FORMAT_RGB8:
        return PixelFormat::e_RGB8;
    case RS2_FORMAT_BGR8:
        return PixelFormat::e_BGR8;
    case RS2_FORMAT_Z16:
        return PixelFormat::e_Z16;
    default:
        return PixelFormat::e_UNKNOWN;
    }
}

StreamProfile toStreamProfile(const rs2::video_frame& frame)
{
    return {frame.get_width(), frame.get_height(), frame.get_profile().fps(), toPixelFormat(frame.get_profile().format())};
}

CameraIntrinsics toIntrinsics(const rs2::video_frame& frame)
{
    const rs2_intrinsics source = frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
    return {source.width, source.height, source.fx, source.fy, source.ppx, source.ppy, DistortionModel::e_UNKNOWN, {source.coeffs[0], source.coeffs[1], source.coeffs[2], source.coeffs[3], source.coeffs[4]}};
}

class IntelD435CapturedFrame final : public CapturedFrame,
                                     public std::enable_shared_from_this<IntelD435CapturedFrame> {
public:
    IntelD435CapturedFrame(FrameSnapshot snapshot, rs2::depth_frame depth_frame, rs2::video_frame color_frame)
        : m_snapshot(std::move(snapshot)),
          m_depth_frame(std::move(depth_frame)),
          m_color_frame(std::move(color_frame))
    {
    }

    const FrameSnapshot& getSnapshot() const noexcept override { return m_snapshot; }

    std::optional<VideoFrameView> getColorFrameView() const override
    {
        if (!m_color_frame) {
            return std::nullopt;
        }
        const std::shared_ptr<const IntelD435CapturedFrame> owner = shared_from_this();
        return VideoFrameView{m_color_frame.get_width(), m_color_frame.get_height(), m_color_frame.get_stride_in_bytes(), toPixelFormat(m_color_frame.get_profile().format()), std::shared_ptr<const void>(owner, static_cast<const void*>(this)), static_cast<const std::uint8_t*>(m_color_frame.get_data()), m_snapshot.identity};
    }

    std::optional<VideoFrameView> getDepthPreviewFrameView() const override { return std::nullopt; }

    std::optional<PixelPointResult> queryPixelPoint(int pixel_x, int pixel_y) const override
    {
        PixelPointResult result;
        result.identity = m_snapshot.identity;
        result.pixel_x = pixel_x;
        result.pixel_y = pixel_y;
        if (!m_depth_frame || pixel_x < 0 || pixel_y < 0 || pixel_x >= m_depth_frame.get_width() || pixel_y >= m_depth_frame.get_height()) {
            result.invalid_reason = "pixel_out_of_bounds";
            return result;
        }
        result.depth_m = m_depth_frame.get_distance(pixel_x, pixel_y);
        if (result.depth_m <= 0.0F) {
            result.invalid_reason = "invalid_depth";
            return result;
        }
        const float pixel[2]{static_cast<float>(pixel_x), static_cast<float>(pixel_y)};
        float point[3]{0.0F, 0.0F, 0.0F};
        const rs2_intrinsics intrinsics = m_depth_frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
        rs2_deproject_pixel_to_point(point, &intrinsics, pixel, result.depth_m);
        result.valid = true;
        result.optical_point_m = {point[0], point[1], point[2]};
        return result;
    }

    RoiDepthResult queryDepthInRoi(int pixel_x, int pixel_y, int width, int height) const override
    {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("ROI width and height must be positive.");
        }
        RoiDepthResult result;
        result.identity = m_snapshot.identity;
        result.requested_roi = {pixel_x, pixel_y, width, height};
        return result;
    }

    PointCloudSnapshot buildPointCloudSnapshot(std::size_t max_points, int stride_pixels) const override
    {
        if (max_points == 0U || stride_pixels <= 0) {
            throw std::invalid_argument("Point cloud bounds must be positive.");
        }
        PointCloudSnapshot result;
        result.identity = m_snapshot.identity;
        result.source_profile = m_snapshot.depth_profile;
        result.source_intrinsics = m_snapshot.depth_intrinsics;
        result.requested_stride_pixels = stride_pixels;
        result.stride_pixels = stride_pixels;
        return result;
    }

private:
    FrameSnapshot m_snapshot;
    rs2::depth_frame m_depth_frame;
    rs2::video_frame m_color_frame;
};

} // namespace

void validateIntelD435AdapterConfig(const IntelD435AdapterConfig& config)
{
    if (config.depth_width <= 0 || config.depth_height <= 0 || config.depth_fps <= 0 ||
        (config.enable_color && (config.color_width <= 0 || config.color_height <= 0 || config.color_fps <= 0)) ||
        config.depth_min_m < 0.0F || config.depth_max_m <= 0.0F || config.depth_min_m > config.depth_max_m) {
        throw std::invalid_argument("Intel D435 stream profile or depth range is invalid.");
    }
}

class IntelD435Adapter::Impl {
public:
    explicit Impl(IntelD435AdapterConfig config)
        : m_config(std::move(config))
    {
        validateIntelD435AdapterConfig(m_config);
        m_health.device_id = m_config.device_id;
    }

    IntelD435AdapterConfig m_config;
    mutable std::mutex m_mutex;
    rs2::context m_context;
    std::unique_ptr<rs2::pipeline> m_p_pipeline;
    std::string m_selected_serial;
    CameraHealthSnapshot m_health;
    bool m_connected{false};
    bool m_streaming{false};
    std::uint64_t m_capture_generation{0};
};

IntelD435Adapter::IntelD435Adapter(IntelD435AdapterConfig config)
    : m_p_impl(std::make_unique<Impl>(std::move(config)))
{
}

IntelD435Adapter::~IntelD435Adapter() = default;

void IntelD435Adapter::connectCamera()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    const rs2::device_list devices = m_p_impl->m_context.query_devices();
    std::vector<rs2::device> matching_devices;
    for (const rs2::device& device : devices) {
        const std::string serial = device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
        const std::string name = device.get_info(RS2_CAMERA_INFO_NAME);
        if ((!m_p_impl->m_config.serial_number.empty() && serial == m_p_impl->m_config.serial_number) ||
            (m_p_impl->m_config.serial_number.empty() && name.find(m_p_impl->m_config.device_name_filter) != std::string::npos)) {
            matching_devices.push_back(device);
        }
    }
    if (matching_devices.size() != 1U) {
        throw std::runtime_error("Intel D435 selection requires exactly one matching device.");
    }
    m_p_impl->m_selected_serial = matching_devices.front().get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
    m_p_impl->m_health.device_name = matching_devices.front().get_info(RS2_CAMERA_INFO_NAME);
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_CONNECTED;
    m_p_impl->m_connected = true;
}

void IntelD435Adapter::disconnectCamera() noexcept
{
    stopStream();
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_selected_serial.clear();
    m_p_impl->m_connected = false;
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_DISCONNECTED;
}

void IntelD435Adapter::startStream()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_connected) {
        throw std::runtime_error("Intel D435 is not connected.");
    }
    rs2::config config;
    config.enable_device(m_p_impl->m_selected_serial);
    config.enable_stream(RS2_STREAM_DEPTH, m_p_impl->m_config.depth_width, m_p_impl->m_config.depth_height, RS2_FORMAT_Z16, m_p_impl->m_config.depth_fps);
    if (m_p_impl->m_config.enable_color) {
        config.enable_stream(RS2_STREAM_COLOR, m_p_impl->m_config.color_width, m_p_impl->m_config.color_height, RS2_FORMAT_RGB8, m_p_impl->m_config.color_fps);
    }
    m_p_impl->m_p_pipeline = std::make_unique<rs2::pipeline>(m_p_impl->m_context);
    m_p_impl->m_p_pipeline->start(config);
    ++m_p_impl->m_capture_generation;
    m_p_impl->m_streaming = true;
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_STREAMING;
}

void IntelD435Adapter::stopStream() noexcept
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_p_pipeline != nullptr) {
        try {
            m_p_impl->m_p_pipeline->stop();
        } catch (const rs2::error&) {
        }
        m_p_impl->m_p_pipeline.reset();
    }
    m_p_impl->m_streaming = false;
    m_p_impl->m_health.lifecycle = m_p_impl->m_connected ? CameraLifecycle::e_CONNECTED : CameraLifecycle::e_DISCONNECTED;
}

std::shared_ptr<const CapturedFrame> IntelD435Adapter::readFrame(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_streaming) {
        throw std::runtime_error("Intel D435 stream is not running.");
    }
    try {
        const rs2::frameset frameset = m_p_impl->m_p_pipeline->wait_for_frames(static_cast<unsigned int>(timeout.count()));
        const rs2::depth_frame depth_frame = frameset.get_depth_frame();
        const rs2::video_frame color_frame = frameset.get_color_frame();
        FrameSnapshot snapshot;
        snapshot.identity = {m_p_impl->m_capture_generation, depth_frame.get_frame_number(), 0, 0, depth_frame.get_timestamp(), toTimestampDomain(depth_frame.get_frame_timestamp_domain())};
        snapshot.depth_profile = toStreamProfile(depth_frame);
        snapshot.depth_intrinsics = toIntrinsics(depth_frame);
        snapshot.has_depth = static_cast<bool>(depth_frame);
        snapshot.has_color = static_cast<bool>(color_frame);
        if (color_frame) { snapshot.color_profile = toStreamProfile(color_frame); snapshot.color_intrinsics = toIntrinsics(color_frame); }
        m_p_impl->m_health.latest_identity = snapshot.identity;
        return std::make_shared<IntelD435CapturedFrame>(std::move(snapshot), depth_frame, color_frame);
    } catch (const rs2::error& error) {
        ++m_p_impl->m_health.timeout_count;
        m_p_impl->m_health.last_diagnostic = error.what();
        throw std::runtime_error("Intel D435 frame read failed.");
    }
}

CameraHealthSnapshot IntelD435Adapter::getHealthSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    return m_p_impl->m_health;
}

} // namespace nodus_vision
