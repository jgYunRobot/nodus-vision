/** @file test_recording_manifest.cpp @brief frames.jsonl append contract를 검증한다. */

#include <gtest/gtest.h>

#include <boost/json.hpp>
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

TEST(RecordingManifest, CalculatesBoundedSha256ForRegularArtifact) {
    TemporarySidecarDirectory directory;
    const std::filesystem::path artifact = directory.getPath() / "artifact.bin";
    {
        std::ofstream output(artifact, std::ios::binary);
        output << "abc";
    }
    const RecordingArtifactDigest digest =
        calculateRecordingArtifactDigest(directory.getPath(), "artifact.bin");
    EXPECT_EQ(digest.size_bytes, 3U);
    EXPECT_EQ(digest.sha256_hex,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(RecordingManifest, SerializesFinalizedIdentityCountersAndArtifactDigests) {
    FinalizedRecordingManifest manifest{"episode-0001-front",
                                        "camera.front",
                                        "vision-test-1",
                                        "front_d435",
                                        "front_optical",
                                        "front_v1",
                                        64,
                                        64,
                                        30,
                                        10,
                                        20,
                                        30,
                                        40,
                                        3U,
                                        2U,
                                        1U,
                                        {1U, 41U, 101, 201},
                                        {1U, 42U, 102, 202},
                                        true,
                                        "start-001",
                                        "stop-001",
                                        RecordingStopReason::e_REQUESTED};
    const RecordingArtifactDigest video{
        "color.mp4", 123U, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
    const RecordingArtifactDigest sidecar{
        "frames.jsonl", 456U, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
    const boost::json::object result =
        boost::json::parse(serializeFinalizedRecordingManifest(manifest, video, sidecar))
            .as_object();
    EXPECT_EQ(result.at("state").as_string(), "finalized");
    EXPECT_EQ(result.at("component_id").as_string(), "camera.front");
    EXPECT_EQ(result.at("profile").as_object().at("width").as_int64(), 64);
    EXPECT_EQ(result.at("submitted_frame_count").to_number<std::uint64_t>(), 2U);
    EXPECT_EQ(result.at("first_frame").as_object().at("frame_number").to_number<std::uint64_t>(),
              41U);
    EXPECT_EQ(result.at("artifacts").as_array().size(), 2U);
    EXPECT_EQ(result.at("stop_reason").as_string(), "requested");
}

}  // namespace nodus_vision
