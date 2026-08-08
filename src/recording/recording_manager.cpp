/** @file recording_manager.cpp @brief bounded recording worker를 구현한다. */

#include "recording_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "color_video_writer.hpp"
#include "recording_manifest.hpp"
#include "recording_store.hpp"

namespace nodus_vision {
namespace {

bool isStrictlyAfter(const FrameIdentity& candidate, const FrameIdentity& previous) noexcept {
    return candidate.capture_generation > previous.capture_generation ||
           (candidate.capture_generation == previous.capture_generation &&
            candidate.frame_number > previous.frame_number);
}

std::int64_t getMonotonicNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::int64_t getUnixNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string serializeInternalStopEvidence(const std::string& recording_id,
                                          RecordingStopReason reason) {
    return "{\"schema_version\":1,\"recording_id\":\"" + recording_id + "\",\"stop_reason\":\"" +
           getRecordingStopReasonName(reason) + "\"}";
}

}  // namespace

class RecordingManager::Impl {
   public:
    explicit Impl(RecordingManagerConfig config)
        : m_config(std::move(config)),
          m_store(m_config.root),
          m_queue(m_config.queue_capacity_frames) {
        if (m_config.queue_capacity_frames == 0U || m_config.sensor_frame.empty() ||
            m_config.calibration_id.empty() || m_config.component_id.empty() ||
            m_config.device_id.empty() || m_config.instance_id.empty() ||
            m_config.max_duration_ms <= 0 || m_config.minimum_free_bytes == 0U ||
            m_config.finalize_timeout_ms <= 0 || m_config.preset != "veryfast" ||
            m_config.tune != "zerolatency") {
            throw std::invalid_argument("Recording manager configuration is invalid.");
        }
    }

    ~Impl() { finalize(); }

    RecordingStartResult startOrReplay(const RecordingStartRequest& request) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string canonical = serializeRecordingStartRequest(request);
        if (request.recording_id == m_recording_id && request.request_id == m_start_request_id) {
            if (canonical == m_start_canonical) {
                return RecordingStartResult::e_REPLAYED;
            }
            throw std::runtime_error("Recording request ID was reused with different content.");
        }
        if (m_state == RecordingState::e_PREPARING || m_state == RecordingState::e_RECORDING ||
            m_state == RecordingState::e_FINALIZING) {
            throw std::runtime_error("A recording is already active.");
        }
        if (m_worker.joinable()) {
            m_worker.join();
        }
        const std::optional<PersistedRecordingArtifact> persisted =
            m_store.findPersistedArtifact(request.recording_id);
        if (persisted.has_value()) {
            if (persisted->start_request != canonical) {
                throw std::runtime_error(
                    "Recording identity exists with different start request content.");
            }
            restorePersistedArtifactLocked(*persisted, request, canonical);
            return RecordingStartResult::e_REPLAYED;
        }
        m_state = RecordingState::e_PREPARING;
        try {
            m_store.initialize();
            std::error_code space_error;
            const std::filesystem::space_info space =
                std::filesystem::space(m_config.root, space_error);
            if (space_error || space.available < m_config.minimum_free_bytes) {
                throw std::runtime_error(
                    "Recording root does not satisfy the configured free-space reserve.");
            }
            const RecordingArtifactPaths paths = m_store.createStaging(request);
            m_artifact_paths = paths;
            m_writer = std::make_unique<ColorVideoWriter>(ColorVideoWriterConfig{
                paths.staging_directory / "color.mp4", m_config.width, m_config.height,
                m_config.fps, m_config.bit_rate_bps, m_config.preset, m_config.tune});
            m_sidecar = std::make_unique<RecordingSidecarWriter>(
                paths.staging_directory / "frames.jsonl", request.recording_id,
                m_config.sensor_frame, m_config.calibration_id);
            m_recording_id = request.recording_id;
            m_start_request_id = request.request_id;
            m_start_canonical = canonical;
            m_admitted_frame_count = 0U;
            m_submitted_frame_count = 0U;
            m_recording_drop_count = 0U;
            m_contention_drop_count.store(0U, std::memory_order_relaxed);
            m_queue_head = 0U;
            m_queue_tail = 0U;
            m_queue_count = 0U;
            for (std::shared_ptr<const CapturedFrame>& frame : m_queue) {
                frame.reset();
            }
            m_stop_requested = false;
            m_stop_request_id.clear();
            m_stop_canonical.clear();
            m_stop_reason = RecordingStopReason::e_APPLICATION_SHUTDOWN;
            m_has_identity = false;
            m_started_monotonic_ns = getMonotonicNowNs();
            m_started_unix_epoch_ns = getUnixNowNs();
            m_stopped_monotonic_ns = 0;
            m_stopped_unix_epoch_ns = 0;
            m_duration_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(m_config.max_duration_ms);
            m_worker = std::thread(&Impl::runWorker, this);
            m_state = RecordingState::e_RECORDING;
            m_admitting.store(true, std::memory_order_release);
            return RecordingStartResult::e_STARTED;
        } catch (...) {
            m_state = RecordingState::e_FAULTED;
            m_admitting.store(false, std::memory_order_release);
            throw;
        }
    }

    bool trySubmitFrame(std::shared_ptr<const CapturedFrame> frame) noexcept {
        if (frame == nullptr) {
            return false;
        }
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            if (m_admitting.load(std::memory_order_acquire)) {
                m_contention_drop_count.fetch_add(1U, std::memory_order_relaxed);
            }
            return false;
        }
        if (m_state != RecordingState::e_RECORDING) {
            return false;
        }
        if (m_queue_count == m_queue.size()) {
            ++m_recording_drop_count;
            return false;
        }
        m_queue[m_queue_tail] = std::move(frame);
        m_queue_tail = (m_queue_tail + 1U) % m_queue.size();
        ++m_queue_count;
        ++m_admitted_frame_count;
        lock.unlock();
        m_frame_available.notify_one();
        return true;
    }

    void finalize() {
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state != RecordingState::e_RECORDING && m_state != RecordingState::e_FINALIZING &&
                m_state != RecordingState::e_FINALIZED && m_state != RecordingState::e_FAULTED) {
                return;
            }
            if (m_state == RecordingState::e_RECORDING) {
                try {
                    beginFinalizationLocked(
                        RecordingStopReason::e_APPLICATION_SHUTDOWN,
                        serializeInternalStopEvidence(m_recording_id,
                                                      RecordingStopReason::e_APPLICATION_SHUTDOWN),
                        "", "");
                } catch (const std::exception&) {
                    m_state = RecordingState::e_FAULTED;
                    m_admitting.store(false, std::memory_order_release);
                    m_stop_requested = true;
                }
            }
            should_notify = m_stop_requested;
        }
        if (should_notify) {
            m_frame_available.notify_one();
        }
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    RecordingStopResult finalizeOrReplay(const RecordingStartRequest& request) {
        if (!isRecordingRequestIdValid(request.request_id) ||
            !isRecordingIdValid(request.recording_id)) {
            throw std::invalid_argument("Recording stop request has an unsafe identity.");
        }
        const std::string canonical = request.canonical_json.empty()
                                          ? "{\"schema_version\":1,\"request_id\":\"" +
                                                request.request_id + "\",\"recording_id\":\"" +
                                                request.recording_id + "\"}"
                                          : request.canonical_json;
        bool should_notify = false;
        RecordingStopResult result = RecordingStopResult::e_ACCEPTED;
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (request.recording_id == m_recording_id && request.request_id == m_stop_request_id) {
                if (canonical == m_stop_canonical) {
                    if (m_state == RecordingState::e_FAULTED) {
                        throw std::runtime_error("Recording finalization faulted.");
                    }
                    return m_state == RecordingState::e_FINALIZED ? RecordingStopResult::e_REPLAYED
                                                                  : RecordingStopResult::e_ACCEPTED;
                }
                throw std::runtime_error(
                    "Recording stop request ID was reused with different content.");
            }

            const std::optional<PersistedRecordingArtifact> persisted =
                m_store.findPersistedArtifact(request.recording_id);
            if (persisted.has_value() && persisted->stop_request.has_value()) {
                if (*persisted->stop_request != canonical) {
                    throw std::runtime_error(
                        "Recording identity exists with different stop request content.");
                }
                if (persisted->finalized) {
                    if (m_state != RecordingState::e_RECORDING &&
                        m_state != RecordingState::e_FINALIZING) {
                        restorePersistedArtifactLocked(*persisted, request, "");
                        m_stop_request_id = request.request_id;
                        m_stop_canonical = canonical;
                        m_stop_reason = RecordingStopReason::e_REQUESTED;
                    }
                    return RecordingStopResult::e_REPLAYED;
                }
                if (m_state != RecordingState::e_FINALIZING ||
                    request.recording_id != m_recording_id) {
                    throw std::runtime_error("Persisted recording finalization did not complete.");
                }
            }
            if (request.recording_id != m_recording_id) {
                throw RecordingNotFoundError("Recording is unknown.");
            }
            if (m_state != RecordingState::e_RECORDING) {
                throw std::runtime_error("Recording is not accepting a stop request.");
            }
            beginFinalizationLocked(RecordingStopReason::e_REQUESTED, canonical, request.request_id,
                                    canonical);
            should_notify = true;
            result = RecordingStopResult::e_ACCEPTED;
        } catch (...) {
            m_frame_available.notify_one();
            throw;
        }
        if (should_notify) {
            m_frame_available.notify_one();
        }
        return result;
    }

    RecordingStatus getStatus() const {
        RecordingStatus status;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            status = {
                m_state,
                m_recording_id,
                m_admitted_frame_count,
                m_submitted_frame_count,
                m_recording_drop_count + m_contention_drop_count.load(std::memory_order_relaxed),
                m_queue_count,
                m_queue.size(),
                0U,
                m_started_monotonic_ns,
                m_started_unix_epoch_ns,
                m_stopped_monotonic_ns,
                m_stopped_unix_epoch_ns,
                m_state == RecordingState::e_FINALIZED ? "finalized/" + m_recording_id : ""};
        }
        status.orphan_staging_count = m_store.getStagingCount();
        return status;
    }

   private:
    void restorePersistedArtifactLocked(const PersistedRecordingArtifact& artifact,
                                        const RecordingStartRequest& request,
                                        const std::string& canonical_start) {
        m_artifact_paths = artifact.paths;
        m_recording_id = request.recording_id;
        m_admitted_frame_count = 0U;
        m_submitted_frame_count = 0U;
        m_recording_drop_count = 0U;
        m_contention_drop_count.store(0U, std::memory_order_relaxed);
        m_queue_head = 0U;
        m_queue_tail = 0U;
        m_queue_count = 0U;
        for (std::shared_ptr<const CapturedFrame>& frame : m_queue) {
            frame.reset();
        }
        m_started_monotonic_ns = 0;
        m_started_unix_epoch_ns = 0;
        m_stopped_monotonic_ns = 0;
        m_stopped_unix_epoch_ns = 0;
        m_stop_request_id.clear();
        m_stop_canonical.clear();
        m_stop_reason = RecordingStopReason::e_APPLICATION_SHUTDOWN;
        m_has_identity = false;
        if (!canonical_start.empty()) {
            m_start_request_id = request.request_id;
            m_start_canonical = canonical_start;
        } else {
            m_start_request_id.clear();
            m_start_canonical.clear();
        }
        m_stop_requested = true;
        m_admitting.store(false, std::memory_order_release);
        m_state = artifact.finalized ? RecordingState::e_FINALIZED : RecordingState::e_FAULTED;
    }

    void beginFinalizationLocked(RecordingStopReason reason, const std::string& stop_evidence,
                                 const std::string& stop_request_id,
                                 const std::string& stop_canonical) {
        m_state = RecordingState::e_FINALIZING;
        m_admitting.store(false, std::memory_order_release);
        m_stop_requested = true;
        m_finalize_deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(m_config.finalize_timeout_ms);
        try {
            m_store.writeStopRequest(m_artifact_paths, stop_evidence);
            if (std::chrono::steady_clock::now() >= m_finalize_deadline) {
                throw std::runtime_error(
                    "Recording stop evidence exceeded its configured finalize timeout.");
            }
            m_stop_reason = reason;
            m_stop_request_id = stop_request_id;
            m_stop_canonical = stop_canonical;
        } catch (...) {
            m_state = RecordingState::e_FAULTED;
            throw;
        }
    }

    void requireFinalizeWithinDeadline() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop_requested && std::chrono::steady_clock::now() >= m_finalize_deadline) {
            throw std::runtime_error("Recording finalization exceeded its configured timeout.");
        }
    }

    std::chrono::steady_clock::time_point getFinalizeDeadline() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_finalize_deadline;
    }

    void finalizeArtifacts() {
        requireFinalizeWithinDeadline();
        m_writer->finalize();
        requireFinalizeWithinDeadline();
        m_sidecar->finalize();
        requireFinalizeWithinDeadline();
        const std::chrono::steady_clock::time_point deadline = getFinalizeDeadline();
        const RecordingArtifactDigest video = calculateRecordingArtifactDigest(
            m_artifact_paths.staging_directory, "color.mp4", deadline);
        const RecordingArtifactDigest sidecar = calculateRecordingArtifactDigest(
            m_artifact_paths.staging_directory, "frames.jsonl", deadline);
        requireFinalizeWithinDeadline();

        FinalizedRecordingManifest manifest;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped_monotonic_ns = getMonotonicNowNs();
            m_stopped_unix_epoch_ns = getUnixNowNs();
            manifest = {
                m_recording_id,
                m_config.component_id,
                m_config.instance_id,
                m_config.device_id,
                m_config.sensor_frame,
                m_config.calibration_id,
                m_config.width,
                m_config.height,
                m_config.fps,
                m_started_monotonic_ns,
                m_started_unix_epoch_ns,
                m_stopped_monotonic_ns,
                m_stopped_unix_epoch_ns,
                m_admitted_frame_count,
                m_submitted_frame_count,
                m_recording_drop_count + m_contention_drop_count.load(std::memory_order_relaxed),
                m_first_identity,
                m_last_identity,
                m_has_identity,
                m_start_request_id,
                m_stop_request_id,
                m_stop_reason};
        }
        m_store.writeFinalizedManifest(
            m_artifact_paths, serializeFinalizedRecordingManifest(manifest, video, sidecar));
        requireFinalizeWithinDeadline();
        m_store.activateFinalized(m_artifact_paths);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = RecordingState::e_FINALIZED;
    }

    void runWorker() noexcept {
        try {
            while (true) {
                std::shared_ptr<const CapturedFrame> frame;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    if (!m_stop_requested && m_queue_count == 0U) {
                        m_frame_available.wait_until(lock, m_duration_deadline, [this] {
                            return m_queue_count != 0U || m_stop_requested;
                        });
                    }
                    if (!m_stop_requested &&
                        std::chrono::steady_clock::now() >= m_duration_deadline) {
                        beginFinalizationLocked(
                            RecordingStopReason::e_MAX_DURATION,
                            serializeInternalStopEvidence(m_recording_id,
                                                          RecordingStopReason::e_MAX_DURATION),
                            "", "");
                    }
                    if (m_queue_count == 0U && m_stop_requested) {
                        break;
                    }
                    frame = std::move(m_queue[m_queue_head]);
                    m_queue_head = (m_queue_head + 1U) % m_queue.size();
                    --m_queue_count;
                }
                requireFinalizeWithinDeadline();
                const FrameSnapshot& snapshot = frame->getSnapshot();
                const std::optional<VideoFrameView> color = frame->getColorFrameView();
                if (!color.has_value() ||
                    color->identity.capture_generation != snapshot.identity.capture_generation ||
                    color->identity.frame_number != snapshot.identity.frame_number ||
                    (m_has_identity && !isStrictlyAfter(snapshot.identity, m_last_identity))) {
                    throw std::runtime_error(
                        "Recording frame identity is invalid or out of order.");
                }
                if (m_has_identity &&
                    snapshot.identity.capture_generation != m_last_identity.capture_generation) {
                    throw std::runtime_error("Recording capture generation changed.");
                }
                const std::uint64_t index = m_writer->writeFrame(*color);
                m_sidecar->append({index, static_cast<std::int64_t>(index), snapshot.identity});
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_has_identity) {
                        m_first_identity = snapshot.identity;
                    }
                    m_last_identity = snapshot.identity;
                    m_has_identity = true;
                    ++m_submitted_frame_count;
                }
            }
            finalizeArtifacts();
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = RecordingState::e_FAULTED;
            m_admitting.store(false, std::memory_order_release);
            m_stop_requested = true;
        }
    }

    RecordingManagerConfig m_config;
    RecordingStore m_store;
    RecordingArtifactPaths m_artifact_paths;
    std::vector<std::shared_ptr<const CapturedFrame>> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_frame_available;
    std::thread m_worker;
    std::unique_ptr<ColorVideoWriter> m_writer;
    std::unique_ptr<RecordingSidecarWriter> m_sidecar;
    std::string m_recording_id;
    std::string m_start_request_id;
    std::string m_start_canonical;
    std::string m_stop_request_id;
    std::string m_stop_canonical;
    FrameIdentity m_first_identity;
    FrameIdentity m_last_identity;
    std::size_t m_queue_head{0U};
    std::size_t m_queue_tail{0U};
    std::size_t m_queue_count{0U};
    std::uint64_t m_admitted_frame_count{0U};
    std::uint64_t m_submitted_frame_count{0U};
    std::uint64_t m_recording_drop_count{0U};
    std::atomic<std::uint64_t> m_contention_drop_count{0U};
    RecordingState m_state{RecordingState::e_IDLE};
    std::atomic<bool> m_admitting{false};
    std::int64_t m_started_monotonic_ns{0};
    std::int64_t m_started_unix_epoch_ns{0};
    std::int64_t m_stopped_monotonic_ns{0};
    std::int64_t m_stopped_unix_epoch_ns{0};
    std::chrono::steady_clock::time_point m_duration_deadline;
    std::chrono::steady_clock::time_point m_finalize_deadline;
    RecordingStopReason m_stop_reason{RecordingStopReason::e_APPLICATION_SHUTDOWN};
    bool m_has_identity{false};
    bool m_stop_requested{false};
};

RecordingManager::RecordingManager(RecordingManagerConfig config)
    : m_p_impl(std::make_unique<Impl>(std::move(config))) {}
RecordingManager::~RecordingManager() = default;
void RecordingManager::start(const RecordingStartRequest& request) {
    m_p_impl->startOrReplay(request);
}
RecordingStartResult RecordingManager::startOrReplay(const RecordingStartRequest& request) {
    return m_p_impl->startOrReplay(request);
}
bool RecordingManager::trySubmitFrame(std::shared_ptr<const CapturedFrame> frame) noexcept {
    return m_p_impl->trySubmitFrame(std::move(frame));
}
void RecordingManager::finalize() { m_p_impl->finalize(); }
RecordingStopResult RecordingManager::finalizeOrReplay(const RecordingStartRequest& request) {
    return m_p_impl->finalizeOrReplay(request);
}
RecordingStatus RecordingManager::getStatus() const { return m_p_impl->getStatus(); }

}  // namespace nodus_vision
