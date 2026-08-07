/**
 * @file recording_manifest.hpp
 * @brief append-only recording frame sidecar contract를 제공한다.
 */

#ifndef NODUS_VISION_RECORDING_RECORDING_MANIFEST_HPP_
#define NODUS_VISION_RECORDING_RECORDING_MANIFEST_HPP_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nodus_vision/camera_contracts.hpp>
#include <string>

namespace nodus_vision {

/** @brief one encoded input frame의 durable sidecar identity다. */
struct RecordingFrameEntry {
    std::uint64_t video_frame_index{0U};
    std::int64_t video_pts{0};
    FrameIdentity identity;
};

/** @brief immutable artifact file의 verified metadata다. */
struct RecordingArtifactDigest {
    std::string relative_path;
    std::uint64_t size_bytes{0U};
    std::string sha256_hex;
};

/** @brief immutable finalized recording manifest의 typed metadata다. */
struct FinalizedRecordingManifest {
    std::string recording_id;
    std::string component_id;
    std::string instance_id;
    std::string device_id;
    std::string sensor_frame;
    std::string calibration_id;
    int width{0};
    int height{0};
    int fps{0};
    std::int64_t started_monotonic_ns{0};
    std::int64_t started_unix_epoch_ns{0};
    std::int64_t stopped_monotonic_ns{0};
    std::int64_t stopped_unix_epoch_ns{0};
    std::uint64_t admitted_frame_count{0U};
    std::uint64_t submitted_frame_count{0U};
    std::uint64_t recording_drop_count{0U};
    FrameIdentity first_frame;
    FrameIdentity last_frame;
    bool has_frames{false};
    std::string start_request_id;
    std::string stop_request_id;
};

/** @brief regular non-symlink file의 bounded SHA-256 metadata를 계산한다. */
RecordingArtifactDigest calculateRecordingArtifactDigest(const std::filesystem::path& root,
                                                         const std::string& relative_path);
/** @brief immutable finalized manifest를 compact JSON으로 serialize한다. */
std::string serializeFinalizedRecordingManifest(const FinalizedRecordingManifest& manifest,
                                                const RecordingArtifactDigest& video,
                                                const RecordingArtifactDigest& sidecar);

/** @brief one recording의 compact versioned frames.jsonl writer다. */
class RecordingSidecarWriter {
   public:
    RecordingSidecarWriter(std::filesystem::path output_path, std::string recording_id,
                           std::string sensor_frame, std::string calibration_id);
    ~RecordingSidecarWriter();
    RecordingSidecarWriter(const RecordingSidecarWriter&) = delete;
    RecordingSidecarWriter& operator=(const RecordingSidecarWriter&) = delete;

    /** @brief strictly ordered encoded frame entry one line을 append한다. */
    void append(const RecordingFrameEntry& entry);
    /** @brief buffered sidecar contents를 flush하고 close한다. */
    void finalize();
    /** @return successfully appended line count다. */
    std::uint64_t getEntryCount() const noexcept;

   private:
    std::ofstream m_output;
    std::string m_recording_id;
    std::string m_sensor_frame;
    std::string m_calibration_id;
    FrameIdentity m_last_identity;
    std::uint64_t m_entry_count{0U};
    bool m_has_identity{false};
    bool m_finalized{false};
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_RECORDING_MANIFEST_HPP_
