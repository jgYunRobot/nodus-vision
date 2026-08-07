/**
 * @file camera_adapter.hpp
 * @brief immutable capture frame과 camera adapter의 vendor-neutral boundary를 제공한다.
 */

#ifndef NODUS_VISION_CAMERA_ADAPTER_HPP_
#define NODUS_VISION_CAMERA_ADAPTER_HPP_

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

#include <nodus_vision/camera_contracts.hpp>

namespace nodus_vision {

/** @brief 하나의 immutable captured camera frame을 표현한다. */
class CapturedFrame {
public:
    virtual ~CapturedFrame() = default;

    /** @return immutable frame metadata. */
    virtual const FrameSnapshot& getSnapshot() const noexcept = 0;
    /** @return color image view. */
    virtual std::optional<VideoFrameView> getColorFrameView() const = 0;
    /** @return depth preview image view. */
    virtual std::optional<VideoFrameView> getDepthPreviewFrameView() const = 0;
    /** @return pixel depth와 optical point result. */
    virtual std::optional<PixelPointResult> queryPixelPoint(int pixel_x, int pixel_y) const = 0;
    /** @return requested ROI의 depth statistics. */
    virtual RoiDepthResult queryDepthInRoi(int pixel_x, int pixel_y, int width, int height) const = 0;
    /** @return bounded raw optical point-cloud snapshot. */
    virtual PointCloudSnapshot buildPointCloudSnapshot(
        std::size_t max_points,
        int stride_pixels) const = 0;
};

/** @brief physical camera 또는 deterministic fake camera의 lifecycle boundary다. */
class CameraAdapter {
public:
    virtual ~CameraAdapter() = default;

    /** @brief configured camera를 select한다. */
    virtual void connectCamera() = 0;
    /** @brief camera selection을 release한다. */
    virtual void disconnectCamera() noexcept = 0;
    /** @brief configured stream을 시작한다. */
    virtual void startStream() = 0;
    /** @brief active stream을 중지한다. */
    virtual void stopStream() noexcept = 0;
    /** @brief next immutable frame을 bounded timeout 안에 읽는다. */
    virtual std::shared_ptr<const CapturedFrame> readFrame(
        std::chrono::milliseconds timeout) = 0;
    /** @return adapter health snapshot. */
    virtual CameraHealthSnapshot getHealthSnapshot() const = 0;
};

} // namespace nodus_vision

#endif // NODUS_VISION_CAMERA_ADAPTER_HPP_
