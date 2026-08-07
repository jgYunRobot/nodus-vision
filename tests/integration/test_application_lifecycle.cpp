/**
 * @file test_application_lifecycle.cpp
 * @brief fake provider의 lifecycle과 direct data-plane wiring을 검증한다.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
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

class TemporaryRecordingRoot {
   public:
    TemporaryRecordingRoot() {
        char template_path[] = "/tmp/nodus-vision-recording-http-XXXXXX";
        char* created = mkdtemp(template_path);
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary recording root.");
        }
        m_path = created;
    }
    ~TemporaryRecordingRoot() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& getPath() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

VisionConfig makeRecordingConfig(const std::filesystem::path& root) {
    VisionConfig config = makeFakeConfig();
    config.fake.width = 64;
    config.fake.height = 64;
    config.recording = {true, root.string(), 4, 10000, 1U, 1000, 100000, "veryfast", "zerolatency"};
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

class RestartableFakePilot {
   public:
    ~RestartableFakePilot() { stopServer(); }

    int startServer(int requested_port = 0) {
        m_io_context.restart();
        m_p_acceptor = std::make_unique<tcp::acceptor>(m_io_context);
        m_p_acceptor->open(tcp::v4());
        m_p_acceptor->set_option(asio::socket_base::reuse_address(true));
        m_p_acceptor->bind(
            {asio::ip::address_v4::loopback(), static_cast<unsigned short>(requested_port)});
        m_p_acceptor->listen();
        m_port = m_p_acceptor->local_endpoint().port();
        ++m_instance_index;
        m_running.store(true);
        m_worker = std::thread([this]() { runServer(); });
        return m_port;
    }

    void stopServer() {
        if (!m_running.exchange(false)) {
            return;
        }
        beast::error_code ignored;
        asio::io_context wake_context;
        tcp::socket wake_socket(wake_context);
        wake_socket.connect({asio::ip::address_v4::loopback(), static_cast<unsigned short>(m_port)},
                            ignored);
        wake_socket.close(ignored);
        m_p_acceptor->cancel(ignored);
        m_p_acceptor->close(ignored);
        if (m_worker.joinable()) {
            m_worker.join();
        }
        m_p_acceptor.reset();
    }

   private:
    void runServer() {
        while (m_running.load()) {
            tcp::socket socket(m_io_context);
            beast::error_code error;
            m_p_acceptor->accept(socket, error);
            if (error || !m_running.load()) {
                return;
            }
            beast::flat_buffer buffer;
            http::request<http::string_body> request;
            http::read(socket, buffer, request, error);
            if (error) {
                continue;
            }
            const std::string target(request.target());
            http::status status = http::status::ok;
            std::string body = "{}";
            if (target == "/api/v1/components/register") {
                status = http::status::created;
                body = "{\"session_id\":\"session-" + std::to_string(m_instance_index) +
                       "\",\"server_instance_id\":\"pilot-" + std::to_string(m_instance_index) +
                       "\",\"accepted_protocol_version\":1,\"accepted_schema_versions\":[1],"
                       "\"heartbeat_interval_ms\":20,\"lease_timeout_ms\":100,\"server_time\":0}";
            } else if (target.find("/endpoint-catalog") != std::string::npos) {
                body = "{\"status\":\"accepted\",\"server_instance_id\":\"pilot-" +
                       std::to_string(m_instance_index) +
                       "\",\"component_id\":\"camera.fake\",\"session_generation\":1,\"catalog_"
                       "generation\":1,\"catalog_revision\":1,\"descriptor_count\":9}";
            }
            http::response<http::string_body> response(status, request.version());
            response.set(http::field::content_type, "application/json");
            response.keep_alive(false);
            response.body() = body;
            response.prepare_payload();
            http::write(socket, response, error);
        }
    }

    asio::io_context m_io_context;
    std::unique_ptr<tcp::acceptor> m_p_acceptor;
    std::atomic<bool> m_running{false};
    std::thread m_worker;
    int m_port{0};
    int m_instance_index{0};
};

template <typename Predicate>
bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
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
    EXPECT_EQ(boost::json::parse(metadata.body()).as_object().at("api_version"), "1.1.0");
    const http::response<http::string_body> health =
        requestResponse(first_port, http::verb::get, "/health");
    const boost::json::object health_json = boost::json::parse(health.body()).as_object();
    ASSERT_EQ(health_json.size(), 7U);
    EXPECT_EQ(health_json.at("schema_version"), 1);
    EXPECT_EQ(health_json.at("state"), "ready");
    ASSERT_TRUE(health_json.at("pilot").is_object());
    const boost::json::object& pilot = health_json.at("pilot").as_object();
    ASSERT_EQ(pilot.size(), 8U);
    EXPECT_FALSE(pilot.at("enabled").as_bool());
    EXPECT_EQ(pilot.at("state"), "disabled");
    EXPECT_TRUE(pilot.at("server_instance_id").is_null());
    EXPECT_EQ(pilot.at("catalog_generation"), 0U);
    ASSERT_TRUE(health_json.at("recording").is_object());
    const boost::json::object& recording = health_json.at("recording").as_object();
    EXPECT_FALSE(recording.at("enabled").as_bool());
    EXPECT_EQ(recording.at("state"), "disabled");
    EXPECT_TRUE(recording.at("recording_id").is_null());
    EXPECT_EQ(pilot.at("descriptor_count"), 0);
    EXPECT_EQ(pilot.at("retry_count"), 0);
    EXPECT_TRUE(pilot.at("last_success_age_ms").is_null());
    EXPECT_TRUE(pilot.at("last_error").is_null());
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

TEST(VisionApplication, KeepsDirectProviderAvailableWhenPilotIsAbsent) {
    VisionConfig config = makeFakeConfig();
    config.pilot = {true, "http://127.0.0.1:1", "monotonic_same_host", 20, 20, 4096, 5, 20, 20};
    VisionApplication application(config);
    application.startApplication();
    const int port = application.getBoundPort();
    ASSERT_GT(port, 0);
    EXPECT_EQ(requestResponse(port, http::verb::get, "/health").result(), http::status::ok);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline &&
           application.getHealthSnapshot().pilot.state != PilotIntegrationState::e_RECOVERING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(application.getHealthSnapshot().state, ProviderState::e_READY);
    EXPECT_EQ(application.getHealthSnapshot().pilot.state, PilotIntegrationState::e_RECOVERING);
    application.stopApplication();
}

TEST(VisionApplication, RecoversPilotRestartWithoutRestartingProviderOrCapture) {
    RestartableFakePilot pilot;
    const int pilot_port = pilot.startServer();
    VisionConfig config = makeFakeConfig();
    config.pilot = {true,
                    "http://127.0.0.1:" + std::to_string(pilot_port),
                    "monotonic_same_host",
                    20,
                    20,
                    4096,
                    5,
                    20,
                    20};
    VisionApplication application(config);
    application.startApplication();
    const int provider_port = application.getBoundPort();
    ASSERT_TRUE(waitFor([&application]() {
        return application.getHealthSnapshot().pilot.state == PilotIntegrationState::e_ONLINE;
    }));
    const std::uint64_t capture_generation =
        application.getHealthSnapshot().camera.latest_identity.capture_generation;
    pilot.stopServer();
    ASSERT_TRUE(waitFor([&application]() {
        return application.getHealthSnapshot().pilot.state == PilotIntegrationState::e_RECOVERING;
    }));
    EXPECT_EQ(requestResponse(provider_port, http::verb::get, "/health").result(),
              http::status::ok);
    EXPECT_EQ(application.getBoundPort(), provider_port);
    EXPECT_EQ(application.getHealthSnapshot().camera.latest_identity.capture_generation,
              capture_generation);
    pilot.startServer(pilot_port);
    ASSERT_TRUE(waitFor([&application]() {
        return application.getHealthSnapshot().pilot.state == PilotIntegrationState::e_ONLINE &&
               application.getHealthSnapshot().pilot.server_instance_id == "pilot-2";
    }));
    EXPECT_EQ(application.getBoundPort(), provider_port);
    EXPECT_EQ(application.getHealthSnapshot().camera.latest_identity.capture_generation,
              capture_generation);
    application.stopApplication();
}

TEST(VisionApplication, RecordsThroughDirectHttpLifecycle) {
    TemporaryRecordingRoot root;
    VisionApplication application(makeRecordingConfig(root.getPath()));
    application.startApplication();
    const int port = application.getBoundPort();
    const std::string start =
        "{\"schema_version\":1,\"request_id\":\"start-001\",\"recording_id\":\"episode-0001-"
        "front\",\"expected_device_id\":\"fake\",\"expected_calibration_id\":\"fake_calibration\","
        "\"expected_profile\":{\"width\":64,\"height\":64,\"fps\":30,\"pixel_format\":\"rgb24\"}}";
    EXPECT_EQ(requestResponse(port, http::verb::post, "/recordings/start", start).result(),
              http::status::created);
    EXPECT_EQ(requestResponse(port, http::verb::post, "/recordings/start", start).result(),
              http::status::ok);
    const http::response<http::string_body> health =
        requestResponse(port, http::verb::get, "/health");
    EXPECT_TRUE(boost::json::parse(health.body())
                    .as_object()
                    .at("recording")
                    .as_object()
                    .at("enabled")
                    .as_bool());
    ASSERT_TRUE(waitFor([&application]() {
        return application.getHealthSnapshot().camera.latest_identity.frame_number > 2U;
    }));
    const std::string stop =
        "{\"schema_version\":1,\"request_id\":\"stop-001\",\"recording_id\":\"episode-0001-"
        "front\"}";
    EXPECT_EQ(requestResponse(port, http::verb::post, "/recordings/stop", stop).result(),
              http::status::accepted);
    EXPECT_EQ(requestResponse(port, http::verb::post, "/recordings/stop", stop).result(),
              http::status::ok);
    const std::filesystem::path artifact = root.getPath() / "finalized" / "episode-0001-front";
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "color.mp4"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "frames.jsonl"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "recording_manifest.json"));
    std::ifstream manifest_input(artifact / "recording_manifest.json");
    const boost::json::object manifest =
        boost::json::parse(std::string(std::istreambuf_iterator<char>(manifest_input),
                                       std::istreambuf_iterator<char>()))
            .as_object();
    EXPECT_EQ(manifest.at("recording_id").as_string(), "episode-0001-front");
    EXPECT_GT(manifest.at("submitted_frame_count").to_number<std::uint64_t>(), 0U);
    const http::response<http::string_body> current =
        requestResponse(port, http::verb::get, "/recordings/current");
    EXPECT_EQ(current.result(), http::status::ok);
    EXPECT_EQ(boost::json::parse(current.body()).as_object().at("artifact_reference").as_string(),
              "finalized/episode-0001-front");
    application.stopApplication();
}

}  // namespace nodus_vision
