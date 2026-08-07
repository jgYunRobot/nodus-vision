/**
 * @file fake_camera_adapter.cpp
 * @brief deterministic RGBD fake camera adapter를 구현한다.
 */

#include "fake_camera_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nodus_vision {
namespace {

constexpr float DEPTH_BASE_M = 1.0F;
constexpr float DEPTH_X_STEP_M = 0.1F;
constexpr float DEPTH_Y_STEP_M = 0.2F;
constexpr float INTRINSIC_FOCAL_LENGTH = 100.0F;
constexpr std::int64_t NS_PER_SECOND = 1000000000LL;

std::array<float, 3> deproject(const CameraIntrinsics& intrinsics, int pixel_x, int pixel_y, float depth_m)
{
    return {
        (static_cast<float>(pixel_x) - intrinsics.ppx) * depth_m / intrinsics.fx,
        (static_cast<float>(pixel_y) - intrinsics.ppy) * depth_m / intrinsics.fy,
        depth_m,
    };
}

int countSampleSlots(int width, int height, int stride_pixels)
{
    return ((width + stride_pixels - 1) / stride_pixels) *
           ((height + stride_pixels - 1) / stride_pixels);
}

class FakeCapturedFrame final : public CapturedFrame,
                                public std::enable_shared_from_this<FakeCapturedFrame> {
public:
    FakeCapturedFrame(FrameSnapshot snapshot, std::vector<std::uint8_t> color, std::vector<std::uint8_t> depth_preview, std::vector<float> depth)
        : m_snapshot(std::move(snapshot)),
          m_color(std::move(color)),
          m_depth_preview(std::move(depth_preview)),
          m_depth(std::move(depth))
    {
    }

    const FrameSnapshot& getSnapshot() const noexcept override { return m_snapshot; }

    std::optional<VideoFrameView> getColorFrameView() const override
    {
        if (!m_snapshot.has_color) {
            return std::nullopt;
        }
        const std::shared_ptr<const FakeCapturedFrame> owner = shared_from_this();
        return VideoFrameView{
            m_snapshot.color_profile.width,
            m_snapshot.color_profile.height,
            m_snapshot.color_profile.width * 3,
            PixelFormat::e_RGB8,
            std::shared_ptr<const void>(owner, static_cast<const void*>(this)),
            m_color.data(),
            m_snapshot.identity,
        };
    }

    std::optional<VideoFrameView> getDepthPreviewFrameView() const override
    {
        const std::shared_ptr<const FakeCapturedFrame> owner = shared_from_this();
        return VideoFrameView{m_snapshot.depth_profile.width, m_snapshot.depth_profile.height,
            m_snapshot.depth_profile.width * 3, PixelFormat::e_RGB8,
            std::shared_ptr<const void>(owner, static_cast<const void*>(this)), m_depth_preview.data(),
            m_snapshot.identity};
    }

    std::optional<PixelPointResult> queryPixelPoint(int pixel_x, int pixel_y) const override
    {
        PixelPointResult result;
        result.identity = m_snapshot.identity;
        result.pixel_x = pixel_x;
        result.pixel_y = pixel_y;
        if (pixel_x < 0 || pixel_y < 0 || pixel_x >= m_snapshot.depth_profile.width ||
            pixel_y >= m_snapshot.depth_profile.height) {
            result.invalid_reason = "pixel_out_of_bounds";
            return result;
        }
        const float depth_m = m_depth.at(static_cast<std::size_t>(pixel_y * m_snapshot.depth_profile.width + pixel_x));
        if (depth_m <= 0.0F) {
            result.invalid_reason = "invalid_depth";
            return result;
        }
        result.valid = true;
        result.depth_m = depth_m;
        result.optical_point_m = deproject(m_snapshot.depth_intrinsics, pixel_x, pixel_y, depth_m);
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
        const int min_x = std::clamp(pixel_x, 0, m_snapshot.depth_profile.width);
        const int min_y = std::clamp(pixel_y, 0, m_snapshot.depth_profile.height);
        const int max_x = std::clamp(pixel_x + width, 0, m_snapshot.depth_profile.width);
        const int max_y = std::clamp(pixel_y + height, 0, m_snapshot.depth_profile.height);
        result.clamped_roi = {min_x, min_y, std::max(0, max_x - min_x), std::max(0, max_y - min_y)};
        result.pixel_count = static_cast<std::uint64_t>(result.clamped_roi.width) *
                             static_cast<std::uint64_t>(result.clamped_roi.height);
        std::vector<std::pair<float, std::array<int, 2>>> samples;
        for (int y = min_y; y < max_y; ++y) {
            for (int x = min_x; x < max_x; ++x) {
                const float depth_m = m_depth.at(static_cast<std::size_t>(y * m_snapshot.depth_profile.width + x));
                if (depth_m > 0.0F) {
                    samples.emplace_back(depth_m, std::array<int, 2>{x, y});
                    result.mean_depth_m += depth_m;
                }
            }
        }
        result.valid_pixel_count = samples.size();
        result.fill_rate = result.pixel_count == 0U ? 0.0F :
            static_cast<float>(result.valid_pixel_count) / static_cast<float>(result.pixel_count);
        if (samples.empty()) {
            return result;
        }
        std::sort(samples.begin(), samples.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        result.valid = true;
        result.min_depth_m = samples.front().first;
        result.max_depth_m = samples.back().first;
        result.mean_depth_m /= static_cast<float>(samples.size());
        const auto& median = samples.at(samples.size() / 2U);
        result.median_depth_m = median.first;
        result.median_pixel_x = median.second.at(0);
        result.median_pixel_y = median.second.at(1);
        result.median_optical_point_m = deproject(
            m_snapshot.depth_intrinsics, result.median_pixel_x, result.median_pixel_y, median.first);
        return result;
    }

    PointCloudSnapshot buildPointCloudSnapshot(std::size_t max_points, int stride_pixels) const override
    {
        if (max_points == 0U || stride_pixels <= 0) {
            throw std::invalid_argument("Point cloud bounds must be positive.");
        }
        int effective_stride = stride_pixels;
        while (static_cast<std::size_t>(countSampleSlots(
                   m_snapshot.depth_profile.width, m_snapshot.depth_profile.height, effective_stride)) > max_points) {
            ++effective_stride;
        }
        PointCloudSnapshot result;
        result.identity = m_snapshot.identity;
        result.source_profile = m_snapshot.depth_profile;
        result.source_intrinsics = m_snapshot.depth_intrinsics;
        result.requested_stride_pixels = stride_pixels;
        result.stride_pixels = effective_stride;
        for (int y = 0; y < m_snapshot.depth_profile.height; y += effective_stride) {
            for (int x = 0; x < m_snapshot.depth_profile.width; x += effective_stride) {
                const float depth_m = m_depth.at(static_cast<std::size_t>(y * m_snapshot.depth_profile.width + x));
                if (depth_m <= 0.0F) {
                    continue;
                }
                PointCloudPoint point;
                point.optical_point_m = deproject(m_snapshot.depth_intrinsics, x, y, depth_m);
                const std::size_t color_index = static_cast<std::size_t>((y * m_snapshot.color_profile.width + x) * 3);
                point.color_rgb = {m_color.at(color_index), m_color.at(color_index + 1U), m_color.at(color_index + 2U)};
                result.points.push_back(point);
            }
        }
        return result;
    }

private:
    FrameSnapshot m_snapshot;
    std::vector<std::uint8_t> m_color;
    std::vector<std::uint8_t> m_depth_preview;
    std::vector<float> m_depth;
};

} // namespace

class FakeCameraAdapter::Impl {
public:
    explicit Impl(FakeCameraAdapterConfig config)
        : m_config(std::move(config))
    {
        if (m_config.width <= 0 || m_config.height <= 0 || m_config.fps <= 0) {
            throw std::invalid_argument("Fake camera width, height, and fps must be positive.");
        }
        m_health.device_id = m_config.device_id;
    }

    std::shared_ptr<const CapturedFrame> makeFrame()
    {
        ++m_next_frame_number;
        FrameSnapshot snapshot;
        snapshot.identity.capture_generation = m_capture_generation;
        snapshot.identity.frame_number = m_next_frame_number;
        snapshot.identity.capture_timestamp_ns = static_cast<std::int64_t>(m_next_frame_number) * NS_PER_SECOND / m_config.fps;
        snapshot.identity.capture_unix_epoch_ns = snapshot.identity.capture_timestamp_ns;
        snapshot.identity.device_timestamp = static_cast<double>(snapshot.identity.capture_timestamp_ns) / 1000000.0;
        snapshot.identity.device_timestamp_domain = DeviceTimestampDomain::e_HARDWARE_CLOCK;
        snapshot.depth_profile = {m_config.width, m_config.height, m_config.fps, PixelFormat::e_Z16};
        snapshot.color_profile = {m_config.width, m_config.height, m_config.fps, PixelFormat::e_RGB8};
        snapshot.depth_intrinsics = {m_config.width, m_config.height, INTRINSIC_FOCAL_LENGTH, INTRINSIC_FOCAL_LENGTH, 0.0F, 0.0F, DistortionModel::e_NONE, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F}};
        snapshot.color_intrinsics = snapshot.depth_intrinsics;
        snapshot.depth_scale_m = 0.001F;
        snapshot.has_color = true;
        snapshot.has_depth = true;
        std::vector<std::uint8_t> color(static_cast<std::size_t>(m_config.width * m_config.height * 3));
        std::vector<std::uint8_t> depth_preview(static_cast<std::size_t>(m_config.width * m_config.height * 3));
        std::vector<float> depth(static_cast<std::size_t>(m_config.width * m_config.height));
        for (int y = 0; y < m_config.height; ++y) {
            for (int x = 0; x < m_config.width; ++x) {
                const std::size_t pixel_index = static_cast<std::size_t>(y * m_config.width + x);
                color.at(pixel_index * 3U) = static_cast<std::uint8_t>((x + m_next_frame_number + m_config.pattern_seed) % 256U);
                color.at(pixel_index * 3U + 1U) = static_cast<std::uint8_t>((y + m_config.pattern_seed) % 256U);
                color.at(pixel_index * 3U + 2U) = static_cast<std::uint8_t>(m_next_frame_number % 256U);
                depth.at(pixel_index) = (x == 0 && y == 0) ? 0.0F : DEPTH_BASE_M + DEPTH_X_STEP_M * x + DEPTH_Y_STEP_M * y;
                const std::uint8_t intensity = static_cast<std::uint8_t>(std::min(255.0F, depth.at(pixel_index) * 100.0F));
                depth_preview.at(pixel_index * 3U) = intensity;
                depth_preview.at(pixel_index * 3U + 1U) = 0U;
                depth_preview.at(pixel_index * 3U + 2U) = 255U - intensity;
            }
        }
        m_health.latest_identity = snapshot.identity;
        m_health.latest_frame_age_ms = 0;
        return std::make_shared<FakeCapturedFrame>(std::move(snapshot), std::move(color), std::move(depth_preview), std::move(depth));
    }

    FakeCameraAdapterConfig m_config;
    mutable std::mutex m_mutex;
    CameraHealthSnapshot m_health;
    bool m_connected{false};
    bool m_streaming{false};
    bool m_manual_frame_ready{false};
    std::uint64_t m_capture_generation{0};
    std::uint64_t m_next_frame_number{0};
};

FakeCameraAdapter::FakeCameraAdapter(FakeCameraAdapterConfig config)
    : m_p_impl(std::make_unique<Impl>(std::move(config)))
{
}

FakeCameraAdapter::~FakeCameraAdapter() = default;

void FakeCameraAdapter::connectCamera()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_connected = true;
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_CONNECTED;
}

void FakeCameraAdapter::disconnectCamera() noexcept
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_streaming = false;
    m_p_impl->m_connected = false;
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_DISCONNECTED;
}

void FakeCameraAdapter::startStream()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_connected) {
        throw std::runtime_error("Fake camera is not connected.");
    }
    m_p_impl->m_streaming = true;
    ++m_p_impl->m_capture_generation;
    m_p_impl->m_next_frame_number = m_p_impl->m_config.start_frame_number - 1U;
    m_p_impl->m_health.lifecycle = CameraLifecycle::e_STREAMING;
}

void FakeCameraAdapter::stopStream() noexcept
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_streaming = false;
    m_p_impl->m_health.lifecycle = m_p_impl->m_connected ? CameraLifecycle::e_CONNECTED : CameraLifecycle::e_DISCONNECTED;
}

std::shared_ptr<const CapturedFrame> FakeCameraAdapter::readFrame(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_streaming) {
        throw std::runtime_error("Fake camera stream is not running.");
    }
    if (timeout.count() <= 0 || m_p_impl->m_config.inject_timeout) {
        ++m_p_impl->m_health.timeout_count;
        throw std::runtime_error("Fake camera frame read timed out.");
    }
    if (m_p_impl->m_config.inject_read_error) {
        m_p_impl->m_health.last_diagnostic = "Fake camera injected read error.";
        throw std::runtime_error(m_p_impl->m_health.last_diagnostic);
    }
    if (!m_p_impl->m_config.auto_advance && !m_p_impl->m_manual_frame_ready) {
        ++m_p_impl->m_health.timeout_count;
        throw std::runtime_error("Fake camera has no caller-advanced frame.");
    }
    m_p_impl->m_manual_frame_ready = false;
    return m_p_impl->makeFrame();
}

CameraHealthSnapshot FakeCameraAdapter::getHealthSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    return m_p_impl->m_health;
}

void FakeCameraAdapter::advanceFrame()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_manual_frame_ready = true;
}

} // namespace nodus_vision
