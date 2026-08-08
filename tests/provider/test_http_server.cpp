/**
 * @file test_http_server.cpp
 * @brief bounded HTTP와 MJPEG latest-only session을 검증한다.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "provider_http_server.hpp"

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

http::response<http::string_body> requestResponse(int port, http::verb method,
                                                  const std::string& target,
                                                  const std::string& body = {},
                                                  const std::string& origin = {}) {
    asio::io_context context;
    tcp::socket socket(context);
    socket.connect({asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "localhost");
    if (!origin.empty()) {
        request.set("Origin", origin);
    }
    if (!body.empty()) {
        request.set(http::field::content_type, "application/json");
        request.body() = body;
        request.prepare_payload();
    }
    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response;
}

std::shared_ptr<const ProviderStreamFrame> makeStreamFrame(std::uint64_t frame_number,
                                                           std::size_t payload_size = 4U) {
    auto p_frame = std::make_shared<ProviderStreamFrame>();
    p_frame->identity.capture_generation = 1U;
    p_frame->identity.frame_number = frame_number;
    p_frame->jpeg_data = std::make_shared<const std::vector<std::uint8_t>>(
        payload_size, static_cast<std::uint8_t>(frame_number));
    p_frame->headers = {
        {"X-Nodus-Capture-Generation", "1"},
        {"X-Nodus-Frame-Number", std::to_string(frame_number)},
    };
    return p_frame;
}

ProviderHttpRoutes makeRoutes(std::shared_ptr<const ProviderStreamFrame>* p_current_stream_frame) {
    ProviderHttpRoutes routes;
    routes.get_health = []() {
        return ProviderHttpResponse{200, "application/json",
                                    "{\"schema_version\":1,\"state\":\"degraded\"}"};
    };
    routes.get_metadata = []() {
        return ProviderHttpResponse{
            200, "application/json",
            "{\"schema_version\":1,\"endpoints\":[\"/health\",\"/metadata\"]}"};
    };
    routes.get_color_snapshot = []() {
        return ProviderHttpResponse{503, "application/json", "{}"};
    };
    routes.get_depth_snapshot = routes.get_color_snapshot;
    routes.get_pointcloud_snapshot = routes.get_color_snapshot;
    routes.post_roi_depth = [](const std::string&) {
        return ProviderHttpResponse{
            200, "application/json", "{\"valid\":true}", {{"X-Nodus-Frame-Number", "9"}}};
    };
    routes.post_pixel_point = routes.post_roi_depth;
    routes.post_recording_start = [](const std::string& body) {
        return ProviderHttpResponse{201, "application/json", body};
    };
    routes.post_recording_stop = [](const std::string& body) {
        return ProviderHttpResponse{202, "application/json", body};
    };
    routes.get_recording_current = []() {
        return ProviderHttpResponse{200, "application/json", "{\"state\":\"idle\"}"};
    };
    routes.get_color_stream_frame = [p_current_stream_frame]() {
        return std::atomic_load(p_current_stream_frame);
    };
    routes.get_depth_stream_frame = routes.get_color_stream_frame;
    return routes;
}

class StreamClient {
   public:
    explicit StreamClient(int port) : m_socket(m_context) {
        m_socket.connect({asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
    }

    std::string openStream(const std::string& target, const std::string& origin = {}) {
        http::request<http::empty_body> request{http::verb::get, target, 11};
        request.set(http::field::host, "localhost");
        if (!origin.empty()) {
            request.set("Origin", origin);
        }
        http::write(m_socket, request);
        return readThrough("\r\n\r\n");
    }

    std::string readPart() {
        const std::string header = readThrough("\r\n\r\n");
        const std::string length_prefix = "Content-Length: ";
        const std::size_t length_position = header.find(length_prefix);
        if (length_position == std::string::npos) {
            throw std::runtime_error("MJPEG part has no content length.");
        }
        const std::size_t value_start = length_position + length_prefix.size();
        const std::size_t value_end = header.find("\r\n", value_start);
        const std::size_t body_size = static_cast<std::size_t>(
            std::stoul(header.substr(value_start, value_end - value_start)));
        const std::string body = readBytes(body_size);
        if (readBytes(2U) != "\r\n") {
            throw std::runtime_error("MJPEG part terminator is invalid.");
        }
        return header + body;
    }

   private:
    std::string readThrough(const std::string& delimiter) {
        while (m_received.find(delimiter) == std::string::npos) {
            readMore();
        }
        const std::size_t end = m_received.find(delimiter) + delimiter.size();
        std::string result = m_received.substr(0U, end);
        m_received.erase(0U, end);
        return result;
    }

    std::string readBytes(std::size_t size) {
        while (m_received.size() < size) {
            readMore();
        }
        std::string result = m_received.substr(0U, size);
        m_received.erase(0U, size);
        return result;
    }

    void readMore() {
        std::array<char, 4096> buffer{};
        const std::size_t size = m_socket.read_some(asio::buffer(buffer));
        m_received.append(buffer.data(), size);
    }

    asio::io_context m_context;
    tcp::socket m_socket;
    std::string m_received;
};

bool waitForStreamCount(ProviderHttpServer& server, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (server.getActiveStreamClientCount() == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return server.getActiveStreamClientCount() == expected;
}

}  // namespace

TEST(ProviderHttpServer, PreservesRoutePayloadHeadersAndNoStoreResponses) {
    std::shared_ptr<const ProviderStreamFrame> current_stream_frame = makeStreamFrame(1U);
    ProviderHttpServer server("127.0.0.1", 0, 4, 2, 1000, 8192, 4096, std::vector<std::string>{},
                              makeRoutes(&current_stream_frame));
    server.startServer();

    const http::response<http::string_body> health =
        requestResponse(server.getBoundPort(), http::verb::get, "/health");
    EXPECT_EQ(health.result(), http::status::ok);
    EXPECT_EQ(health[http::field::cache_control], "no-store");
    EXPECT_NE(health.body().find("degraded"), std::string::npos);

    const http::response<http::string_body> query =
        requestResponse(server.getBoundPort(), http::verb::post, "/query/roi_depth",
                        "{\"x\":0,\"y\":0,\"width\":1,\"height\":1}");
    EXPECT_EQ(query.result(), http::status::ok);
    EXPECT_EQ(query["X-Nodus-Frame-Number"], "9");

    const http::response<http::string_body> snapshot =
        requestResponse(server.getBoundPort(), http::verb::get, "/snapshot/color");
    EXPECT_EQ(snapshot.result(), http::status::service_unavailable);
    EXPECT_EQ(snapshot[http::field::cache_control], "no-store");

    const http::response<http::string_body> recording_start =
        requestResponse(server.getBoundPort(), http::verb::post, "/recordings/start",
                        "{\"request_id\":\"start-001\"}");
    EXPECT_EQ(recording_start.result(), http::status::created);
    const http::response<http::string_body> recording_current =
        requestResponse(server.getBoundPort(), http::verb::get, "/recordings/current");
    EXPECT_EQ(recording_current.result(), http::status::ok);
    EXPECT_NE(recording_current.body().find("idle"), std::string::npos);
    server.stopServer();
}

TEST(ProviderHttpServer, StreamsLatestFrameAndBoundsSlowClients) {
    std::shared_ptr<const ProviderStreamFrame> current_stream_frame = makeStreamFrame(1U);
    ProviderHttpServer server("127.0.0.1", 0, 4, 1, 2000, 8192, 4096, std::vector<std::string>{},
                              makeRoutes(&current_stream_frame));
    server.startServer();

    StreamClient first_client(server.getBoundPort());
    const std::string stream_header = first_client.openStream("/stream/color.mjpg");
    EXPECT_NE(stream_header.find("200 OK"), std::string::npos);
    EXPECT_NE(stream_header.find("multipart/x-mixed-replace"), std::string::npos);
    const std::string first_part = first_client.readPart();
    EXPECT_NE(first_part.find("X-Nodus-Frame-Number: 1"), std::string::npos);
    ASSERT_TRUE(waitForStreamCount(server, 1));

    const http::response<http::string_body> rejected_stream =
        requestResponse(server.getBoundPort(), http::verb::get, "/stream/depth.mjpg");
    EXPECT_EQ(rejected_stream.result(), http::status::too_many_requests);
    EXPECT_EQ(server.getActiveStreamClientCount(), 1);

    std::atomic_store(&current_stream_frame, makeStreamFrame(100U, 1024U));
    for (int notification = 0; notification < 100; ++notification) {
        server.notifyStreamFrame(ProviderStreamKind::e_COLOR);
    }
    const std::string latest_part = first_client.readPart();
    EXPECT_NE(latest_part.find("X-Nodus-Frame-Number: 100"), std::string::npos);

    server.stopServer();
    EXPECT_EQ(server.getActiveConnectionCount(), 0);
    EXPECT_EQ(server.getActiveStreamClientCount(), 0);
}

TEST(ProviderHttpServer, RestartsAfterBoundedCancellation) {
    std::shared_ptr<const ProviderStreamFrame> current_stream_frame = makeStreamFrame(1U);
    ProviderHttpServer server("127.0.0.1", 0, 2, 1, 1000, 8192, 4096, std::vector<std::string>{},
                              makeRoutes(&current_stream_frame));
    server.startServer();
    EXPECT_GT(server.getBoundPort(), 0);
    server.stopServer();
    EXPECT_EQ(server.getBoundPort(), 0);
    server.startServer();
    EXPECT_GT(server.getBoundPort(), 0);
    server.stopServer();
}

TEST(ProviderHttpServer, CancelsBlockedSlowWriterWithoutQueuingFrames) {
    std::shared_ptr<const ProviderStreamFrame> current_stream_frame = makeStreamFrame(1U);
    ProviderHttpServer server("127.0.0.1", 0, 2, 1, 500, 8192, 4096, std::vector<std::string>{},
                              makeRoutes(&current_stream_frame));
    server.startServer();

    StreamClient slow_client(server.getBoundPort());
    (void)slow_client.openStream("/stream/color.mjpg");
    (void)slow_client.readPart();
    ASSERT_TRUE(waitForStreamCount(server, 1));

    std::atomic_store(&current_stream_frame, makeStreamFrame(2U, 8U * 1024U * 1024U));
    server.notifyStreamFrame(ProviderStreamKind::e_COLOR);
    std::atomic_store(&current_stream_frame, makeStreamFrame(200U));
    for (int notification = 0; notification < 200; ++notification) {
        server.notifyStreamFrame(ProviderStreamKind::e_COLOR);
    }

    const auto stop_started = std::chrono::steady_clock::now();
    server.stopServer();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
    EXPECT_LT(stop_elapsed, std::chrono::seconds(1));
    EXPECT_EQ(server.getActiveConnectionCount(), 0);
    EXPECT_EQ(server.getActiveStreamClientCount(), 0);
}

TEST(ProviderHttpServer, AppliesExactOriginPolicyToResponsesPreflightAndStreams) {
    constexpr const char* allowed_origin = "http://portal.test:5173";
    std::shared_ptr<const ProviderStreamFrame> current_stream_frame = makeStreamFrame(1U);
    ProviderHttpServer server("127.0.0.1", 0, 6, 2, 1000, 8192, 4096,
                              std::vector<std::string>{allowed_origin},
                              makeRoutes(&current_stream_frame));
    server.startServer();

    const http::response<http::string_body> health =
        requestResponse(server.getBoundPort(), http::verb::get, "/health", {}, allowed_origin);
    EXPECT_EQ(health.result(), http::status::ok);
    EXPECT_EQ(health["Access-Control-Allow-Origin"], allowed_origin);
    EXPECT_EQ(health[http::field::vary], "Origin");

    const http::response<http::string_body> preflight = requestResponse(
        server.getBoundPort(), http::verb::options, "/metadata", {}, allowed_origin);
    EXPECT_EQ(preflight.result(), http::status::no_content);
    EXPECT_EQ(preflight["Access-Control-Allow-Origin"], allowed_origin);
    EXPECT_EQ(preflight["Access-Control-Allow-Methods"], "GET, POST, OPTIONS");
    EXPECT_EQ(preflight["Access-Control-Allow-Headers"], "Accept, Content-Type");

    const http::response<http::string_body> denied = requestResponse(
        server.getBoundPort(), http::verb::get, "/health", {}, "http://denied.test:5173");
    EXPECT_EQ(denied.result(), http::status::forbidden);
    EXPECT_TRUE(denied["Access-Control-Allow-Origin"].empty());

    StreamClient stream_client(server.getBoundPort());
    const std::string stream_header =
        stream_client.openStream("/stream/color.mjpg", allowed_origin);
    EXPECT_NE(stream_header.find("Access-Control-Allow-Origin: http://portal.test:5173"),
              std::string::npos);
    server.stopServer();
}

}  // namespace nodus_vision
