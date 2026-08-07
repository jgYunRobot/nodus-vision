/**
 * @file encoded_preview_cache.cpp
 * @brief latest-wins encoded preview cache를 구현한다.
 */

#include "encoded_preview_cache.hpp"

#include <utility>

namespace nodus_vision {
namespace {
bool isNewer(const FrameIdentity& candidate, const FrameIdentity& current) {
    return candidate.capture_generation > current.capture_generation ||
           (candidate.capture_generation == current.capture_generation &&
            candidate.frame_number > current.frame_number);
}
}  // namespace
bool EncodedPreviewCache::publishPreview(PreviewKind kind,
                                         std::shared_ptr<const EncodedPreview> preview) {
    if (preview == nullptr || preview->jpeg.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    PreviewSlot& slot = kind == PreviewKind::e_COLOR ? m_color_preview : m_depth_preview;
    if (slot.preview != nullptr && !isNewer(preview->identity, slot.preview->identity)) {
        return false;
    }
    slot.preview = std::move(preview);
    slot.published_at = std::chrono::steady_clock::now();
    return true;
}
std::shared_ptr<const EncodedPreview> EncodedPreviewCache::acquirePreview(PreviewKind kind) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (kind == PreviewKind::e_COLOR ? m_color_preview : m_depth_preview).preview;
}
std::shared_ptr<const EncodedPreview> EncodedPreviewCache::acquireFreshPreview(
    PreviewKind kind, std::chrono::milliseconds max_age) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const PreviewSlot& slot = kind == PreviewKind::e_COLOR ? m_color_preview : m_depth_preview;
    if (slot.preview == nullptr || max_age.count() <= 0 ||
        std::chrono::steady_clock::now() - slot.published_at > max_age) {
        return nullptr;
    }
    return slot.preview;
}
void EncodedPreviewCache::invalidateGeneration(std::uint64_t capture_generation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_color_preview.preview != nullptr &&
        m_color_preview.preview->identity.capture_generation != capture_generation) {
        m_color_preview = {};
    }
    if (m_depth_preview.preview != nullptr &&
        m_depth_preview.preview->identity.capture_generation != capture_generation) {
        m_depth_preview = {};
    }
}
void EncodedPreviewCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_color_preview = {};
    m_depth_preview = {};
}
}  // namespace nodus_vision
