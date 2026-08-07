/** @file recording_store.hpp @brief staging/finalized artifact directory owner를 제공한다. */

#ifndef NODUS_VISION_RECORDING_STORE_HPP_
#define NODUS_VISION_RECORDING_STORE_HPP_

#include <filesystem>
#include <string>

#include "recording_contracts.hpp"

namespace nodus_vision {

/** @brief one recording artifact의 private filesystem paths다. */
struct RecordingArtifactPaths {
    std::filesystem::path staging_directory;
    std::filesystem::path finalized_directory;
};

/** @brief config-owned artifact root의 safe staging/finalized layout owner다. */
class RecordingStore {
   public:
    explicit RecordingStore(std::filesystem::path root);

    /** @brief managed root와 direct children을 symlink 없이 준비한다. */
    void initialize();
    /** @brief new staging directory와 atomic start request evidence를 만든다. */
    RecordingArtifactPaths createStaging(const RecordingStartRequest& request);
    /** @brief crash recovery를 위해 finalized로 노출하지 않은 staging count를 반환한다. */
    std::size_t getStagingCount() const;

   private:
    std::filesystem::path m_root;
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_STORE_HPP_
