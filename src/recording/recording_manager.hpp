/** @file recording_manager.hpp @brief bounded Vision-owned recording worker를 제공한다. */

#ifndef NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_
#define NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nodus_vision/camera_adapter.hpp>
#include <stdexcept>
#include <string>

#include "recording_contracts.hpp"

namespace nodus_vision {

struct RecordingManagerConfig {
    std::filesystem::path root;
    std::size_t queue_capacity_frames{0U};
    int width{0};
    int height{0};
    int fps{0};
    int bit_rate_bps{0};
    int max_duration_ms{0};
    std::uint64_t minimum_free_bytes{0U};
    int finalize_timeout_ms{0};
    std::string preset;
    std::string tune;
    std::string sensor_frame;
    std::string calibration_id;
    std::string component_id;
    std::string device_id;
    std::string instance_id;
};

struct RecordingStatus {
    RecordingState state{RecordingState::e_IDLE};
    std::string recording_id;
    std::uint64_t admitted_frame_count{0U};
    std::uint64_t submitted_frame_count{0U};
    std::uint64_t recording_drop_count{0U};
    std::size_t queue_depth{0U};
    std::size_t queue_capacity{0U};
    std::size_t orphan_staging_count{0U};
    std::int64_t started_monotonic_ns{0};
    std::int64_t started_unix_epoch_ns{0};
    std::int64_t stopped_monotonic_ns{0};
    std::int64_t stopped_unix_epoch_ns{0};
    std::string finalized_artifact_reference;
};

enum class RecordingStartResult { e_STARTED, e_REPLAYED };
enum class RecordingStopResult { e_ACCEPTED, e_REPLAYED };

/** @brief requested recording identity가 active/persisted storage에 없음을 나타낸다. */
class RecordingNotFoundError final : public std::runtime_error {
   public:
    explicit RecordingNotFoundError(const std::string& message) : std::runtime_error(message) {}
};

/** @brief capture path와 FFmpeg writer를 bounded queue로 분리하는 sole lifecycle owner다. */
class RecordingManager {
   public:
    explicit RecordingManager(RecordingManagerConfig config);
    ~RecordingManager();
    RecordingManager(const RecordingManager&) = delete;
    RecordingManager& operator=(const RecordingManager&) = delete;

    /** @brief new staging artifact와 writer worker를 준비한다. */
    void start(const RecordingStartRequest& request);
    /** @brief exact request replay를 distinguish하며 start를 수행한다. */
    RecordingStartResult startOrReplay(const RecordingStartRequest& request);
    /** @brief capture thread를 기다리지 않고 immutable frame을 admission한다. */
    bool trySubmitFrame(std::shared_ptr<const CapturedFrame> frame) noexcept;
    /** @brief application shutdown 근거를 기록하고 worker finalize 완료를 기다린다. */
    void finalize();
    /** @brief stop을 durable하게 수락하고 worker에 비동기 finalize를 요청한다. */
    RecordingStopResult finalizeOrReplay(const RecordingStartRequest& request);
    /** @return recording owner의 consistent public counters다. */
    RecordingStatus getStatus() const;

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_
