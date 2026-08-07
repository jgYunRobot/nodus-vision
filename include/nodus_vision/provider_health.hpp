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

/** @brief redacted Pilot lifecycle client state다. */
enum class PilotIntegrationState {
    e_DISABLED,
    e_REGISTERING,
    e_ONLINE,
    e_RECOVERING,
    e_CONTRACT_FAULT,
    e_STOPPING,
    e_STOPPED,
};

/** @brief health response에 포함할 redacted Pilot snapshot이다. */
struct PilotIntegrationSnapshot {
    bool enabled{false};
    PilotIntegrationState state{PilotIntegrationState::e_DISABLED};
    std::string server_instance_id;
    std::uint64_t catalog_generation{0U};
    int descriptor_count{0};
    int retry_count{0};
    int last_success_age_ms{-1};
    std::string last_error;
};

/** @brief health response에서 전달할 bounded server snapshot이다. */
struct ProviderHealthSnapshot {
    ProviderState state{ProviderState::e_STARTING};
    bool listening{false};
    int active_connections{0};
    int max_connections{0};
    int active_stream_clients{0};
    int max_stream_clients{0};
    CameraHealthSnapshot camera;
    PilotIntegrationSnapshot pilot;
    std::string last_error;
};

/** @brief provider state의 stable serialization을 반환한다. */
const char* toString(ProviderState state) noexcept;
/** @brief Pilot integration state의 stable serialization을 반환한다. */
const char* toString(PilotIntegrationState state) noexcept;

}  // namespace nodus_vision

#endif  // NODUS_VISION_PROVIDER_HEALTH_HPP_
