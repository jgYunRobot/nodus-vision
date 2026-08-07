/**
 * @file frame_store.cpp
 * @brief immutable latest-only frame slot을 구현한다.
 */

#include <nodus_vision/frame_store.hpp>
#include <utility>

namespace nodus_vision {
namespace {

bool isNewerIdentity(const FrameIdentity& candidate, const FrameIdentity& current) {
    if (candidate.capture_generation != current.capture_generation) {
        return candidate.capture_generation > current.capture_generation;
    }
    return candidate.frame_number > current.frame_number;
}

}  // namespace

bool FrameStore::publishFrame(std::shared_ptr<const CapturedFrame> frame) {
    if (frame == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latest_frame != nullptr &&
        !isNewerIdentity(frame->getSnapshot().identity, m_latest_frame->getSnapshot().identity)) {
        return false;
    }
    m_latest_frame = std::move(frame);
    m_published_at = std::chrono::steady_clock::now();
    m_frame_available.notify_all();
    return true;
}

std::shared_ptr<const CapturedFrame> FrameStore::acquireLatestFrame() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest_frame;
}

std::shared_ptr<const CapturedFrame> FrameStore::acquireFreshFrame(
    std::chrono::milliseconds max_age) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latest_frame == nullptr || max_age.count() <= 0 ||
        std::chrono::steady_clock::now() - m_published_at > max_age) {
        return nullptr;
    }
    return m_latest_frame;
}

std::shared_ptr<const CapturedFrame> FrameStore::waitForFrameAfter(
    const FrameIdentity& identity, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(m_mutex);
    const auto has_newer_frame = [this, &identity]() {
        return m_latest_frame != nullptr &&
               isNewerIdentity(m_latest_frame->getSnapshot().identity, identity);
    };
    if (!m_frame_available.wait_for(lock, timeout, has_newer_frame)) {
        return nullptr;
    }
    return m_latest_frame;
}

void FrameStore::clear() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latest_frame.reset();
        m_published_at = {};
    }
    m_frame_available.notify_all();
}

}  // namespace nodus_vision
