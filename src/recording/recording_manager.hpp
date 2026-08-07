/** @file recording_manager.hpp @brief bounded Vision-owned recording worker를 제공한다. */

#ifndef NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_
#define NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nodus_vision/camera_adapter.hpp>
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
    std::string sensor_frame;
    std::string calibration_id;
};

struct RecordingStatus {
    RecordingState state{RecordingState::e_IDLE};
    std::string recording_id;
    std::uint64_t admitted_frame_count{0U};
    std::uint64_t submitted_frame_count{0U};
    std::uint64_t recording_drop_count{0U};
};

enum class RecordingStartResult { e_STARTED, e_REPLAYED };
enum class RecordingStopResult { e_FINALIZED, e_REPLAYED };

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
    /** @brief admission을 닫고 bounded queue를 drain한 staging artifact를 close한다. */
    void finalize();
    /** @brief exact stop replay를 distinguish하며 staging writer를 close한다. */
    RecordingStopResult finalizeOrReplay(const RecordingStartRequest& request);
    /** @return recording owner의 consistent public counters다. */
    RecordingStatus getStatus() const;

   private:
    class Impl;
    std::unique_ptr<Impl> m_p_impl;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_RECORDING_MANAGER_HPP_
