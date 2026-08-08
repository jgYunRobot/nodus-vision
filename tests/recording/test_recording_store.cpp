/** @file test_recording_store.cpp @brief staging artifact store를 검증한다. */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include "recording_store.hpp"

namespace nodus_vision {
namespace {

class TemporaryRecordingRoot {
   public:
    TemporaryRecordingRoot() {
        char template_path[] = "/tmp/nodus-vision-recording-XXXXXX";
        char* created = mkdtemp(template_path);
        if (created == nullptr) {
            throw std::runtime_error("Cannot create temporary recording root.");
        }
        m_path = created;
    }
    ~TemporaryRecordingRoot() { std::filesystem::remove_all(m_path); }
    const std::filesystem::path& getPath() const { return m_path; }

   private:
    std::filesystem::path m_path;
};

}  // namespace

TEST(RecordingStore, CreatesOneStagingIdentityWithAtomicRequestEvidence) {
    TemporaryRecordingRoot root;
    RecordingStore store(root.getPath());
    const RecordingArtifactPaths paths =
        store.createStaging({"start-001", "episode-0001-front", ""});
    EXPECT_EQ(store.getStagingCount(), 1U);
    EXPECT_TRUE(std::filesystem::is_regular_file(paths.staging_directory / "start_request.json"));
    EXPECT_FALSE(std::filesystem::exists(paths.staging_directory / "start_request.json.tmp"));
    const std::optional<PersistedRecordingArtifact> persisted =
        store.findPersistedArtifact("episode-0001-front");
    ASSERT_TRUE(persisted.has_value());
    EXPECT_FALSE(persisted->finalized);
    EXPECT_EQ(persisted->start_request,
              "{\"schema_version\":1,\"request_id\":\"start-001\",\"recording_id\":\"episode-"
              "0001-front\"}");
    EXPECT_FALSE(persisted->stop_request.has_value());
    EXPECT_THROW(store.createStaging({"start-002", "episode-0001-front", ""}), std::runtime_error);
}

TEST(RecordingStore, RejectsManagedSymlink) {
    TemporaryRecordingRoot root;
    std::filesystem::create_directory(root.getPath() / ".staging");
    std::filesystem::remove(root.getPath() / ".staging");
    std::filesystem::create_directory_symlink("/tmp", root.getPath() / ".staging");
    RecordingStore store(root.getPath());
    EXPECT_THROW(store.initialize(), std::invalid_argument);
}

}  // namespace nodus_vision
