/**
 * @file encoded_preview_cache.hpp
 * @brief stream kind별 immutable latest-only JPEG cache를 제공한다.
 */

#ifndef NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_
#define NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <nodus_vision/camera_contracts.hpp>

namespace nodus_vision {
enum class PreviewKind { e_COLOR, e_DEPTH };
struct EncodedPreview { FrameIdentity identity; std::vector<std::uint8_t> jpeg; };
class EncodedPreviewCache {
public:
    bool publishPreview(PreviewKind kind, std::shared_ptr<const EncodedPreview> preview);
    std::shared_ptr<const EncodedPreview> acquirePreview(PreviewKind kind) const;
    void invalidateGeneration(std::uint64_t capture_generation);
private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const EncodedPreview> m_color_preview;
    std::shared_ptr<const EncodedPreview> m_depth_preview;
};
} // namespace nodus_vision

#endif // NODUS_VISION_PROVIDER_HTTP_ENCODED_PREVIEW_CACHE_HPP_
