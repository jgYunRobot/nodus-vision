/** @file recording_manifest.cpp @brief append-only frame sidecar를 구현한다. */

#include "recording_manifest.hpp"

#include <boost/json.hpp>
#include <stdexcept>
#include <utility>

#include "recording_contracts.hpp"

namespace nodus_vision {
namespace {

bool isStrictlyAfter(const FrameIdentity& candidate, const FrameIdentity& previous) noexcept {
    return candidate.capture_generation > previous.capture_generation ||
           (candidate.capture_generation == previous.capture_generation &&
            candidate.frame_number > previous.frame_number);
}

}  // namespace

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
