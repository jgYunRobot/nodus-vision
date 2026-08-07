/**
 * @file pilot_http_transport.cpp
 * @brief bounded asynchronous Pilot public HTTP transport을 구현한다.
 */

#include "pilot_http_transport.hpp"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

struct ParsedBaseUrl {
    std::string host;
    std::string port;
    std::string base_path;
};

ParsedBaseUrl parseBaseUrl(const std::string& base_url) {
    constexpr const char* HTTP_PREFIX = "http://";
    if (base_url.rfind(HTTP_PREFIX, 0U) != 0U || base_url.find('@') != std::string::npos ||
        base_url.find('?') != std::string::npos || base_url.find('#') != std::string::npos) {
        throw std::invalid_argument("Pilot base URL must be an absolute HTTP URL.");
    }
    const std::size_t authority_start = std::string(HTTP_PREFIX).size();
    const std::size_t path_start = base_url.find('/', authority_start);
    const std::string authority = base_url.substr(authority_start, path_start - authority_start);
    const std::size_t port_separator = authority.rfind(':');
    if (port_separator == std::string::npos || port_separator == 0U ||
        port_separator == authority.size() - 1U) {
        throw std::invalid_argument("Pilot base URL must contain a host and explicit port.");
    }
    for (std::size_t index = port_separator + 1U; index < authority.size(); ++index) {
        if (authority.at(index) < '0' || authority.at(index) > '9') {
            throw std::invalid_argument("Pilot base URL port is invalid.");
        }
    }
    const int port = std::stoi(authority.substr(port_separator + 1U));
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("Pilot base URL port is outside the valid range.");
    }
    return {authority.substr(0U, port_separator), authority.substr(port_separator + 1U),
            path_start == std::string::npos ? "" : base_url.substr(path_start)};
}

bool isJsonContentType(const std::string& content_type) {
    constexpr const char* JSON_CONTENT_TYPE = "application/json";
    return content_type.rfind(JSON_CONTENT_TYPE, 0U) == 0U &&
           (content_type.size() == std::char_traits<char>::length(JSON_CONTENT_TYPE) ||
            content_type.at(std::char_traits<char>::length(JSON_CONTENT_TYPE)) == ';');
}

class RequestSession final : public std::enable_shared_from_this<RequestSession> {
   public:
    using Completion = std::function<void(PilotHttpResult)>;

    RequestSession(asio::io_context& io_context, ParsedBaseUrl base_url, PilotHttpRequest request,
                   int connect_timeout_ms, int request_timeout_ms, int max_response_bytes,
                   Completion completion)
        : m_resolver(io_context),
          m_stream(io_context),
          m_timer(io_context),
          m_base_url(std::move(base_url)),
          m_connect_timeout_ms(connect_timeout_ms),
          m_request_timeout_ms(request_timeout_ms),
          m_max_response_bytes(max_response_bytes),
          m_completion(std::move(completion)) {
        const http::verb method = http::string_to_verb(request.method);
        if (method == http::verb::unknown || request.target.empty() ||
            request.target.front() != '/') {
            throw std::invalid_argument("Pilot HTTP request method or target is invalid.");
        }
        m_request = {method, m_base_url.base_path + request.target, 11};
        m_request.set(http::field::host, m_base_url.host);
        m_request.set(http::field::accept, "application/json");
        m_request.set(http::field::content_type, "application/json");
        m_request.keep_alive(false);
        m_request.body() = std::move(request.body);
        m_request.prepare_payload();
        m_response_parser.body_limit(static_cast<std::uint64_t>(m_max_response_bytes));
    }

    void startRequest() {
        armTimeout(m_connect_timeout_ms);
        m_resolver.async_resolve(
            m_base_url.host, m_base_url.port,
            beast::bind_front_handler(&RequestSession::handleResolve, shared_from_this()));
    }

    void cancelRequest() noexcept {
        asio::post(m_stream.get_executor(), [self = shared_from_this()]() {
            if (self->m_finished) {
                return;
            }
            self->m_cancelled = true;
            beast::error_code ignored;
            self->m_timer.cancel(ignored);
            self->m_resolver.cancel();
            self->m_stream.socket().cancel(ignored);
            self->finish({0, "", "", "request cancelled"});
        });
    }

   private:
    void armTimeout(int timeout_ms) {
        m_timer.expires_after(std::chrono::milliseconds(timeout_ms));
        m_timer.async_wait([self = shared_from_this()](beast::error_code error) {
            if (error || self->m_finished) {
                return;
            }
            self->m_timed_out = true;
            beast::error_code ignored;
            self->m_resolver.cancel();
            self->m_stream.socket().cancel(ignored);
            self->finish({0, "", "", "request deadline exceeded"});
        });
    }

    void handleResolve(beast::error_code error, tcp::resolver::results_type results) {
        if (error || m_finished) {
            handleError(error, "resolve failed");
            return;
        }
        beast::error_code ignored;
        m_timer.cancel(ignored);
        m_stream.expires_after(std::chrono::milliseconds(m_connect_timeout_ms));
        m_stream.async_connect(
            results, beast::bind_front_handler(&RequestSession::handleConnect, shared_from_this()));
    }

    void handleConnect(beast::error_code error, const tcp::resolver::results_type::endpoint_type&) {
        if (error || m_finished) {
            handleError(error, "connect failed");
            return;
        }
        m_stream.expires_after(std::chrono::milliseconds(m_request_timeout_ms));
        http::async_write(
            m_stream, m_request,
            beast::bind_front_handler(&RequestSession::handleWrite, shared_from_this()));
    }

    void handleWrite(beast::error_code error, std::size_t) {
        if (error || m_finished) {
            handleError(error, "write failed");
            return;
        }
        m_stream.expires_after(std::chrono::milliseconds(m_request_timeout_ms));
        http::async_read(
            m_stream, m_buffer, m_response_parser,
            beast::bind_front_handler(&RequestSession::handleRead, shared_from_this()));
    }

    void handleRead(beast::error_code error, std::size_t) {
        if (error || m_finished) {
            handleError(error, "read failed");
            return;
        }
        beast::error_code ignored;
        m_timer.cancel(ignored);
        const http::response<http::string_body>& response = m_response_parser.get();
        const std::string content_type(response[http::field::content_type]);
        if (!isJsonContentType(content_type)) {
            finish({0, content_type, "", "Pilot response content type is not application/json"});
            return;
        }
        finish({static_cast<int>(response.result_int()), content_type, response.body(), ""});
    }

    void handleError(beast::error_code error, const char* fallback) {
        if (m_finished) {
            return;
        }
        beast::error_code ignored;
        m_timer.cancel(ignored);
        if (m_timed_out || error == beast::error::timeout) {
            finish({0, "", "", "request deadline exceeded"});
        } else if (m_cancelled || error == asio::error::operation_aborted) {
            finish({0, "", "", "request cancelled"});
        } else {
            finish({0, "", "", fallback});
        }
    }

    void finish(PilotHttpResult result) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_completion) {
            Completion completion = std::move(m_completion);
            completion(std::move(result));
        }
    }

    tcp::resolver m_resolver;
    beast::tcp_stream m_stream;
    asio::steady_timer m_timer;
    beast::flat_buffer m_buffer;
    http::request<http::string_body> m_request;
    http::response_parser<http::string_body> m_response_parser;
    ParsedBaseUrl m_base_url;
    int m_connect_timeout_ms;
    int m_request_timeout_ms;
    int m_max_response_bytes;
    Completion m_completion;
    bool m_finished{false};
    bool m_cancelled{false};
    bool m_timed_out{false};
};

}  // namespace

bool PilotHttpResult::hasResponse() const noexcept { return status > 0 && error.empty(); }

class PilotHttpTransport::Impl {
   public:
    Impl(std::string base_url, int connect_timeout_ms, int request_timeout_ms,
         int max_response_bytes)
        : m_base_url(parseBaseUrl(base_url)),
          m_connect_timeout_ms(connect_timeout_ms),
          m_request_timeout_ms(request_timeout_ms),
          m_max_response_bytes(max_response_bytes) {
        if (connect_timeout_ms <= 0 || request_timeout_ms <= 0 || max_response_bytes <= 0) {
            throw std::invalid_argument("Pilot transport bounds must be positive.");
        }
    }

    ParsedBaseUrl m_base_url;
    int m_connect_timeout_ms;
    int m_request_timeout_ms;
    int m_max_response_bytes;
    asio::io_context m_io_context{1};
    std::mutex m_mutex;
    std::shared_ptr<RequestSession> m_p_active_session;
};

PilotHttpTransport::PilotHttpTransport(std::string base_url, int connect_timeout_ms,
                                       int request_timeout_ms, int max_response_bytes)
    : m_p_impl(std::make_unique<Impl>(std::move(base_url), connect_timeout_ms, request_timeout_ms,
                                      max_response_bytes)) {}

PilotHttpTransport::~PilotHttpTransport() { cancelRequest(); }

PilotHttpResult PilotHttpTransport::executeRequest(const PilotHttpRequest& request) {
    std::unique_lock<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_io_context.restart();
    PilotHttpResult result;
    m_p_impl->m_p_active_session = std::make_shared<RequestSession>(
        m_p_impl->m_io_context, m_p_impl->m_base_url, request, m_p_impl->m_connect_timeout_ms,
        m_p_impl->m_request_timeout_ms, m_p_impl->m_max_response_bytes,
        [&result](PilotHttpResult completed) { result = std::move(completed); });
    m_p_impl->m_p_active_session->startRequest();
    lock.unlock();
    m_p_impl->m_io_context.run();
    lock.lock();
    m_p_impl->m_p_active_session.reset();
    return result;
}

void PilotHttpTransport::cancelRequest() noexcept {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_p_active_session != nullptr) {
        m_p_impl->m_p_active_session->cancelRequest();
    }
}

}  // namespace nodus_vision
