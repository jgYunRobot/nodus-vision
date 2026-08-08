/**
 * @file camera_contracts.hpp
 * @brief 카메라 vendor와 무관한 frame, geometry, health value contract를 제공한다.
 */

#ifndef NODUS_VISION_CAMERA_CONTRACTS_HPP_
#define NODUS_VISION_CAMERA_CONTRACTS_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nodus_vision {

/** @brief camera-neutral pixel format. */
enum class PixelFormat {
    e_UNKNOWN,
    e_RGB8,
    e_BGR8,
    e_Z16,
};

/** @brief device timestamp의 관측 clock domain. */
enum class DeviceTimestampDomain {
    e_UNKNOWN,
    e_HARDWARE_CLOCK,
    e_SYSTEM_TIME,
    e_GLOBAL_TIME,
};

/** @brief camera distortion model. */
enum class DistortionModel {
    e_UNKNOWN,
    e_NONE,
    e_MODIFIED_BROWN_CONRADY,
    e_INVERSE_BROWN_CONRADY,
    e_FTHETA,
    e_BROWN_CONRADY,
    e_KANNALA_BRANDT4,
};

/** @brief camera adapter lifecycle. */
enum class CameraLifecycle {
    e_DISCONNECTED,
    e_CONNECTED,
    e_STREAMING,
    e_DEGRADED,
};

/** @brief bounded enum의 stable text serialization을 반환한다. */
const char* toString(PixelFormat format) noexcept;
/** @brief bounded enum의 stable text serialization을 반환한다. */
const char* toString(DeviceTimestampDomain domain) noexcept;
/** @brief bounded enum의 stable text serialization을 반환한다. */
const char* toString(DistortionModel model) noexcept;
/** @brief bounded enum의 stable text serialization을 반환한다. */
const char* toString(CameraLifecycle lifecycle) noexcept;

/** @brief 하나의 video/depth stream profile이다. */
struct StreamProfile {
    int width{0};
    int height{0};
    int fps{0};
    PixelFormat format{PixelFormat::e_UNKNOWN};
};

/** @brief pinhole camera intrinsics와 distortion snapshot이다. */
struct CameraIntrinsics {
    int width{0};
    int height{0};
    float fx{0.0F};
    float fy{0.0F};
    float ppx{0.0F};
    float ppy{0.0F};
    DistortionModel distortion_model{DistortionModel::e_UNKNOWN};
    std::array<float, 5> distortion_coefficients{0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
};

/** @brief immutable captured frame의 process-local identity이다. */
struct FrameIdentity {
    std::uint64_t capture_generation{0};
    std::uint64_t frame_number{0};
    std::int64_t capture_timestamp_ns{0};
    std::int64_t capture_unix_epoch_ns{0};
    double device_timestamp{0.0};
    DeviceTimestampDomain device_timestamp_domain{DeviceTimestampDomain::e_UNKNOWN};
};

/** @brief frame의 stream/profile/calibration-independent metadata이다. */
struct FrameSnapshot {
    FrameIdentity identity;
    StreamProfile depth_profile;
    StreamProfile color_profile;
    CameraIntrinsics depth_intrinsics;
    CameraIntrinsics color_intrinsics;
    float depth_scale_m{0.0F};
    bool has_color{false};
    bool has_depth{false};
};

/** @brief owner가 살아있는 동안만 유효한 immutable image byte view이다. */
struct VideoFrameView {
    int width{0};
    int height{0};
    int stride_bytes{0};
    PixelFormat format{PixelFormat::e_UNKNOWN};
    std::shared_ptr<const void> owner;
    const std::uint8_t* p_data{nullptr};
    FrameIdentity identity;
};

/** @brief pixel depth/deprojection 결과이다. */
struct PixelPointResult {
    FrameIdentity identity;
    int pixel_x{0};
    int pixel_y{0};
    bool valid{false};
    float depth_m{0.0F};
    std::array<float, 3> optical_point_m{0.0F, 0.0F, 0.0F};
    std::string invalid_reason;
};

/** @brief image boundary에 clamp된 ROI다. */
struct PixelRoi {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

/** @brief depth ROI 통계와 median optical point다. */
struct RoiDepthResult {
    FrameIdentity identity;
    PixelRoi requested_roi;
    PixelRoi clamped_roi;
    std::uint64_t pixel_count{0};
    std::uint64_t valid_pixel_count{0};
    float fill_rate{0.0F};
    bool valid{false};
    float min_depth_m{0.0F};
    float max_depth_m{0.0F};
    float mean_depth_m{0.0F};
    float median_depth_m{0.0F};
    int median_pixel_x{0};
    int median_pixel_y{0};
    std::array<float, 3> median_optical_point_m{0.0F, 0.0F, 0.0F};
};

/** @brief raw camera optical coordinate RGB point다. */
struct PointCloudPoint {
    std::array<float, 3> optical_point_m{0.0F, 0.0F, 0.0F};
    std::array<std::uint8_t, 3> color_rgb{255U, 255U, 255U};
};

/** @brief bounded raw optical point-cloud snapshot이다. */
struct PointCloudSnapshot {
    FrameIdentity identity;
    StreamProfile source_profile;
    CameraIntrinsics source_intrinsics;
    int requested_stride_pixels{1};
    int stride_pixels{1};
    std::array<float, 12> mount_from_camera_optical_matrix3x4{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
                                                              0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    std::vector<PointCloudPoint> points;
};

/** @brief camera adapter의 low-rate health snapshot이다. */
struct CameraHealthSnapshot {
    CameraLifecycle lifecycle{CameraLifecycle::e_DISCONNECTED};
    std::string device_id;
    std::string device_name;
    FrameIdentity latest_identity;
    std::int64_t latest_frame_age_ms{-1};
    std::uint64_t timeout_count{0};
    std::uint64_t drop_count{0};
    std::string last_diagnostic;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_CAMERA_CONTRACTS_HPP_
