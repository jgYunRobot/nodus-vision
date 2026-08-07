/**
 * @file test_pilot_integration_client.cpp
 * @brief fake Pilot public lifecycle client를 검증한다.
 */

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pilot_integration_client.hpp"

namespace nodus_vision {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class FakePilotLifecycle {
   public:
    explicit FakePilotLifecycle(bool retry_catalog_once = false)
        : m_retry_catalog_once(retry_catalog_once), m_acceptor(m_io_context) {
        m_acceptor.open(tcp::v4());
        m_acceptor.set_option(asio::socket_base::reuse_address(true));
        m_acceptor.bind({asio::ip::address_v4::loopback(), 0});
        m_acceptor.listen();
        m_port = m_acceptor.local_endpoint().port();
        m_worker = std::thread([this]() { runServer(); });
    }

    ~FakePilotLifecycle() {
        beast::error_code ignored;
        tcp::socket wake_socket(m_io_context);
        wake_socket.connect({asio::ip::address_v4::loopback(), m_port}, ignored);
        wake_socket.close(ignored);
        m_acceptor.cancel(ignored);
        m_acceptor.close(ignored);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    std::string getBaseUrl() const { return "http://127.0.0.1:" + std::to_string(m_port); }

    std::vector<std::pair<std::string, std::string>> getRequests() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_requests;
    }

   private:
    void runServer() {
        while (m_acceptor.is_open()) {
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
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_requests.emplace_back(std::string(request.target()), request.body());
            }
            const std::string target(request.target());
            http::status status = http::status::ok;
            std::string body = "{}";
            if (target == "/api/v1/components/register") {
                status = http::status::created;
                body =
                    R"json({"session_id":"opaque/session secret","server_instance_id":"pilot-instance","accepted_protocol_version":1,"accepted_schema_versions":[1],"heartbeat_interval_ms":20,"lease_timeout_ms":100,"server_time":0})json";
            } else if (target.find("/endpoint-catalog") != std::string::npos) {
                if (m_retry_catalog_once && !m_catalog_retry_sent) {
                    m_catalog_retry_sent = true;
                    status = http::status::service_unavailable;
                } else {
                    body =
                        R"json({"status":"accepted","server_instance_id":"pilot-instance","component_id":"camera.fake","session_generation":1,"catalog_generation":1,"catalog_revision":1,"descriptor_count":9})json";
                }
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
    tcp::acceptor m_acceptor;
    unsigned short m_port{0};
    bool m_retry_catalog_once{false};
    bool m_catalog_retry_sent{false};
    mutable std::mutex m_mutex;
    std::vector<std::pair<std::string, std::string>> m_requests;
    std::thread m_worker;
};

VisionConfig makeConfig(const std::string& base_url, bool enabled = true) {
    VisionConfig config;
    config.component_id = "camera.fake";
    config.device_id = "fake";
    config.adapter = "fake";
    config.provider.advertised_base_url = "http://127.0.0.1:8900";
    config.calibration.calibration_id = "fake-v1";
    config.calibration.sensor_frame = "fake_optical";
    config.pilot = {enabled, base_url, "monotonic_same_host", 50, 50, 4096, 5, 20, 50};
    return config;
}

template <typename Predicate>
bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

}  // namespace

TEST(PilotIntegrationClient, UsesOneSequenceForStateHeartbeatAndBoundedDisconnect) {
    FakePilotLifecycle server;
    PilotIntegrationClient client(makeConfig(server.getBaseUrl()),
                                  []() { return "vision-00112233445566778899aabbccddeeff"; });
    client.startClient(ProviderState::e_READY);
    ASSERT_TRUE(waitFor(
        [&client]() { return client.getSnapshot().state == PilotIntegrationState::e_ONLINE; }));
    EXPECT_EQ(client.getSnapshot().catalog_generation, 1U);
    EXPECT_EQ(client.getSnapshot().descriptor_count, 9);
    client.updateProviderState(ProviderState::e_DEGRADED);
    ASSERT_TRUE(waitFor([&server]() { return server.getRequests().size() >= 2U; }));
    client.stopClient();

    const std::vector<std::pair<std::string, std::string>> requests = server.getRequests();
    ASSERT_GE(requests.size(), 3U);
    EXPECT_EQ(requests.front().first, "/api/v1/components/register");
    EXPECT_EQ(requests.back().first, "/api/v1/components/opaque%2Fsession%20secret/disconnect");
    std::uint64_t previous_sequence = 0U;
    for (const auto& request : requests) {
        if (request.first.find("/heartbeat") == std::string::npos &&
            request.first.find("/state") == std::string::npos) {
            continue;
        }
        const std::uint64_t sequence = boost::json::parse(request.second)
                                           .as_object()
                                           .at("sequence")
                                           .to_number<std::uint64_t>();
        EXPECT_GT(sequence, previous_sequence);
        previous_sequence = sequence;
    }
    const PilotIntegrationSnapshot snapshot = client.getSnapshot();
    EXPECT_EQ(snapshot.server_instance_id, "pilot-instance");
    EXPECT_EQ(snapshot.last_error.find("opaque/session secret"), std::string::npos);
}

TEST(PilotIntegrationClient, DisabledClientMakesNoNetworkRequest) {
    PilotIntegrationClient client(makeConfig("http://127.0.0.1:1", false));
    client.startClient(ProviderState::e_READY);
    EXPECT_EQ(client.getSnapshot().state, PilotIntegrationState::e_DISABLED);
    client.stopClient();
}

TEST(PilotIntegrationClient, RetriesSameCatalogPublicationWithoutPayloadRelay) {
    FakePilotLifecycle server(true);
    PilotIntegrationClient client(makeConfig(server.getBaseUrl()),
                                  []() { return "vision-00112233445566778899aabbccddeeff"; });
    client.startClient(ProviderState::e_READY);
    ASSERT_TRUE(waitFor(
        [&client]() { return client.getSnapshot().state == PilotIntegrationState::e_ONLINE; }));
    client.stopClient();
    const std::vector<std::pair<std::string, std::string>> requests = server.getRequests();
    std::vector<std::string> publications;
    for (const auto& request : requests) {
        if (request.first.find("/endpoint-catalog") != std::string::npos) {
            publications.push_back(request.second);
        }
    }
    ASSERT_EQ(publications.size(), 2U);
    EXPECT_EQ(publications.at(0), publications.at(1));
    EXPECT_EQ(
        boost::json::parse(publications.at(0)).as_object().at("descriptors").as_array().size(), 9U);
}

TEST(PilotIntegrationClient, MissingPilotLeavesRecoveringSnapshotAndStops) {
    PilotIntegrationClient client(makeConfig("http://127.0.0.1:1"));
    client.startClient(ProviderState::e_READY);
    ASSERT_TRUE(waitFor(
        [&client]() { return client.getSnapshot().state == PilotIntegrationState::e_RECOVERING; }));
    const auto started_at = std::chrono::steady_clock::now();
    client.stopClient();
    EXPECT_LT(std::chrono::steady_clock::now() - started_at, std::chrono::milliseconds(200));
}

}  // namespace nodus_vision
