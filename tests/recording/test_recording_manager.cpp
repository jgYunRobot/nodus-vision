/** @file test_recording_manager.cpp @brief bounded recording worker를 검증한다. */

#include <gtest/gtest.h>

#include <boost/json.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "recording_manager.hpp"
#include "recording_store.hpp"

namespace nodus_vision {
namespace {

class TemporaryRecordingDirectory {
   public:
    TemporaryRecordingDirectory() {
        char template_path[] = "/tmp/nodus-vision-manager-XXXXXX";
        char* created = mkdtemp(template_path);
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary recording directory.");
        }
        m_path = created;
    }
    ~TemporaryRecordingDirectory() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& getPath() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

class RgbFrame final : public CapturedFrame, public std::enable_shared_from_this<RgbFrame> {
   public:
    RgbFrame(std::uint64_t generation, std::uint64_t frame_number) : m_pixels(64U * 64U * 3U, 7U) {
        m_snapshot.identity.capture_generation = generation;
        m_snapshot.identity.frame_number = frame_number;
        m_snapshot.identity.capture_timestamp_ns = static_cast<std::int64_t>(frame_number);
        m_snapshot.identity.capture_unix_epoch_ns = static_cast<std::int64_t>(frame_number * 10U);
    }
    const FrameSnapshot& getSnapshot() const noexcept override { return m_snapshot; }
    std::optional<VideoFrameView> getColorFrameView() const override {
        const std::shared_ptr<const RgbFrame> owner = shared_from_this();
        return VideoFrameView{64,
                              64,
                              64 * 3,
                              PixelFormat::e_RGB8,
                              std::shared_ptr<const void>(owner, static_cast<const void*>(this)),
                              m_pixels.data(),
                              m_snapshot.identity};
    }
    std::optional<VideoFrameView> getDepthPreviewFrameView() const override { return std::nullopt; }
    std::optional<PixelPointResult> queryPixelPoint(int, int) const override {
        return std::nullopt;
    }
    RoiDepthResult queryDepthInRoi(int, int, int, int) const override { return {}; }
    PointCloudSnapshot buildPointCloudSnapshot(std::size_t, int) const override { return {}; }

   private:
    FrameSnapshot m_snapshot;
    std::vector<std::uint8_t> m_pixels;
};

RecordingManagerConfig makeConfig(const std::filesystem::path& root) {
    return {root,
            4U,
            64,
            64,
            30,
            100000,
            10000,
            1U,
            1000,
            "veryfast",
            "zerolatency",
            "front_optical",
            "front_v1",
            "camera.front",
            "front_d435",
            "vision-test-1"};
}

bool submitFrameEventually(RecordingManager& manager, std::uint64_t generation,
                           std::uint64_t frame_number) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (manager.trySubmitFrame(std::make_shared<RgbFrame>(generation, frame_number))) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool waitForState(RecordingManager& manager, RecordingState expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (manager.getStatus().state == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return manager.getStatus().state == expected;
}

boost::json::object readJsonObject(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    return boost::json::parse(
               std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()))
        .as_object();
}

}  // namespace

TEST(RecordingManager, DrainsImmutableRgbFramesIntoStagingArtifact) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-001", "episode-0001-front", ""});
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
    EXPECT_TRUE(manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, 2U)));
    EXPECT_EQ(manager.finalizeOrReplay({"stop-001", "episode-0001-front", ""}),
              RecordingStopResult::e_ACCEPTED);
    manager.finalize();
    const RecordingStatus status = manager.getStatus();
    EXPECT_EQ(status.state, RecordingState::e_FINALIZED);
    EXPECT_EQ(status.admitted_frame_count, 2U);
    EXPECT_EQ(status.submitted_frame_count, 2U);
    const std::filesystem::path artifact = directory.getPath() / "finalized" / "episode-0001-front";
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "color.mp4"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "frames.jsonl"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "recording_manifest.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "stop_request.json"));
    EXPECT_EQ(manager.finalizeOrReplay({"stop-001", "episode-0001-front", ""}),
              RecordingStopResult::e_REPLAYED);
}

TEST(RecordingManager, RejectsFrameWhenNotRecording) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    EXPECT_FALSE(manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, 1U)));
}

TEST(RecordingManager, ReplaysOnlyTheExactAcceptedStartRequest) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    const RecordingStartRequest request{"start-001", "episode-0001-front", ""};
    EXPECT_EQ(manager.startOrReplay(request), RecordingStartResult::e_STARTED);
    EXPECT_EQ(manager.startOrReplay(request), RecordingStartResult::e_REPLAYED);
    EXPECT_THROW(manager.startOrReplay({"start-001", "episode-0001-front", "{\"changed\":true}"}),
                 std::runtime_error);
    EXPECT_THROW(manager.startOrReplay({"start-001", "different-id", ""}), std::runtime_error);
    manager.finalize();
    EXPECT_EQ(manager.startOrReplay(request), RecordingStartResult::e_REPLAYED);
}

TEST(RecordingManager, FaultsRatherThanMixingCaptureGenerations) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-001", "episode-0001-front", ""});
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
    bool first_frame_submitted = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (manager.getStatus().submitted_frame_count == 1U) {
            first_frame_submitted = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(first_frame_submitted);
    ASSERT_TRUE(submitFrameEventually(manager, 2U, 1U));
    manager.finalize();
    EXPECT_EQ(manager.getStatus().state, RecordingState::e_FAULTED);
}

TEST(RecordingManager, DropsOverflowWithoutBlockingCaptureAdmission) {
    TemporaryRecordingDirectory directory;
    RecordingManagerConfig config = makeConfig(directory.getPath());
    config.queue_capacity_frames = 1U;
    RecordingManager manager(std::move(config));
    manager.start({"start-001", "episode-0001-front", ""});
    std::uint64_t rejected = 0U;
    for (std::uint64_t frame_number = 1U; frame_number <= 100U; ++frame_number) {
        if (!manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, frame_number))) {
            ++rejected;
        }
    }
    manager.finalize();
    const RecordingStatus status = manager.getStatus();
    EXPECT_EQ(status.state, RecordingState::e_FINALIZED);
    EXPECT_GT(rejected, 0U);
    EXPECT_EQ(status.recording_drop_count, rejected);
}

TEST(RecordingManager, RejectsStartBelowConfiguredFreeSpaceReserve) {
    TemporaryRecordingDirectory directory;
    RecordingManagerConfig config = makeConfig(directory.getPath());
    const std::filesystem::space_info space = std::filesystem::space(directory.getPath());
    config.minimum_free_bytes = space.available + 1U;
    RecordingManager manager(std::move(config));
    EXPECT_THROW(manager.start({"start-001", "episode-0001-front", ""}), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(directory.getPath() / ".staging" / "episode-0001-front"));
}

TEST(RecordingManager, FinalizesAutomaticallyAtConfiguredMaximumDuration) {
    TemporaryRecordingDirectory directory;
    RecordingManagerConfig config = makeConfig(directory.getPath());
    config.max_duration_ms = 100;
    RecordingManager manager(std::move(config));
    manager.start({"start-001", "episode-0001-front", ""});
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
    ASSERT_TRUE(waitForState(manager, RecordingState::e_FINALIZED));
    const std::filesystem::path artifact = directory.getPath() / "finalized" / "episode-0001-front";
    EXPECT_EQ(readJsonObject(artifact / "stop_request.json").at("stop_reason"), "max_duration");
    const boost::json::object manifest = readJsonObject(artifact / "recording_manifest.json");
    EXPECT_EQ(manifest.at("stop_reason"), "max_duration");
    EXPECT_TRUE(manifest.at("stop_request_id").is_null());
}

TEST(RecordingManager, PersistsApplicationShutdownReasonWithoutInventingRequestId) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-001", "episode-0001-front", ""});
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
    manager.finalize();
    ASSERT_EQ(manager.getStatus().state, RecordingState::e_FINALIZED);
    const std::filesystem::path artifact = directory.getPath() / "finalized" / "episode-0001-front";
    EXPECT_EQ(readJsonObject(artifact / "stop_request.json").at("stop_reason"),
              "application_shutdown");
    const boost::json::object manifest = readJsonObject(artifact / "recording_manifest.json");
    EXPECT_EQ(manifest.at("stop_reason"), "application_shutdown");
    EXPECT_TRUE(manifest.at("stop_request_id").is_null());
}

TEST(RecordingManager, RecoversExactRequestReplayFromFinalizedArtifactAfterRestart) {
    TemporaryRecordingDirectory directory;
    const RecordingStartRequest start{"start-001", "episode-0001-front", ""};
    const RecordingStartRequest stop{"stop-001", "episode-0001-front", ""};
    {
        RecordingManager manager(makeConfig(directory.getPath()));
        EXPECT_EQ(manager.startOrReplay(start), RecordingStartResult::e_STARTED);
        ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
        EXPECT_EQ(manager.finalizeOrReplay(stop), RecordingStopResult::e_ACCEPTED);
        manager.finalize();
        ASSERT_EQ(manager.getStatus().state, RecordingState::e_FINALIZED);
    }
    RecordingManager recovered(makeConfig(directory.getPath()));
    EXPECT_EQ(recovered.startOrReplay(start), RecordingStartResult::e_REPLAYED);
    EXPECT_EQ(recovered.getStatus().state, RecordingState::e_FINALIZED);
    EXPECT_EQ(recovered.finalizeOrReplay(stop), RecordingStopResult::e_REPLAYED);
    EXPECT_THROW(recovered.startOrReplay({"start-002", "episode-0001-front", "{\"changed\":true}"}),
                 std::runtime_error);
}

TEST(RecordingManager, ExposesPersistedCrashStagingAsFaultedWithoutStartingWriter) {
    TemporaryRecordingDirectory directory;
    const RecordingStartRequest start{"start-001", "episode-0001-front", ""};
    RecordingStore store(directory.getPath());
    store.createStaging(start);
    RecordingManager recovered(makeConfig(directory.getPath()));
    EXPECT_EQ(recovered.startOrReplay(start), RecordingStartResult::e_REPLAYED);
    EXPECT_EQ(recovered.getStatus().state, RecordingState::e_FAULTED);
    EXPECT_FALSE(recovered.trySubmitFrame(std::make_shared<RgbFrame>(1U, 1U)));
    EXPECT_FALSE(std::filesystem::exists(directory.getPath() / "finalized" / "episode-0001-front"));
}

TEST(RecordingManager, IsolatesStopLedgerAndTimestampsAcrossConsecutiveRecordings) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-shared", "episode-0001-front", ""});
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 1U));
    EXPECT_EQ(manager.finalizeOrReplay({"stop-shared", "episode-0001-front", ""}),
              RecordingStopResult::e_ACCEPTED);
    manager.finalize();
    ASSERT_EQ(manager.getStatus().state, RecordingState::e_FINALIZED);

    EXPECT_EQ(manager.startOrReplay({"start-shared", "episode-0002-front", ""}),
              RecordingStartResult::e_STARTED);
    const RecordingStatus second_started = manager.getStatus();
    EXPECT_EQ(second_started.recording_id, "episode-0002-front");
    EXPECT_EQ(second_started.stopped_monotonic_ns, 0);
    EXPECT_EQ(second_started.stopped_unix_epoch_ns, 0);
    ASSERT_TRUE(submitFrameEventually(manager, 1U, 2U));
    EXPECT_EQ(manager.finalizeOrReplay({"stop-shared", "episode-0002-front", ""}),
              RecordingStopResult::e_ACCEPTED);
    manager.finalize();

    const boost::json::object first_manifest = readJsonObject(
        directory.getPath() / "finalized" / "episode-0001-front" / "recording_manifest.json");
    const boost::json::object second_manifest = readJsonObject(
        directory.getPath() / "finalized" / "episode-0002-front" / "recording_manifest.json");
    EXPECT_EQ(first_manifest.at("recording_id"), "episode-0001-front");
    EXPECT_EQ(second_manifest.at("recording_id"), "episode-0002-front");
    EXPECT_EQ(first_manifest.at("stop_request_id"), "stop-shared");
    EXPECT_EQ(second_manifest.at("stop_request_id"), "stop-shared");
    EXPECT_EQ(second_manifest.at("stop_reason"), "requested");
    EXPECT_EQ(manager.finalizeOrReplay({"stop-shared", "episode-0001-front", ""}),
              RecordingStopResult::e_REPLAYED);
}

}  // namespace nodus_vision
