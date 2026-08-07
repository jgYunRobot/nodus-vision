/**
 * @file test_pilot_http_transport.cpp
 * @brief bounded Pilot public HTTP transport를 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "pilot_http_transport.hpp"

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class FakePilotServer {
   public:
    FakePilotServer(std::string response, std::chrono::milliseconds response_delay)
        : m_response(std::move(response)),
          m_response_delay(response_delay),
          m_acceptor(m_io_context) {
        m_acceptor.open(tcp::v4());
        m_acceptor.set_option(asio::socket_base::reuse_address(true));
        m_acceptor.bind({asio::ip::address_v4::loopback(), 0});
        m_acceptor.listen();
        m_port = m_acceptor.local_endpoint().port();
        m_worker = std::thread([this]() {
            tcp::socket socket(m_io_context);
            beast::error_code error;
            m_acceptor.accept(socket, error);
            if (error) {
                return;
            }
            beast::flat_buffer buffer;
            http::request<http::string_body> request;
            http::read(socket, buffer, request, error);
            if (error) {
                return;
            }
            if (m_response_delay.count() < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                return;
            }
            std::this_thread::sleep_for(m_response_delay);
            asio::write(socket, asio::buffer(m_response), error);
        });
    }

    ~FakePilotServer() {
        beast::error_code ignored;
        m_acceptor.cancel(ignored);
        m_acceptor.close(ignored);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    std::string getBaseUrl() const { return "http://127.0.0.1:" + std::to_string(m_port); }

   private:
    std::string m_response;
    std::chrono::milliseconds m_response_delay;
    asio::io_context m_io_context;
    tcp::acceptor m_acceptor;
    unsigned short m_port{0};
    std::thread m_worker;
};

PilotHttpRequest makeRequest() { return {"POST", "/api/v1/components/register", "{}"}; }

}  // namespace

TEST(PilotHttpTransport, ReturnsBoundedJsonResponse) {
    FakePilotServer server(
        "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: "
        "2\r\nConnection: close\r\n\r\n{}",
        std::chrono::milliseconds(0));
    PilotHttpTransport transport(server.getBaseUrl(), 100, 100, 64);
    const PilotHttpResult result = transport.executeRequest(makeRequest());
    EXPECT_TRUE(result.hasResponse());
    EXPECT_EQ(result.status, 201);
    EXPECT_EQ(result.body, "{}");
}

TEST(PilotHttpTransport, PreservesJsonErrorStatusAndRejectsMalformedResponses) {
    FakePilotServer unavailable(
        "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: "
        "2\r\nConnection: close\r\n\r\n{}",
        std::chrono::milliseconds(0));
    PilotHttpTransport unavailable_transport(unavailable.getBaseUrl(), 100, 100, 64);
    EXPECT_EQ(unavailable_transport.executeRequest(makeRequest()).status, 503);

    FakePilotServer non_json(
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: "
        "close\r\n\r\nok",
        std::chrono::milliseconds(0));
    PilotHttpTransport non_json_transport(non_json.getBaseUrl(), 100, 100, 64);
    EXPECT_FALSE(non_json_transport.executeRequest(makeRequest()).hasResponse());

    FakePilotServer oversized(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 3\r\nConnection: "
        "close\r\n\r\n{}x",
        std::chrono::milliseconds(0));
    PilotHttpTransport oversized_transport(oversized.getBaseUrl(), 100, 100, 2);
    EXPECT_FALSE(oversized_transport.executeRequest(makeRequest()).hasResponse());

    FakePilotServer malformed(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
        "3\r\nConnection: close\r\n\r\n{}",
        std::chrono::milliseconds(0));
    PilotHttpTransport malformed_transport(malformed.getBaseUrl(), 100, 100, 64);
    EXPECT_FALSE(malformed_transport.executeRequest(makeRequest()).hasResponse());
}

TEST(PilotHttpTransport, TimesOutAndCancelsStalledRequest) {
    FakePilotServer stalled("", std::chrono::milliseconds(-1));
    PilotHttpTransport timeout_transport(stalled.getBaseUrl(), 100, 20, 64);
    EXPECT_EQ(timeout_transport.executeRequest(makeRequest()).error, "request deadline exceeded");

    FakePilotServer cancellable("", std::chrono::milliseconds(-1));
    PilotHttpTransport cancellable_transport(cancellable.getBaseUrl(), 100, 1000, 64);
    std::future<PilotHttpResult> result = std::async(
        std::launch::async,
        [&cancellable_transport]() { return cancellable_transport.executeRequest(makeRequest()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cancellable_transport.cancelRequest();
    ASSERT_EQ(result.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    EXPECT_EQ(result.get().error, "request cancelled");
}

}  // namespace nodus_vision
