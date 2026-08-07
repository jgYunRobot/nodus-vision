/**
 * @file vision_application.hpp
 * @brief Vision provider의 ordered startup과 shutdown lifecycle을 제공한다.
 */

#ifndef NODUS_VISION_RUNTIME_VISION_APPLICATION_HPP_
#define NODUS_VISION_RUNTIME_VISION_APPLICATION_HPP_

#include <memory>
#include <nodus_vision/provider_health.hpp>
#include <string>

#include "vision_config.hpp"

namespace nodus_vision {

/** @brief config, adapter, latest frame, HTTP provider를 소유하는 application이다. */
class VisionApplication {
   public:
    explicit VisionApplication(VisionConfig config);
    ~VisionApplication();
    void startApplication();
    void stopApplication() noexcept;
    ProviderHealthSnapshot getHealthSnapshot() const;
    int getBoundPort() const;

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RUNTIME_VISION_APPLICATION_HPP_
