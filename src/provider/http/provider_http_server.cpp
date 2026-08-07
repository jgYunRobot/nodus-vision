/**
 * @file provider_http_server.cpp
 * @brief bounded HTTP와 latest-only MJPEG session을 구현한다.
 */

#include "provider_http_server.hpp"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

constexpr const char* MJPEG_BOUNDARY = "nodus_frame";

ProviderHttpResponse makeErrorResponse(int status, const std::string& code,
                                       const std::string& message, bool retryable) {
    std::ostringstream body;
    body << "{\"schema_version\":1,\"error\":{\"code\":\"" << code << "\",\"message\":\"" << message
         << "\",\"retryable\":" << (retryable ? "true" : "false") << "}}";
    return {status, "application/json", body.str(), {}};
}

bool isNewerIdentity(const FrameIdentity& candidate, const FrameIdentity& current) {
    return candidate.capture_generation > current.capture_generation ||
           (candidate.capture_generation == current.capture_generation &&
            candidate.frame_number > current.frame_number);
}

std::string buildMjpegPart(const ProviderStreamFrame& frame) {
    std::ostringstream header;
    header << "--" << MJPEG_BOUNDARY << "\r\n";
    header << "Content-Type: image/jpeg\r\n";
    header << "Content-Length: " << frame.jpeg_data->size() << "\r\n";
    for (const std::pair<std::string, std::string>& item : frame.headers) {
        header << item.first << ": " << item.second << "\r\n";
    }
    header << "\r\n";

    std::string part = header.str();
    part.reserve(part.size() + frame.jpeg_data->size() + 2U);
    for (const std::uint8_t byte : *frame.jpeg_data) {
        part.push_back(static_cast<char>(byte));
    }
    part.append("\r\n");
    return part;
}

class HttpSession final : public std::enable_shared_from_this<HttpSession> {
   public:
    using StreamAdmission = std::function<bool(ProviderStreamKind)>;
    using ClosedHandler = std::function<void(bool)>;

    HttpSession(tcp::socket socket, int timeout_ms, int header_bytes, int body_bytes,
                ProviderHttpRoutes routes, StreamAdmission stream_admission,
                ClosedHandler closed_handler)
        : m_stream(std::move(socket)),
          m_idle_timer(m_stream.get_executor()),
          m_timeout_ms(timeout_ms),
          m_header_bytes(header_bytes),
          m_body_bytes(body_bytes),
          m_routes(std::move(routes)),
          m_stream_admission(std::move(stream_admission)),
          m_closed_handler(std::move(closed_handler)) {}

    void runSession() {
        m_stream.expires_after(std::chrono::milliseconds(m_timeout_ms));
        m_parser.header_limit(static_cast<std::uint32_t>(m_header_bytes));
        m_parser.body_limit(static_cast<std::uint64_t>(m_body_bytes));
        http::async_read(m_stream, m_buffer, m_parser,
                         beast::bind_front_handler(&HttpSession::handleRead, shared_from_this()));
    }

    void notifyStreamFrame(ProviderStreamKind kind) {
        if (m_finished || !m_streaming || kind != m_stream_kind) {
            return;
        }
        if (m_write_in_progress) {
            m_refresh_pending = true;
            return;
        }
        beast::error_code ignored;
        m_idle_timer.cancel(ignored);
        writeLatestStreamFrame();
    }

    void cancelSession() noexcept {
        if (m_finished) {
            return;
        }
        m_finished = true;
        beast::error_code ignored;
        m_idle_timer.cancel(ignored);
        m_stream.socket().cancel(ignored);
        m_stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        m_stream.socket().close(ignored);
        if (m_closed_handler) {
            ClosedHandler closed_handler = std::move(m_closed_handler);
            closed_handler(m_streaming);
        }
    }

   private:
    void handleRead(beast::error_code error, std::size_t) {
        if (error) {
            cancelSession();
            return;
        }

        const http::request<http::string_body>& request = m_parser.get();
        const std::string target(request.target());
        if (target == "/stream/color.mjpg" || target == "/stream/depth.mjpg") {
            if (request.method() != http::verb::get) {
                sendResponse(
                    makeErrorResponse(405, "method_not_allowed", "Method is not allowed.", false));
                return;
            }
            startStream(target == "/stream/color.mjpg" ? ProviderStreamKind::e_COLOR
                                                       : ProviderStreamKind::e_DEPTH,
                        request.version());
            return;
        }

        const bool is_roi_route = target == "/query/roi_depth";
        const bool is_pixel_route = target == "/query/pixel_to_point";
        const bool is_recording_start_route = target == "/recordings/start";
        const bool is_recording_stop_route = target == "/recordings/stop";
        const bool is_recording_post_route = is_recording_start_route || is_recording_stop_route;
        if ((is_roi_route || is_pixel_route || is_recording_post_route) &&
            request.method() != http::verb::post) {
            sendResponse(
                makeErrorResponse(405, "method_not_allowed", "Method is not allowed.", false));
            return;
        }
        if (!is_roi_route && !is_pixel_route && !is_recording_post_route &&
            request.method() != http::verb::get) {
            sendResponse(
                makeErrorResponse(405, "method_not_allowed", "Method is not allowed.", false));
            return;
        }

        try {
            if (is_roi_route && m_routes.post_roi_depth) {
                sendResponse(m_routes.post_roi_depth(request.body()));
            } else if (is_pixel_route && m_routes.post_pixel_point) {
                sendResponse(m_routes.post_pixel_point(request.body()));
            } else if (is_recording_start_route && m_routes.post_recording_start) {
                sendResponse(m_routes.post_recording_start(request.body()));
            } else if (is_recording_stop_route && m_routes.post_recording_stop) {
                sendResponse(m_routes.post_recording_stop(request.body()));
            } else if (target == "/recordings/current" && m_routes.get_recording_current) {
                sendResponse(m_routes.get_recording_current());
            } else if (target == "/health" && m_routes.get_health) {
                sendResponse(m_routes.get_health());
            } else if (target == "/metadata" && m_routes.get_metadata) {
                sendResponse(m_routes.get_metadata());
            } else if (target == "/snapshot/color" && m_routes.get_color_snapshot) {
                sendResponse(m_routes.get_color_snapshot());
            } else if (target == "/snapshot/depth" && m_routes.get_depth_snapshot) {
                sendResponse(m_routes.get_depth_snapshot());
            } else if (target == "/snapshot/pointcloud.bin" && m_routes.get_pointcloud_snapshot) {
                sendResponse(m_routes.get_pointcloud_snapshot());
            } else {
                sendResponse(
                    makeErrorResponse(404, "route_not_found", "Route is not available.", false));
            }
        } catch (const std::exception&) {
            sendResponse(
                makeErrorResponse(500, "provider_failure", "Provider request failed.", true));
        }
    }

    void sendResponse(ProviderHttpResponse payload) {
        const http::request<http::string_body>& request = m_parser.get();
        auto p_response = std::make_shared<http::response<http::string_body>>(
            static_cast<http::status>(payload.status), request.version());
        p_response->set(http::field::content_type, payload.content_type);
        p_response->set(http::field::cache_control, "no-store");
        for (const std::pair<std::string, std::string>& header : payload.headers) {
            p_response->set(header.first, header.second);
        }
        p_response->keep_alive(false);
        p_response->body() = std::move(payload.body);
        p_response->prepare_payload();
        m_stream.expires_after(std::chrono::milliseconds(m_timeout_ms));
        http::async_write(m_stream, *p_response,
                          [self = shared_from_this(), p_response](beast::error_code, std::size_t) {
                              self->cancelSession();
                          });
    }

    void startStream(ProviderStreamKind kind, unsigned int version) {
        std::function<std::shared_ptr<const ProviderStreamFrame>()>& acquire_frame =
            kind == ProviderStreamKind::e_COLOR ? m_routes.get_color_stream_frame
                                                : m_routes.get_depth_stream_frame;
        if (!acquire_frame) {
            sendResponse(makeErrorResponse(409, "stream_disabled", "Stream is disabled.", false));
            return;
        }

        std::shared_ptr<const ProviderStreamFrame> initial_frame;
        try {
            initial_frame = acquire_frame();
        } catch (const std::exception&) {
            sendResponse(
                makeErrorResponse(500, "provider_failure", "Provider stream failed.", true));
            return;
        }
        if (initial_frame == nullptr || initial_frame->jpeg_data == nullptr ||
            initial_frame->jpeg_data->empty()) {
            sendResponse(
                makeErrorResponse(503, "no_fresh_frame", "No captured frame is available.", true));
            return;
        }
        if (!m_stream_admission(kind)) {
            sendResponse(makeErrorResponse(429, "stream_limit_reached",
                                           "Stream client limit is reached.", true));
            return;
        }

        m_streaming = true;
        m_stream_kind = kind;
        m_initial_stream_frame = std::move(initial_frame);
        m_p_stream_response =
            std::make_unique<http::response<http::empty_body>>(http::status::ok, version);
        m_p_stream_response->set(
            http::field::content_type,
            std::string("multipart/x-mixed-replace; boundary=") + MJPEG_BOUNDARY);
        m_p_stream_response->set(http::field::cache_control, "no-store");
        m_p_stream_response->set(http::field::connection, "close");
        m_p_stream_serializer =
            std::make_unique<http::response_serializer<http::empty_body>>(*m_p_stream_response);
        m_write_in_progress = true;
        m_stream.expires_after(std::chrono::milliseconds(m_timeout_ms));
        http::async_write_header(
            m_stream, *m_p_stream_serializer,
            beast::bind_front_handler(&HttpSession::handleStreamHeaderWritten, shared_from_this()));
    }

    void handleStreamHeaderWritten(beast::error_code error, std::size_t) {
        m_write_in_progress = false;
        if (error || m_finished) {
            cancelSession();
            return;
        }
        writeLatestStreamFrame();
    }

    void writeLatestStreamFrame() {
        if (m_finished || m_write_in_progress) {
            return;
        }

        std::shared_ptr<const ProviderStreamFrame> frame = std::move(m_initial_stream_frame);
        if (frame == nullptr) {
            try {
                frame = m_stream_kind == ProviderStreamKind::e_COLOR
                            ? m_routes.get_color_stream_frame()
                            : m_routes.get_depth_stream_frame();
            } catch (const std::exception&) {
                cancelSession();
                return;
            }
        }
        if (frame == nullptr || frame->jpeg_data == nullptr || frame->jpeg_data->empty() ||
            (m_has_sent_frame && !isNewerIdentity(frame->identity, m_last_sent_identity))) {
            waitForStreamFrame();
            return;
        }

        m_pending_stream_frame = std::move(frame);
        m_pending_part = std::make_shared<std::string>(buildMjpegPart(*m_pending_stream_frame));
        m_write_in_progress = true;
        m_refresh_pending = false;
        m_stream.expires_after(std::chrono::milliseconds(m_timeout_ms));
        asio::async_write(
            m_stream, asio::buffer(*m_pending_part),
            beast::bind_front_handler(&HttpSession::handleStreamPartWritten, shared_from_this()));
    }

    void handleStreamPartWritten(beast::error_code error, std::size_t) {
        m_write_in_progress = false;
        if (error || m_finished) {
            cancelSession();
            return;
        }
        m_last_sent_identity = m_pending_stream_frame->identity;
        m_has_sent_frame = true;
        m_pending_stream_frame.reset();
        m_pending_part.reset();
        if (m_refresh_pending) {
            m_refresh_pending = false;
            writeLatestStreamFrame();
        } else {
            waitForStreamFrame();
        }
    }

    void waitForStreamFrame() {
        if (m_finished) {
            return;
        }
        m_stream.expires_never();
        m_idle_timer.expires_after(std::chrono::milliseconds(m_timeout_ms));
        m_idle_timer.async_wait([self = shared_from_this()](beast::error_code error) {
            if (!error) {
                self->cancelSession();
            }
        });
    }

    beast::tcp_stream m_stream;
    asio::steady_timer m_idle_timer;
    beast::flat_buffer m_buffer;
    http::request_parser<http::string_body> m_parser;
    int m_timeout_ms;
    int m_header_bytes;
    int m_body_bytes;
    ProviderHttpRoutes m_routes;
    StreamAdmission m_stream_admission;
    ClosedHandler m_closed_handler;
    ProviderStreamKind m_stream_kind{ProviderStreamKind::e_COLOR};
    bool m_streaming{false};
    bool m_finished{false};
    bool m_write_in_progress{false};
    bool m_refresh_pending{false};
    bool m_has_sent_frame{false};
    FrameIdentity m_last_sent_identity;
    std::shared_ptr<const ProviderStreamFrame> m_initial_stream_frame;
    std::shared_ptr<const ProviderStreamFrame> m_pending_stream_frame;
    std::shared_ptr<std::string> m_pending_part;
    std::unique_ptr<http::response<http::empty_body>> m_p_stream_response;
    std::unique_ptr<http::response_serializer<http::empty_body>> m_p_stream_serializer;
};

}  // namespace

class ProviderHttpServer::Impl {
   public:
    Impl(std::string bind_host, int port, int max_connections, int max_stream_clients,
         int timeout_ms, int header_bytes, int body_bytes, ProviderHttpRoutes routes)
        : m_bind_host(std::move(bind_host)),
          m_port(port),
          m_max_connections(max_connections),
          m_max_stream_clients(max_stream_clients),
          m_timeout_ms(timeout_ms),
          m_header_bytes(header_bytes),
          m_body_bytes(body_bytes),
          m_routes(std::move(routes)),
          m_acceptor(m_io_context) {}

    void acceptNext() {
        m_acceptor.async_accept([this](beast::error_code error, tcp::socket socket) {
            if (!error) {
                if (m_active_connections.load() >= m_max_connections) {
                    beast::error_code ignored;
                    socket.close(ignored);
                } else {
                    const std::uint64_t session_id = ++m_next_session_id;
                    ++m_active_connections;
                    std::shared_ptr<HttpSession> session = std::make_shared<HttpSession>(
                        std::move(socket), m_timeout_ms, m_header_bytes, m_body_bytes, m_routes,
                        [this](ProviderStreamKind) { return admitStream(); },
                        [this, session_id](bool was_streaming) {
                            closeSession(session_id, was_streaming);
                        });
                    m_sessions.emplace(session_id, session);
                    session->runSession();
                }
            }
            if (m_acceptor.is_open()) {
                acceptNext();
            }
        });
    }

    bool admitStream() {
        if (m_active_stream_clients.load() >= m_max_stream_clients) {
            return false;
        }
        ++m_active_stream_clients;
        return true;
    }

    void closeSession(std::uint64_t session_id, bool was_streaming) {
        m_sessions.erase(session_id);
        if (m_active_connections.load() > 0) {
            --m_active_connections;
        }
        if (was_streaming && m_active_stream_clients.load() > 0) {
            --m_active_stream_clients;
        }
    }

    void notifySessions(ProviderStreamKind kind) {
        for (auto iterator = m_sessions.begin(); iterator != m_sessions.end();) {
            const std::shared_ptr<HttpSession> session = iterator->second.lock();
            if (session == nullptr) {
                iterator = m_sessions.erase(iterator);
            } else {
                session->notifyStreamFrame(kind);
                ++iterator;
            }
        }
    }

    void stopOnIoThread() {
        beast::error_code ignored;
        m_acceptor.cancel(ignored);
        m_acceptor.close(ignored);
        std::vector<std::shared_ptr<HttpSession>> sessions;
        sessions.reserve(m_sessions.size());
        for (const auto& item : m_sessions) {
            const std::shared_ptr<HttpSession> session = item.second.lock();
            if (session != nullptr) {
                sessions.push_back(session);
            }
        }
        for (const std::shared_ptr<HttpSession>& session : sessions) {
            session->cancelSession();
        }
        m_sessions.clear();
    }

    std::string m_bind_host;
    int m_port;
    int m_max_connections;
    int m_max_stream_clients;
    int m_timeout_ms;
    int m_header_bytes;
    int m_body_bytes;
    ProviderHttpRoutes m_routes;
    asio::io_context m_io_context{1};
    tcp::acceptor m_acceptor;
    std::thread m_worker;
    std::shared_future<void> m_worker_exit;
    std::unordered_map<std::uint64_t, std::weak_ptr<HttpSession>> m_sessions;
    std::atomic<int> m_active_connections{0};
    std::atomic<int> m_active_stream_clients{0};
    std::uint64_t m_next_session_id{0};
    mutable std::mutex m_mutex;
    int m_bound_port{0};
    bool m_started{false};
};

ProviderHttpServer::ProviderHttpServer(std::string bind_host, int port, int max_connections,
                                       int max_stream_clients, int request_timeout_ms,
                                       int max_header_bytes, int max_body_bytes,
                                       ProviderHttpRoutes routes)
    : m_p_impl(std::make_unique<Impl>(std::move(bind_host), port, max_connections,
                                      max_stream_clients, request_timeout_ms, max_header_bytes,
                                      max_body_bytes, std::move(routes))) {}

ProviderHttpServer::~ProviderHttpServer() { stopServer(); }

void ProviderHttpServer::startServer() {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_started) {
        return;
    }

    m_p_impl->m_io_context.restart();
    beast::error_code error;
    const asio::ip::address address = asio::ip::make_address(m_p_impl->m_bind_host, error);
    if (error) {
        throw std::invalid_argument("Provider bind_host is not a valid IP address.");
    }
    const tcp::endpoint endpoint(address, static_cast<unsigned short>(m_p_impl->m_port));
    m_p_impl->m_acceptor.open(endpoint.protocol(), error);
    if (error) {
        throw std::runtime_error("Provider acceptor open failed.");
    }
    m_p_impl->m_acceptor.set_option(asio::socket_base::reuse_address(true), error);
    if (error) {
        throw std::runtime_error("Provider acceptor option failed.");
    }
    m_p_impl->m_acceptor.bind(endpoint, error);
    if (error) {
        throw std::runtime_error("Provider bind failed.");
    }
    m_p_impl->m_acceptor.listen(asio::socket_base::max_listen_connections, error);
    if (error) {
        throw std::runtime_error("Provider listen failed.");
    }

    m_p_impl->m_bound_port = m_p_impl->m_acceptor.local_endpoint().port();
    m_p_impl->m_active_connections.store(0);
    m_p_impl->m_active_stream_clients.store(0);
    m_p_impl->m_started = true;
    m_p_impl->acceptNext();
    auto p_worker_exit = std::make_shared<std::promise<void>>();
    m_p_impl->m_worker_exit = p_worker_exit->get_future().share();
    m_p_impl->m_worker = std::thread([this, p_worker_exit]() {
        m_p_impl->m_io_context.run();
        p_worker_exit->set_value();
    });
}

void ProviderHttpServer::stopServer() noexcept {
    bool stop_posted = false;
    {
        std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
        if (!m_p_impl->m_started) {
            return;
        }
        m_p_impl->m_started = false;
        try {
            asio::post(m_p_impl->m_io_context, [this]() { m_p_impl->stopOnIoThread(); });
            stop_posted = true;
        } catch (const std::exception&) {
            m_p_impl->m_io_context.stop();
        }
    }

    const std::chrono::milliseconds shutdown_timeout(m_p_impl->m_timeout_ms);
    if (stop_posted && m_p_impl->m_worker_exit.valid() &&
        m_p_impl->m_worker_exit.wait_for(shutdown_timeout) != std::future_status::ready) {
        m_p_impl->m_io_context.stop();
    }
    if (m_p_impl->m_worker.joinable()) {
        m_p_impl->m_worker.join();
    }

    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_io_context.restart();
    while (m_p_impl->m_io_context.poll_one() != 0U) {
    }
    m_p_impl->m_io_context.restart();
    m_p_impl->m_sessions.clear();
    m_p_impl->m_active_connections.store(0);
    m_p_impl->m_active_stream_clients.store(0);
    m_p_impl->m_bound_port = 0;
}

void ProviderHttpServer::notifyStreamFrame(ProviderStreamKind kind) {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_started) {
        return;
    }
    asio::post(m_p_impl->m_io_context, [this, kind]() { m_p_impl->notifySessions(kind); });
}

int ProviderHttpServer::getBoundPort() const {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    return m_p_impl->m_bound_port;
}

int ProviderHttpServer::getActiveConnectionCount() const {
    return m_p_impl->m_active_connections.load();
}

int ProviderHttpServer::getActiveStreamClientCount() const {
    return m_p_impl->m_active_stream_clients.load();
}

}  // namespace nodus_vision
