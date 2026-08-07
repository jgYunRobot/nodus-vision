/** @file test_recording_manager.cpp @brief bounded recording worker를 검증한다. */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "recording_manager.hpp"

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
    return {root, 4U, 64, 64, 30, 100000, "front_optical", "front_v1"};
}

}  // namespace

TEST(RecordingManager, DrainsImmutableRgbFramesIntoStagingArtifact) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-001", "episode-0001-front", ""});
    EXPECT_TRUE(manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, 1U)));
    EXPECT_TRUE(manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, 2U)));
    manager.finalize();
    const RecordingStatus status = manager.getStatus();
    EXPECT_EQ(status.state, RecordingState::e_FINALIZED);
    EXPECT_EQ(status.admitted_frame_count, 2U);
    EXPECT_EQ(status.submitted_frame_count, 2U);
    const std::filesystem::path artifact = directory.getPath() / ".staging" / "episode-0001-front";
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "color.mp4"));
    EXPECT_TRUE(std::filesystem::is_regular_file(artifact / "frames.jsonl"));
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
    EXPECT_THROW(manager.startOrReplay({"start-001", "different-id", ""}), std::runtime_error);
    manager.finalize();
    EXPECT_EQ(manager.startOrReplay(request), RecordingStartResult::e_REPLAYED);
}

TEST(RecordingManager, FaultsRatherThanMixingCaptureGenerations) {
    TemporaryRecordingDirectory directory;
    RecordingManager manager(makeConfig(directory.getPath()));
    manager.start({"start-001", "episode-0001-front", ""});
    EXPECT_TRUE(manager.trySubmitFrame(std::make_shared<RgbFrame>(1U, 1U)));
    EXPECT_TRUE(manager.trySubmitFrame(std::make_shared<RgbFrame>(2U, 1U)));
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

}  // namespace nodus_vision
