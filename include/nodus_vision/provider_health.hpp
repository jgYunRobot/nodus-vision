/**
 * @file provider_health.hpp
 * @brief provider process health의 camera-neutral public value contract를 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HEALTH_HPP_
#define NODUS_VISION_PROVIDER_HEALTH_HPP_

#include <nodus_vision/camera_contracts.hpp>
#include <string>

namespace nodus_vision {

/** @brief provider application lifecycle state다. */
enum class ProviderState { e_STARTING, e_READY, e_DEGRADED, e_STOPPING };

/** @brief health response에서 전달할 bounded server snapshot이다. */
struct ProviderHealthSnapshot {
    ProviderState state{ProviderState::e_STARTING};
    bool listening{false};
    int active_connections{0};
    int max_connections{0};
    int active_stream_clients{0};
    int max_stream_clients{0};
    CameraHealthSnapshot camera;
    std::string last_error;
};

/** @brief provider state의 stable serialization을 반환한다. */
const char* toString(ProviderState state) noexcept;

}  // namespace nodus_vision

#endif  // NODUS_VISION_PROVIDER_HEALTH_HPP_
