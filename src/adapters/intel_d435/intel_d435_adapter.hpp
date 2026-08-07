/**
 * @file intel_d435_adapter.hpp
 * @brief Intel D435의 private vendor adapter configuration을 제공한다.
 */

#ifndef NODUS_VISION_ADAPTERS_INTEL_D435_INTEL_D435_ADAPTER_HPP_
#define NODUS_VISION_ADAPTERS_INTEL_D435_INTEL_D435_ADAPTER_HPP_

#include <memory>
#include <string>

#include <nodus_vision/camera_adapter.hpp>

namespace nodus_vision {

/** @brief Intel D435 adapter의 typed immutable configuration이다. */
struct IntelD435AdapterConfig {
    std::string serial_number;
    std::string device_name_filter{"D435"};
    int depth_width{640};
    int depth_height{480};
    int depth_fps{30};
    bool enable_color{true};
    int color_width{640};
    int color_height{480};
    int color_fps{30};
    float depth_min_m{0.0F};
    float depth_max_m{10.0F};
    std::string device_id;
};

/** @brief configuration을 hardware access 없이 검증한다. */
void validateIntelD435AdapterConfig(const IntelD435AdapterConfig& config);

/** @brief librealsense-owned D435 camera adapter다. */
class IntelD435Adapter final : public CameraAdapter {
public:
    explicit IntelD435Adapter(IntelD435AdapterConfig config);
    ~IntelD435Adapter() override;

    IntelD435Adapter(const IntelD435Adapter&) = delete;
    IntelD435Adapter& operator=(const IntelD435Adapter&) = delete;

    void connectCamera() override;
    void disconnectCamera() noexcept override;
    void startStream() override;
    void stopStream() noexcept override;
    std::shared_ptr<const CapturedFrame> readFrame(std::chrono::milliseconds timeout) override;
    CameraHealthSnapshot getHealthSnapshot() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

} // namespace nodus_vision

#endif // NODUS_VISION_ADAPTERS_INTEL_D435_INTEL_D435_ADAPTER_HPP_
