/**
 * @file provider_http_server.hpp
 * @brief bounded asynchronous health/metadata HTTP provider를 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_PROVIDER_HTTP_SERVER_HPP_
#define NODUS_VISION_PROVIDER_HTTP_PROVIDER_HTTP_SERVER_HPP_

#include <functional>
#include <memory>
#include <string>

namespace nodus_vision {

/** @brief Phase 2 health/metadata route payload callback이다. */
struct ProviderHttpRoutes {
    std::function<std::string()> get_health_json;
    std::function<std::string()> get_metadata_json;
};

/** @brief asynchronous bounded HTTP listener다. */
class ProviderHttpServer {
public:
    ProviderHttpServer(
        std::string bind_host,
        int port,
        int max_connections,
        int request_timeout_ms,
        int max_header_bytes,
        int max_body_bytes,
        ProviderHttpRoutes routes);
    ~ProviderHttpServer();

    ProviderHttpServer(const ProviderHttpServer&) = delete;
    ProviderHttpServer& operator=(const ProviderHttpServer&) = delete;

    /** @brief listener를 bind하고 worker를 시작한다. */
    void startServer();
    /** @brief new accept를 중단하고 active session을 bounded time 안에 close한다. */
    void stopServer() noexcept;
    /** @return 실제 bind된 TCP port다. */
    int getBoundPort() const;
    /** @return active HTTP session 수다. */
    int getActiveConnectionCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

} // namespace nodus_vision

#endif // NODUS_VISION_PROVIDER_HTTP_PROVIDER_HTTP_SERVER_HPP_
