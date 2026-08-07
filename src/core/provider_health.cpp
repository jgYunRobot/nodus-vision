/**
 * @file provider_health.cpp
 * @brief provider health state serialization을 구현한다.
 */

#include <nodus_vision/provider_health.hpp>

namespace nodus_vision {

const char* toString(ProviderState state) noexcept
{
    switch (state) {
    case ProviderState::e_READY:
        return "ready";
    case ProviderState::e_DEGRADED:
        return "degraded";
    case ProviderState::e_STOPPING:
        return "stopping";
    case ProviderState::e_STARTING:
    default:
        return "starting";
    }
}

} // namespace nodus_vision
