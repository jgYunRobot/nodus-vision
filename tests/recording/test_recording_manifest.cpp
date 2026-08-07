/** @file test_recording_manifest.cpp @brief frames.jsonl append contract를 검증한다. */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "recording_manifest.hpp"

namespace nodus_vision {
namespace {

class TemporarySidecarDirectory {
   public:
    TemporarySidecarDirectory() {
        char template_path[] = "/tmp/nodus-vision-sidecar-XXXXXX";
        char* created = mkdtemp(template_path);
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary sidecar directory.");
        }
        m_path = created;
    }
    ~TemporarySidecarDirectory() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& getPath() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

RecordingFrameEntry makeEntry(std::uint64_t index, std::uint64_t frame_number) {
    RecordingFrameEntry entry;
    entry.video_frame_index = index;
    entry.video_pts = static_cast<std::int64_t>(index);
    entry.identity.capture_generation = 1U;
    entry.identity.frame_number = frame_number;
    entry.identity.capture_timestamp_ns = static_cast<std::int64_t>(frame_number * 100U);
    entry.identity.capture_unix_epoch_ns = static_cast<std::int64_t>(frame_number * 1000U);
    return entry;
}

}  // namespace

TEST(RecordingSidecarWriter, PersistsVersionedOrderedLinesWithFinalNewline) {
    TemporarySidecarDirectory directory;
    const std::filesystem::path sidecar = directory.getPath() / "frames.jsonl";
    {
        RecordingSidecarWriter writer(sidecar, "episode-0001-front", "front_optical", "front_v1");
        writer.append(makeEntry(0U, 42U));
        writer.append(makeEntry(1U, 43U));
        EXPECT_EQ(writer.getEntryCount(), 2U);
        writer.finalize();
    }
    std::ifstream input(sidecar, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(contents,
              "{\"schema_version\":1,\"recording_id\":\"episode-0001-front\",\"video_frame_index\":"
              "0,\"video_pts\":0,\"capture_generation\":1,\"frame_number\":42,\"capture_timestamp_"
              "ns\":4200,\"capture_unix_epoch_ns\":42000,\"sensor_frame\":\"front_optical\","
              "\"calibration_id\":\"front_v1\"}\n"
              "{\"schema_version\":1,\"recording_id\":\"episode-0001-front\",\"video_frame_index\":"
              "1,\"video_pts\":1,\"capture_generation\":1,\"frame_number\":43,\"capture_timestamp_"
              "ns\":4300,\"capture_unix_epoch_ns\":43000,\"sensor_frame\":\"front_optical\","
              "\"calibration_id\":\"front_v1\"}\n");
}

TEST(RecordingSidecarWriter, RejectsIndexAndIdentityGaps) {
    TemporarySidecarDirectory directory;
    RecordingSidecarWriter writer(directory.getPath() / "frames.jsonl", "episode-0001-front",
                                  "front_optical", "front_v1");
    writer.append(makeEntry(0U, 42U));
    EXPECT_THROW(writer.append(makeEntry(2U, 43U)), std::invalid_argument);
    EXPECT_THROW(writer.append(makeEntry(1U, 42U)), std::invalid_argument);
}

}  // namespace nodus_vision
