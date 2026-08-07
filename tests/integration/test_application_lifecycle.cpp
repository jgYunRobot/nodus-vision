/**
 * @file test_application_lifecycle.cpp
 * @brief fake provider의 lifecycle과 direct data-plane wiring을 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <string>
#include <thread>

#include "vision_application.hpp"

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

VisionConfig makeFakeConfig() {
    VisionConfig config;
    config.schema_version = 1;
    config.device_id = "fake";
    config.component_id = "camera.fake";
    config.adapter = "fake";
    config.fake = {4, 3, 30, 1U, 7U, true, false, false, "fake"};
    config.calibration.calibration_id = "fake_calibration";
    config.calibration.sensor_frame = "fake_optical";
    config.calibration.mount_frame = "fake_mount";
    config.provider = {"127.0.0.1", 0, "http://127.0.0.1:8900", 4, 1, 1000, 8192, 4096, 1000};
    return config;
}

http::response<http::string_body> requestResponse(int port, http::verb method,
                                                  const std::string& target,
                                                  const std::string& body = {}) {
    asio::io_context context;
    tcp::socket socket(context);
    socket.connect({asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "localhost");
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

http::response<http::string_body> waitForColorSnapshot(int port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    http::response<http::string_body> response;
    while (std::chrono::steady_clock::now() < deadline) {
        response = requestResponse(port, http::verb::get, "/snapshot/color");
        if (response.result() == http::status::ok) {
            return response;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return response;
}

}  // namespace

TEST(VisionApplication, ServesFakeDataPlaneAndRestartsCleanly) {
    VisionApplication application(makeFakeConfig());
    application.startApplication();
    const int first_port = application.getBoundPort();
    ASSERT_GT(first_port, 0);
    EXPECT_EQ(application.getHealthSnapshot().state, ProviderState::e_READY);

    const http::response<http::string_body> snapshot = waitForColorSnapshot(first_port);
    ASSERT_EQ(snapshot.result(), http::status::ok);
    EXPECT_EQ(snapshot[http::field::content_type], "image/jpeg");
    EXPECT_FALSE(snapshot["X-Nodus-Frame-Number"].empty());
    EXPECT_EQ(snapshot["X-Nodus-Sensor-Frame"], "fake_optical");
    EXPECT_EQ(snapshot["X-Nodus-Calibration-Id"], "fake_calibration");

    const http::response<http::string_body> metadata =
        requestResponse(first_port, http::verb::get, "/metadata");
    EXPECT_NE(metadata.body().find("/stream/color.mjpg"), std::string::npos);
    EXPECT_NE(metadata.body().find("/query/roi_depth"), std::string::npos);

    const http::response<http::string_body> query =
        requestResponse(first_port, http::verb::post, "/query/pixel_to_point", "{\"x\":1,\"y\":1}");
    EXPECT_EQ(query.result(), http::status::ok);
    EXPECT_FALSE(query["X-Nodus-Frame-Number"].empty());
    const http::response<http::string_body> invalid_query = requestResponse(
        first_port, http::verb::post, "/query/pixel_to_point", "{\"x\":99,\"y\":1}");
    EXPECT_EQ(invalid_query.result(), http::status::bad_request);

    application.stopApplication();
    application.stopApplication();
    EXPECT_EQ(application.getBoundPort(), 0);
    application.startApplication();
    EXPECT_GT(application.getBoundPort(), 0);
    EXPECT_EQ(application.getHealthSnapshot().state, ProviderState::e_READY);
    application.stopApplication();
}

}  // namespace nodus_vision
