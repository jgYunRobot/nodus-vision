/** @file recording_manifest.cpp @brief append-only frame sidecar를 구현한다. */

#include "recording_manifest.hpp"

#include <array>
#include <boost/json.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

#include "recording_contracts.hpp"

namespace nodus_vision {
namespace {

bool isStrictlyAfter(const FrameIdentity& candidate, const FrameIdentity& previous) noexcept {
    return candidate.capture_generation > previous.capture_generation ||
           (candidate.capture_generation == previous.capture_generation &&
            candidate.frame_number > previous.frame_number);
}

boost::json::object serializeFrameIdentity(const FrameIdentity& identity) {
    boost::json::object result;
    result["capture_generation"] = identity.capture_generation;
    result["frame_number"] = identity.frame_number;
    result["capture_timestamp_ns"] = identity.capture_timestamp_ns;
    result["capture_unix_epoch_ns"] = identity.capture_unix_epoch_ns;
    return result;
}

}  // namespace

RecordingArtifactDigest calculateRecordingArtifactDigest(
    const std::filesystem::path& root, const std::string& relative_path,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    const std::filesystem::path relative(relative_path);
    const std::filesystem::path path = root / relative;
    if (relative.empty() || relative.is_absolute() || relative.has_parent_path() ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_regular_file(path)) {
        throw std::invalid_argument("Recording artifact path is unsafe.");
    }
    std::ifstream input(path, std::ios::binary);
    AVSHA* p_sha = av_sha_alloc();
    if (!input || p_sha == nullptr || av_sha_init(p_sha, 256) != 0) {
        av_freep(&p_sha);
        throw std::runtime_error("Recording artifact digest cannot initialize.");
    }
    std::array<std::uint8_t, 65536U> buffer{};
    while (input.good()) {
        if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline) {
            av_freep(&p_sha);
            throw std::runtime_error("Recording artifact digest exceeded finalize timeout.");
        }
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize size = input.gcount();
        if (size > 0) {
            av_sha_update(p_sha, buffer.data(), static_cast<std::size_t>(size));
        }
    }
    if (!input.eof()) {
        av_freep(&p_sha);
        throw std::runtime_error("Recording artifact digest read failed.");
    }
    std::array<std::uint8_t, 32U> digest{};
    av_sha_final(p_sha, digest.data());
    av_freep(&p_sha);
    std::ostringstream hex;
    for (const std::uint8_t byte : digest) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
    }
    return {relative_path, std::filesystem::file_size(path), hex.str()};
}

std::string serializeFinalizedRecordingManifest(const FinalizedRecordingManifest& manifest,
                                                const RecordingArtifactDigest& video,
                                                const RecordingArtifactDigest& sidecar) {
    if (!isRecordingIdValid(manifest.recording_id) || manifest.component_id.empty() ||
        manifest.instance_id.empty() || manifest.device_id.empty() ||
        manifest.sensor_frame.empty() || manifest.calibration_id.empty() || manifest.width <= 0 ||
        manifest.height <= 0 || manifest.fps <= 0 ||
        manifest.started_monotonic_ns > manifest.stopped_monotonic_ns ||
        manifest.started_unix_epoch_ns > manifest.stopped_unix_epoch_ns ||
        manifest.submitted_frame_count > manifest.admitted_frame_count ||
        video.relative_path != "color.mp4" || sidecar.relative_path != "frames.jsonl") {
        throw std::invalid_argument("Recording manifest values are invalid.");
    }
    auto make_artifact = [](const RecordingArtifactDigest& digest) {
        boost::json::object artifact;
        artifact["path"] = digest.relative_path;
        artifact["size_bytes"] = digest.size_bytes;
        artifact["sha256"] = digest.sha256_hex;
        return artifact;
    };
    boost::json::object root;
    root["schema_version"] = 1;
    root["state"] = "finalized";
    root["recording_id"] = manifest.recording_id;
    root["component_id"] = manifest.component_id;
    root["instance_id"] = manifest.instance_id;
    root["device_id"] = manifest.device_id;
    root["capture_generation"] = manifest.has_frames ? manifest.first_frame.capture_generation : 0U;
    root["sensor_frame"] = manifest.sensor_frame;
    root["calibration_id"] = manifest.calibration_id;
    boost::json::object profile;
    profile["width"] = manifest.width;
    profile["height"] = manifest.height;
    profile["fps"] = manifest.fps;
    profile["input_pixel_format"] = "rgb24";
    root["profile"] = std::move(profile);
    boost::json::object video_details;
    video_details["container"] = "mp4";
    video_details["codec"] = "h264";
    video_details["encoder"] = "libx264";
    video_details["pixel_format"] = "yuv420p";
    video_details["time_base_num"] = 1;
    video_details["time_base_den"] = manifest.fps;
    root["video"] = std::move(video_details);
    root["started_monotonic_ns"] = manifest.started_monotonic_ns;
    root["started_unix_epoch_ns"] = manifest.started_unix_epoch_ns;
    root["stopped_monotonic_ns"] = manifest.stopped_monotonic_ns;
    root["stopped_unix_epoch_ns"] = manifest.stopped_unix_epoch_ns;
    root["admitted_frame_count"] = manifest.admitted_frame_count;
    root["submitted_frame_count"] = manifest.submitted_frame_count;
    root["recording_drop_count"] = manifest.recording_drop_count;
    root["first_frame"] = manifest.has_frames
                              ? boost::json::value(serializeFrameIdentity(manifest.first_frame))
                              : boost::json::value(nullptr);
    root["last_frame"] = manifest.has_frames
                             ? boost::json::value(serializeFrameIdentity(manifest.last_frame))
                             : boost::json::value(nullptr);
    boost::json::array artifacts;
    artifacts.emplace_back(make_artifact(video));
    artifacts.emplace_back(make_artifact(sidecar));
    root["artifacts"] = std::move(artifacts);
    root["start_request_id"] = manifest.start_request_id;
    root["stop_request_id"] = manifest.stop_request_id.empty()
                                  ? boost::json::value(nullptr)
                                  : boost::json::value(manifest.stop_request_id);
    root["stop_reason"] = getRecordingStopReasonName(manifest.stop_reason);
    return boost::json::serialize(root);
}

RecordingSidecarWriter::RecordingSidecarWriter(std::filesystem::path output_path,
                                               std::string recording_id, std::string sensor_frame,
                                               std::string calibration_id)
    : m_output(std::move(output_path), std::ios::out | std::ios::binary),
      m_recording_id(std::move(recording_id)),
      m_sensor_frame(std::move(sensor_frame)),
      m_calibration_id(std::move(calibration_id)) {
    if (!isRecordingIdValid(m_recording_id) || m_sensor_frame.empty() || m_calibration_id.empty() ||
        !m_output.is_open()) {
        throw std::invalid_argument("Recording sidecar configuration is invalid.");
    }
}

RecordingSidecarWriter::~RecordingSidecarWriter() {
    if (!m_finalized) {
        m_output.close();
    }
}

void RecordingSidecarWriter::append(const RecordingFrameEntry& entry) {
    if (m_finalized || entry.video_frame_index != m_entry_count || entry.video_pts < 0 ||
        (m_has_identity && !isStrictlyAfter(entry.identity, m_last_identity))) {
        throw std::invalid_argument("Recording sidecar entry violates its immutable order.");
    }
    boost::json::object line;
    line["schema_version"] = 1;
    line["recording_id"] = m_recording_id;
    line["video_frame_index"] = entry.video_frame_index;
    line["video_pts"] = entry.video_pts;
    line["capture_generation"] = entry.identity.capture_generation;
    line["frame_number"] = entry.identity.frame_number;
    line["capture_timestamp_ns"] = entry.identity.capture_timestamp_ns;
    line["capture_unix_epoch_ns"] = entry.identity.capture_unix_epoch_ns;
    line["sensor_frame"] = m_sensor_frame;
    line["calibration_id"] = m_calibration_id;
    m_output << boost::json::serialize(line) << '\n';
    if (!m_output) {
        throw std::runtime_error("Recording sidecar append failed.");
    }
    m_last_identity = entry.identity;
    m_has_identity = true;
    ++m_entry_count;
}

void RecordingSidecarWriter::finalize() {
    if (m_finalized) {
        return;
    }
    m_output.flush();
    if (!m_output) {
        throw std::runtime_error("Recording sidecar flush failed.");
    }
    m_output.close();
    if (m_output.fail()) {
        throw std::runtime_error("Recording sidecar close failed.");
    }
    m_finalized = true;
}

std::uint64_t RecordingSidecarWriter::getEntryCount() const noexcept { return m_entry_count; }

}  // namespace nodus_vision
