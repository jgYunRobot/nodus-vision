/**
 * @file provider_health.cpp
 * @brief provider health state serialization을 구현한다.
 */

#include <nodus_vision/provider_health.hpp>

namespace nodus_vision {

const char* toString(ProviderState state) noexcept {
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

const char* toString(PilotIntegrationState state) noexcept {
    switch (state) {
        case PilotIntegrationState::e_REGISTERING:
            return "registering";
        case PilotIntegrationState::e_ONLINE:
            return "online";
        case PilotIntegrationState::e_RECOVERING:
            return "recovering";
        case PilotIntegrationState::e_CONTRACT_FAULT:
            return "contract_fault";
        case PilotIntegrationState::e_STOPPING:
            return "stopping";
        case PilotIntegrationState::e_STOPPED:
            return "stopped";
        case PilotIntegrationState::e_DISABLED:
        default:
            return "disabled";
    }
}

}  // namespace nodus_vision
