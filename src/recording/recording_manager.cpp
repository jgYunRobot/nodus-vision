/** @file recording_manager.cpp @brief bounded recording worker를 구현한다. */

#include "recording_manager.hpp"

#include <atomic>
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

}  // namespace

class RecordingManager::Impl {
   public:
    explicit Impl(RecordingManagerConfig config)
        : m_config(std::move(config)),
          m_store(m_config.root),
          m_queue(m_config.queue_capacity_frames) {
        if (m_config.queue_capacity_frames == 0U || m_config.sensor_frame.empty() ||
            m_config.calibration_id.empty()) {
            throw std::invalid_argument("Recording manager configuration is invalid.");
        }
    }

    ~Impl() {
        try {
            finalize();
        } catch (...) {
        }
    }

    RecordingStartResult startOrReplay(const RecordingStartRequest& request) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string canonical = serializeRecordingStartRequest(request);
        if (request.request_id == m_start_request_id) {
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
        m_state = RecordingState::e_PREPARING;
        try {
            const RecordingArtifactPaths paths = m_store.createStaging(request);
            m_artifact_paths = paths;
            m_writer = std::make_unique<ColorVideoWriter>(
                ColorVideoWriterConfig{paths.staging_directory / "color.mp4", m_config.width,
                                       m_config.height, m_config.fps, m_config.bit_rate_bps});
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
            m_stop_requested = false;
            m_has_identity = false;
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
        bool faulted = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            faulted = m_state == RecordingState::e_FAULTED;
            if (!faulted && m_state != RecordingState::e_RECORDING &&
                m_state != RecordingState::e_FINALIZING) {
                return;
            }
            if (!faulted) {
                m_state = RecordingState::e_FINALIZING;
                m_admitting.store(false, std::memory_order_release);
                m_stop_requested = true;
            }
        }
        m_frame_available.notify_one();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (faulted || m_state == RecordingState::e_FAULTED) {
            return;
        }
        try {
            m_writer->finalize();
            m_sidecar->finalize();
            const RecordingArtifactDigest video =
                calculateRecordingArtifactDigest(m_artifact_paths.staging_directory, "color.mp4");
            const RecordingArtifactDigest sidecar = calculateRecordingArtifactDigest(
                m_artifact_paths.staging_directory, "frames.jsonl");
            m_store.writeFinalizedManifest(
                m_artifact_paths, serializeFinalizedRecordingManifest(
                                      m_recording_id, m_submitted_frame_count, video, sidecar));
            m_store.activateFinalized(m_artifact_paths);
            m_state = RecordingState::e_FINALIZED;
        } catch (...) {
            m_state = RecordingState::e_FAULTED;
            throw;
        }
    }

    RecordingStopResult finalizeOrReplay(const RecordingStartRequest& request) {
        if (!isRecordingRequestIdValid(request.request_id) ||
            !isRecordingIdValid(request.recording_id)) {
            throw std::invalid_argument("Recording stop request has an unsafe identity.");
        }
        const std::string canonical = "{\"schema_version\":1,\"request_id\":\"" +
                                      request.request_id + "\",\"recording_id\":\"" +
                                      request.recording_id + "\"}";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (request.request_id == m_stop_request_id) {
                if (canonical == m_stop_canonical) {
                    return RecordingStopResult::e_REPLAYED;
                }
                throw std::runtime_error(
                    "Recording stop request ID was reused with different content.");
            }
            if (request.recording_id != m_recording_id) {
                throw std::runtime_error("Recording is unknown.");
            }
        }
        finalize();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop_request_id = request.request_id;
        m_stop_canonical = canonical;
        return RecordingStopResult::e_FINALIZED;
    }

    RecordingStatus getStatus() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_state, m_recording_id, m_admitted_frame_count, m_submitted_frame_count,
                m_recording_drop_count + m_contention_drop_count.load(std::memory_order_relaxed)};
    }

   private:
    void runWorker() noexcept {
        try {
            while (true) {
                std::shared_ptr<const CapturedFrame> frame;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_frame_available.wait(
                        lock, [this] { return m_queue_count != 0U || m_stop_requested; });
                    if (m_queue_count == 0U) {
                        return;
                    }
                    frame = std::move(m_queue[m_queue_head]);
                    m_queue_head = (m_queue_head + 1U) % m_queue.size();
                    --m_queue_count;
                }
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
                    m_last_identity = snapshot.identity;
                    m_has_identity = true;
                    ++m_submitted_frame_count;
                }
            }
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
