/**
 * @file pilot_http_transport.hpp
 * @brief bounded Pilot public HTTP request transport를 제공한다.
 */

#ifndef NODUS_VISION_PILOT_HTTP_TRANSPORT_HPP_
#define NODUS_VISION_PILOT_HTTP_TRANSPORT_HPP_

#include <memory>
#include <string>

namespace nodus_vision {

/** @brief transport가 전송할 one-shot public HTTP request다. */
struct PilotHttpRequest {
    std::string method;
    std::string target;
    std::string body;
};

/** @brief bounded public HTTP response 또는 transport failure다. */
struct PilotHttpResult {
    int status{0};
    std::string content_type;
    std::string body;
    std::string error;

    /** @return response가 complete HTTP response인지 반환한다. */
    bool hasResponse() const noexcept;
};

/** @brief lifecycle-neutral bounded HTTP/JSON transport다. */
class PilotHttpTransport {
   public:
    PilotHttpTransport(std::string base_url, int connect_timeout_ms, int request_timeout_ms,
                       int max_response_bytes);
    ~PilotHttpTransport();

    PilotHttpTransport(const PilotHttpTransport&) = delete;
    PilotHttpTransport& operator=(const PilotHttpTransport&) = delete;

    /** @brief request를 한 번 전송하고 bounded response/error를 반환한다. */
    PilotHttpResult executeRequest(const PilotHttpRequest& request);
    /** @brief 진행 중인 resolver/socket을 취소한다. */
    void cancelRequest() noexcept;

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_PILOT_HTTP_TRANSPORT_HPP_
