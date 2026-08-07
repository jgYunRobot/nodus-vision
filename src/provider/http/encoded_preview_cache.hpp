/**
 * @file encoded_preview_cache.hpp
 * @brief stream kind별 immutable latest-only JPEG cache를 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_
#define NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nodus_vision/camera_contracts.hpp>
#include <vector>

namespace nodus_vision {
enum class PreviewKind { e_COLOR, e_DEPTH };
struct EncodedPreview {
    FrameIdentity identity;
    std::vector<std::uint8_t> jpeg;
};
class EncodedPreviewCache {
   public:
    bool publishPreview(PreviewKind kind, std::shared_ptr<const EncodedPreview> preview);
    std::shared_ptr<const EncodedPreview> acquirePreview(PreviewKind kind) const;
    std::shared_ptr<const EncodedPreview> acquireFreshPreview(
        PreviewKind kind, std::chrono::milliseconds max_age) const;
    void invalidateGeneration(std::uint64_t capture_generation);
    void clear();

   private:
    struct PreviewSlot {
        std::shared_ptr<const EncodedPreview> preview;
        std::chrono::steady_clock::time_point published_at{};
    };

    mutable std::mutex m_mutex;
    PreviewSlot m_color_preview;
    PreviewSlot m_depth_preview;
};
}  // namespace nodus_vision

#endif  // NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_
