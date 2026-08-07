/**
 * @file pilot_integration_client.cpp
 * @brief single-worker Pilot component lifecycle client를 구현한다.
 */

#include "pilot_integration_client.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

#include "pilot_http_transport.hpp"
#include "vision_endpoint_catalog.hpp"

namespace nodus_vision {
namespace {

std::uint64_t getSteadyNowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

PilotCommonState makePilotState(ProviderState state) {
    switch (state) {
        case ProviderState::e_READY:
            return {"ready", "", {}};
        case ProviderState::e_DEGRADED:
            return {"degraded", "camera_degraded", {}};
        case ProviderState::e_STOPPING:
            return {"stopping", "provider_stopping", {}};
        case ProviderState::e_STARTING:
        default:
            return {"starting", "", {}};
    }
}

std::string encodePathSegment(const std::string& input) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(input.size());
    for (const unsigned char character : input) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(HEX[(character >> 4U) & 0x0FU]);
            result.push_back(HEX[character & 0x0FU]);
        }
    }
    return result;
}

bool isContractStatus(int status) { return status == 400 || status == 413 || status == 415; }

std::string getErrorCode(const std::string& body) {
    boost::json::error_code error;
    const boost::json::value value = boost::json::parse(body, error);
    if (error || !value.is_object()) {
        return "";
    }
    const boost::json::value* p_error = value.as_object().if_contains("error");
    if (p_error == nullptr || !p_error->is_object()) {
        return "";
    }
    const boost::json::value* p_code = p_error->as_object().if_contains("code");
    return p_code != nullptr && p_code->is_string() ? std::string(p_code->as_string()) : "";
}

}  // namespace

class PilotIntegrationClient::Impl {
   public:
    enum class PublishResult { e_PUBLISHED, e_REPLACE_SESSION, e_CONTRACT_FAULT, e_STOPPED };
    Impl(VisionConfig config, InstanceIdGenerator instance_id_generator)
        : m_config(std::move(config)), m_instance_id_generator(std::move(instance_id_generator)) {}

    void setSnapshot(PilotIntegrationState state, const std::string& error = {}) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.enabled = m_config.pilot.enabled;
        m_snapshot.state = state;
        m_snapshot.last_error = error;
        if (error.empty()) {
            m_last_success = std::chrono::steady_clock::now();
        }
    }

    void updateSuccess() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.state = PilotIntegrationState::e_ONLINE;
        m_snapshot.last_error.clear();
        m_last_success = std::chrono::steady_clock::now();
    }

    PublishResult publishCatalog(const std::string& session_id) {
        const std::string publication_id = "catalog-" + m_instance_id + "-1";
        const std::string publication_body =
            serializeCatalogPublicationRequest(publication_id, 0U, m_catalog.descriptors);
        const std::string target =
            "/api/v1/components/" + encodePathSegment(session_id) + "/endpoint-catalog";
        int retry_count = 0;
        while (!m_stop_requested) {
            const PilotHttpResult result =
                m_p_transport->executeRequest({"PUT", target, publication_body});
            if (m_stop_requested) {
                return PublishResult::e_STOPPED;
            }
            if (result.status == 200) {
                try {
                    const PilotCatalogAcceptedResponse response = parseCatalogAcceptedResponse(
                        result.body, m_config.component_id,
                        static_cast<int>(m_catalog.descriptors.size()));
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_snapshot.server_instance_id = response.server_instance_id;
                        m_snapshot.catalog_generation = response.catalog_generation;
                        m_snapshot.descriptor_count = response.descriptor_count;
                    }
                    return PublishResult::e_PUBLISHED;
                } catch (const std::exception&) {
                    setSnapshot(PilotIntegrationState::e_RECOVERING, "invalid_catalog_response");
                    return PublishResult::e_REPLACE_SESSION;
                }
            }
            const std::string error_code = getErrorCode(result.body);
            if (result.status == 409 && error_code == "stale_catalog_generation") {
                setSnapshot(PilotIntegrationState::e_RECOVERING, "stale_catalog_generation");
                return PublishResult::e_REPLACE_SESSION;
            }
            if (result.status == 409 && error_code == "idempotency_conflict") {
                setSnapshot(PilotIntegrationState::e_CONTRACT_FAULT,
                            "catalog_idempotency_conflict");
                return PublishResult::e_CONTRACT_FAULT;
            }
            if (isContractStatus(result.status)) {
                setSnapshot(PilotIntegrationState::e_CONTRACT_FAULT, "catalog_contract_error");
                return PublishResult::e_CONTRACT_FAULT;
            }
            if (result.status == 404 || (result.status == 409 && !error_code.empty())) {
                setSnapshot(PilotIntegrationState::e_RECOVERING, "catalog_session_replaced");
                return PublishResult::e_REPLACE_SESSION;
            }
            ++retry_count;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot.retry_count = retry_count;
            }
            const int capped_delay = std::min(
                m_config.pilot.retry_max_delay_ms,
                m_config.pilot.retry_initial_delay_ms * (1 << std::min(retry_count - 1, 10)));
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait_for(lock, std::chrono::milliseconds(capped_delay),
                                 [this]() { return m_stop_requested; });
        }
        return PublishResult::e_STOPPED;
    }

    void runWorker() {
        std::string session_id;
        std::uint64_t sequence = 1U;
        int retry_count = 0;
        while (!m_stop_requested) {
            setSnapshot(PilotIntegrationState::e_REGISTERING);
            PilotRegistrationRequest request;
            request.component_id = m_config.component_id;
            request.instance_id = m_instance_id;
            request.capabilities = m_catalog.capabilities;
            request.initial_state = makePilotState(m_current_state);
            request.started_at_ns = m_started_at_ns;
            request.metadata["device_id"] = m_config.device_id;
            request.metadata["sensor_frame"] = m_config.calibration.sensor_frame;
            request.metadata["calibration_id"] = m_config.calibration.calibration_id;
            request.metadata["provider_api_version"] = "1.1.0";
            request.clock_domain = m_config.pilot.clock_domain;
            PilotHttpResult result = m_p_transport->executeRequest(
                {"POST", "/api/v1/components/register", serializeRegistrationRequest(request)});
            if (m_stop_requested) {
                break;
            }
            if (result.status == 201) {
                try {
                    const PilotRegistrationResponse response =
                        parseRegistrationResponse(result.body);
                    session_id = response.session_id;
                    sequence = 1U;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_snapshot.server_instance_id = response.server_instance_id;
                        m_snapshot.retry_count = retry_count;
                        m_snapshot.catalog_generation = 0U;
                        m_snapshot.descriptor_count = 0;
                    }
                    const PublishResult publish_result = publishCatalog(session_id);
                    if (publish_result == PublishResult::e_STOPPED) {
                        break;
                    }
                    if (publish_result == PublishResult::e_CONTRACT_FAULT) {
                        return;
                    }
                    if (publish_result == PublishResult::e_REPLACE_SESSION) {
                        session_id.clear();
                        continue;
                    }
                    updateSuccess();
                    retry_count = 0;
                    if (runSession(session_id, response.heartbeat_interval_ms, sequence)) {
                        break;
                    }
                } catch (const std::exception&) {
                    setSnapshot(PilotIntegrationState::e_RECOVERING,
                                "invalid_registration_response");
                }
            } else if (isContractStatus(result.status)) {
                setSnapshot(PilotIntegrationState::e_CONTRACT_FAULT, "registration_contract_error");
                return;
            } else {
                setSnapshot(PilotIntegrationState::e_RECOVERING,
                            result.error.empty() ? "registration_unavailable"
                                                 : "registration_transport_error");
            }
            ++retry_count;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot.retry_count = retry_count;
            }
            const int capped_delay = std::min(
                m_config.pilot.retry_max_delay_ms,
                m_config.pilot.retry_initial_delay_ms * (1 << std::min(retry_count - 1, 10)));
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait_for(lock, std::chrono::milliseconds(capped_delay),
                                 [this]() { return m_stop_requested; });
        }
        if (!session_id.empty()) {
            setSnapshot(PilotIntegrationState::e_STOPPING);
            m_p_transport->executeRequest(
                {"POST", "/api/v1/components/" + encodePathSegment(session_id) + "/disconnect",
                 serializeDisconnectRequest()});
        }
        setSnapshot(m_config.pilot.enabled ? PilotIntegrationState::e_STOPPED
                                           : PilotIntegrationState::e_DISABLED);
    }

    bool runSession(const std::string& session_id, std::uint64_t heartbeat_interval_ms,
                    std::uint64_t& sequence) {
        const std::string session_path = "/api/v1/components/" + encodePathSegment(session_id);
        std::chrono::steady_clock::time_point next_heartbeat =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(heartbeat_interval_ms);
        while (!m_stop_requested) {
            std::optional<ProviderState> pending_state;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait_until(lock, next_heartbeat, [this]() {
                    return m_stop_requested || m_pending_state.has_value();
                });
                if (m_stop_requested) {
                    return true;
                }
                pending_state = m_pending_state;
                m_pending_state.reset();
            }
            PilotHttpRequest request;
            if (pending_state.has_value()) {
                request = {"POST", session_path + "/state",
                           serializeStateUpdateRequest(sequence++, makePilotState(*pending_state))};
            } else {
                request = {"POST", session_path + "/heartbeat",
                           serializeHeartbeatRequest(sequence++)};
            }
            const PilotHttpResult result = m_p_transport->executeRequest(request);
            if (m_stop_requested) {
                return true;
            }
            if (result.status != 200) {
                setSnapshot(PilotIntegrationState::e_RECOVERING, result.error.empty()
                                                                     ? "lifecycle_write_rejected"
                                                                     : "lifecycle_transport_error");
                return false;
            }
            updateSuccess();
            next_heartbeat =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(heartbeat_interval_ms);
        }
        return true;
    }

    VisionConfig m_config;
    InstanceIdGenerator m_instance_id_generator;
    VisionEndpointCatalog m_catalog;
    std::unique_ptr<PilotHttpTransport> m_p_transport;
    std::thread m_worker;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    PilotIntegrationSnapshot m_snapshot;
    std::optional<ProviderState> m_pending_state;
    ProviderState m_current_state{ProviderState::e_STARTING};
    std::chrono::steady_clock::time_point m_last_success{};
    std::string m_instance_id;
    std::uint64_t m_started_at_ns{0U};
    bool m_started{false};
    bool m_stop_requested{false};
};

PilotIntegrationClient::PilotIntegrationClient(VisionConfig config,
                                               InstanceIdGenerator instance_id_generator)
    : m_p_impl(std::make_unique<Impl>(std::move(config), std::move(instance_id_generator))) {}

PilotIntegrationClient::~PilotIntegrationClient() { stopClient(); }

void PilotIntegrationClient::startClient(ProviderState initial_state) {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (m_p_impl->m_started) {
        return;
    }
    m_p_impl->m_started = true;
    m_p_impl->m_current_state = initial_state;
    m_p_impl->m_snapshot = {};
    m_p_impl->m_snapshot.enabled = m_p_impl->m_config.pilot.enabled;
    if (!m_p_impl->m_config.pilot.enabled) {
        m_p_impl->m_snapshot.state = PilotIntegrationState::e_DISABLED;
        return;
    }
    m_p_impl->m_catalog = VisionEndpointCatalogBuilder().buildCatalog(m_p_impl->m_config);
    m_p_impl->m_instance_id = generateInstanceId(m_p_impl->m_instance_id_generator);
    m_p_impl->m_started_at_ns = getSteadyNowNs();
    m_p_impl->m_stop_requested = false;
    m_p_impl->m_p_transport = std::make_unique<PilotHttpTransport>(
        m_p_impl->m_config.pilot.base_url, m_p_impl->m_config.pilot.connect_timeout_ms,
        std::min(m_p_impl->m_config.pilot.request_timeout_ms,
                 m_p_impl->m_config.pilot.shutdown_timeout_ms),
        m_p_impl->m_config.pilot.max_response_bytes);
    m_p_impl->m_worker = std::thread([impl = m_p_impl.get()]() { impl->runWorker(); });
}

void PilotIntegrationClient::updateProviderState(ProviderState state) {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    if (!m_p_impl->m_started || !m_p_impl->m_config.pilot.enabled) {
        return;
    }
    m_p_impl->m_pending_state = state;
    m_p_impl->m_condition.notify_one();
}

void PilotIntegrationClient::stopClient() noexcept {
    PilotHttpTransport* p_transport = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
        if (!m_p_impl->m_started) {
            return;
        }
        m_p_impl->m_stop_requested = true;
        m_p_impl->m_condition.notify_one();
        p_transport = m_p_impl->m_p_transport.get();
    }
    if (p_transport != nullptr) {
        p_transport->cancelRequest();
    }
    if (m_p_impl->m_worker.joinable()) {
        m_p_impl->m_worker.join();
    }
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    m_p_impl->m_p_transport.reset();
    m_p_impl->m_started = false;
}

PilotIntegrationSnapshot PilotIntegrationClient::getSnapshot() const {
    std::lock_guard<std::mutex> lock(m_p_impl->m_mutex);
    PilotIntegrationSnapshot snapshot = m_p_impl->m_snapshot;
    if (m_p_impl->m_last_success.time_since_epoch().count() != 0) {
        snapshot.last_success_age_ms =
            static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - m_p_impl->m_last_success)
                                 .count());
    }
    return snapshot;
}

}  // namespace nodus_vision
