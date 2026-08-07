/**
 * @file pilot_integration_client.hpp
 * @brief single-worker Pilot component lifecycle client를 제공한다.
 */

#ifndef NODUS_VISION_PILOT_INTEGRATION_CLIENT_HPP_
#define NODUS_VISION_PILOT_INTEGRATION_CLIENT_HPP_

#include <memory>
#include <nodus_vision/provider_health.hpp>

#include "pilot_contract_codec.hpp"
#include "vision_config.hpp"

namespace nodus_vision {

/** @brief Vision component lifecycle session과 sequence의 single-worker owner다. */
class PilotIntegrationClient {
   public:
    PilotIntegrationClient(VisionConfig config, InstanceIdGenerator instance_id_generator = {});
    ~PilotIntegrationClient();

    PilotIntegrationClient(const PilotIntegrationClient&) = delete;
    PilotIntegrationClient& operator=(const PilotIntegrationClient&) = delete;

    /** @brief local provider bind 이후 lifecycle worker를 시작한다. */
    void startClient(ProviderState initial_state);
    /** @brief latest coarse state만 worker에 전달한다. */
    void updateProviderState(ProviderState state);
    /** @brief bounded disconnect를 시도하고 worker를 종료한다. */
    void stopClient() noexcept;
    /** @brief session secret을 제외한 integration snapshot을 반환한다. */
    PilotIntegrationSnapshot getSnapshot() const;

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_PILOT_INTEGRATION_CLIENT_HPP_
