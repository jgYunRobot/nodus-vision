/**
 * @file provider_http_server.cpp
 * @brief bounded asynchronous health/metadata HTTP server를 구현한다.
 */

#include "provider_http_server.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class HttpSession final : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, int timeout_ms, int header_bytes, int body_bytes, ProviderHttpRoutes routes, std::function<void()> closed)
        : m_stream(std::move(socket)), m_timeout_ms(timeout_ms), m_header_bytes(header_bytes),
          m_body_bytes(body_bytes), m_routes(std::move(routes)), m_closed(std::move(closed)) {}

    void runSession()
    {
        m_stream.expires_after(std::chrono::milliseconds(m_timeout_ms));
        m_parser.header_limit(static_cast<std::uint32_t>(m_header_bytes));
        m_parser.body_limit(static_cast<std::uint64_t>(m_body_bytes));
        http::async_read(m_stream, m_buffer, m_parser, beast::bind_front_handler(&HttpSession::handleRead, shared_from_this()));
    }

    void closeSession() noexcept
    {
        beast::error_code error;
        m_stream.socket().shutdown(tcp::socket::shutdown_both, error);
        m_stream.socket().close(error);
    }

private:
    void handleRead(beast::error_code error, std::size_t)
    {
        if (error) { finishSession(); return; }
        const http::request<http::string_body>& request = m_parser.get();
        http::response<http::string_body> response{http::status::not_found, request.version()};
        response.set(http::field::content_type, "application/json");
        response.set(http::field::cache_control, "no-store");
        response.keep_alive(false);
        if (request.method() != http::verb::get) {
            response.result(http::status::method_not_allowed);
            response.body() = "{\"schema_version\":1,\"error\":{\"code\":\"method_not_allowed\",\"message\":\"Method is not allowed.\",\"retryable\":false}}";
        } else if (request.target() == "/health") {
            const ProviderHttpResponse payload = m_routes.get_health();
            response.result(static_cast<http::status>(payload.status));
            response.set(http::field::content_type, payload.content_type);
            response.body() = payload.body;
            for (const auto& header : payload.headers) { response.set(header.first, header.second); }
        } else if (request.target() == "/metadata") {
            const ProviderHttpResponse payload = m_routes.get_metadata();
            response.result(static_cast<http::status>(payload.status));
            response.set(http::field::content_type, payload.content_type);
            response.body() = payload.body;
            for (const auto& header : payload.headers) { response.set(header.first, header.second); }
        } else if (request.target() == "/snapshot/color") {
            const ProviderHttpResponse payload = m_routes.get_color_snapshot();
            response.result(static_cast<http::status>(payload.status));
            response.set(http::field::content_type, payload.content_type);
            response.body() = payload.body;
            for (const auto& header : payload.headers) { response.set(header.first, header.second); }
        } else if (request.target() == "/snapshot/pointcloud.bin") {
            const ProviderHttpResponse payload = m_routes.get_pointcloud_snapshot();
            response.result(static_cast<http::status>(payload.status));
            response.set(http::field::content_type, payload.content_type);
            response.body() = payload.body;
            for (const auto& header : payload.headers) { response.set(header.first, header.second); }
        } else {
            response.body() = "{\"schema_version\":1,\"error\":{\"code\":\"route_not_found\",\"message\":\"Route is not available.\",\"retryable\":false}}";
        }
        response.prepare_payload();
        const std::shared_ptr<http::response<http::string_body>> owner = std::make_shared<http::response<http::string_body>>(std::move(response));
        http::async_write(m_stream, *owner, [self = shared_from_this(), owner](beast::error_code, std::size_t) { self->finishSession(); });
    }

    void finishSession() noexcept { closeSession(); m_closed(); }
    beast::tcp_stream m_stream;
    beast::flat_buffer m_buffer;
    http::request_parser<http::string_body> m_parser;
    int m_timeout_ms;
    int m_header_bytes;
    int m_body_bytes;
    ProviderHttpRoutes m_routes;
    std::function<void()> m_closed;
};

} // namespace

class ProviderHttpServer::Impl {
public:
    Impl(std::string bind_host, int port, int max_connections, int timeout_ms, int header_bytes, int body_bytes, ProviderHttpRoutes routes)
        : m_bind_host(std::move(bind_host)), m_port(port), m_max_connections(max_connections),
          m_timeout_ms(timeout_ms), m_header_bytes(header_bytes), m_body_bytes(body_bytes), m_routes(std::move(routes)),
          m_acceptor(m_io_context) {}

    void acceptNext()
    {
        m_acceptor.async_accept([this](beast::error_code error, tcp::socket socket) {
            if (!error) {
                if (m_active_connections.fetch_add(1) >= m_max_connections) {
                    --m_active_connections;
                    beast::error_code close_error;
                    socket.close(close_error);
                } else {
                    std::make_shared<HttpSession>(std::move(socket), m_timeout_ms, m_header_bytes, m_body_bytes, m_routes, [this]() { --m_active_connections; })->runSession();
                }
            }
            if (m_acceptor.is_open()) { acceptNext(); }
        });
    }

    std::string m_bind_host;
    int m_port;
    int m_max_connections;
    int m_timeout_ms;
    int m_header_bytes;
    int m_body_bytes;
    ProviderHttpRoutes m_routes;
    asio::io_context m_io_context{1};
    tcp::acceptor m_acceptor;
    std::thread m_worker;
    std::atomic<int> m_active_connections{0};
    mutable std::mutex m_mutex;
    int m_bound_port{0};
    bool m_started{false};
};

ProviderHttpServer::ProviderHttpServer(std::string bind_host, int port, int max_connections, int request_timeout_ms, int max_header_bytes, int max_body_bytes, ProviderHttpRoutes routes)
    : m_p_impl(std::make_unique<Impl>(std::move(bind_host), port, max_connections, request_timeout_ms, max_header_bytes, max_body_bytes, std::move(routes))) {}
ProviderHttpServer::~ProviderHttpServer() { stopServer(); }

void ProviderHttpServer::startServer()
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_started) { return; }
    beast::error_code error;
    const asio::ip::address address = asio::ip::make_address(m_p_impl->m_bind_host, error);
    if (error) { throw std::invalid_argument("Provider bind_host is not a valid IP address."); }
    m_p_impl->m_acceptor.open(tcp::v4(), error);
    if (error) { throw std::runtime_error("Provider acceptor open failed."); }
    m_p_impl->m_acceptor.set_option(asio::socket_base::reuse_address(true), error);
    m_p_impl->m_acceptor.bind({address, static_cast<unsigned short>(m_p_impl->m_port)}, error);
    if (error) { throw std::runtime_error("Provider bind failed."); }
    m_p_impl->m_acceptor.listen(asio::socket_base::max_listen_connections, error);
    if (error) { throw std::runtime_error("Provider listen failed."); }
    m_p_impl->m_bound_port = m_p_impl->m_acceptor.local_endpoint().port();
    m_p_impl->m_started = true;
    m_p_impl->acceptNext();
    m_p_impl->m_worker = std::thread([this]() { m_p_impl->m_io_context.run(); });
}

void ProviderHttpServer::stopServer() noexcept
{
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_started) { return; }
    beast::error_code error;
    m_p_impl->m_acceptor.close(error);
    m_p_impl->m_io_context.stop();
    if (m_p_impl->m_worker.joinable()) { m_p_impl->m_worker.join(); }
    m_p_impl->m_started = false;
}
int ProviderHttpServer::getBoundPort() const { std::lock_guard<std::mutex> lock(m_p_impl->m_mutex); return m_p_impl->m_bound_port; }
int ProviderHttpServer::getActiveConnectionCount() const { return m_p_impl->m_active_connections.load(); }

} // namespace nodus_vision
