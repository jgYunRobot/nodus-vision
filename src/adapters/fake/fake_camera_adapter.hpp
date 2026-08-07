/**
 * @file fake_camera_adapter.hpp
 * @brief hardware-independent acceptance를 위한 deterministic camera adapter를 제공한다.
 */

#ifndef NODUS_VISION_ADAPTERS_FAKE_FAKE_CAMERA_ADAPTER_HPP_
#define NODUS_VISION_ADAPTERS_FAKE_FAKE_CAMERA_ADAPTER_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include <nodus_vision/camera_adapter.hpp>

namespace nodus_vision {

/** @brief deterministic fake camera stream configuration이다. */
struct FakeCameraAdapterConfig {
    int width{8};
    int height{6};
    int fps{30};
    std::uint64_t start_frame_number{1};
    std::uint32_t pattern_seed{7};
    bool auto_advance{true};
    bool inject_timeout{false};
    bool inject_read_error{false};
    std::string device_id{"fake_camera"};
};

/** @brief caller-driven frame advance를 지원하는 deterministic fake adapter다. */
class FakeCameraAdapter final : public CameraAdapter {
public:
    explicit FakeCameraAdapter(FakeCameraAdapterConfig config);
    ~FakeCameraAdapter() override;

    void connectCamera() override;
    void disconnectCamera() noexcept override;
    void startStream() override;
    void stopStream() noexcept override;
    std::shared_ptr<const CapturedFrame> readFrame(std::chrono::milliseconds timeout) override;
    CameraHealthSnapshot getHealthSnapshot() const override;

    /** @brief manual mode에서 다음 deterministic frame을 queue한다. */
    void advanceFrame();

private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

} // namespace nodus_vision

#endif // NODUS_VISION_ADAPTERS_FAKE_FAKE_CAMERA_ADAPTER_HPP_
