/**
 * @file frame_store.cpp
 * @brief immutable latest-only frame slot을 구현한다.
 */

#include <nodus_vision/frame_store.hpp>

#include <utility>

namespace nodus_vision {
namespace {

bool isNewerIdentity(const FrameIdentity& candidate, const FrameIdentity& current)
{
    if (candidate.capture_generation != current.capture_generation) {
        return candidate.capture_generation > current.capture_generation;
    }
    return candidate.frame_number > current.frame_number;
}

} // namespace

bool FrameStore::publishFrame(std::shared_ptr<const CapturedFrame> frame)
{
    if (frame == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latest_frame != nullptr &&
        !isNewerIdentity(frame->getSnapshot().identity, m_latest_frame->getSnapshot().identity)) {
        return false;
    }
    m_latest_frame = std::move(frame);
    return true;
}

std::shared_ptr<const CapturedFrame> FrameStore::acquireLatestFrame() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest_frame;
}

} // namespace nodus_vision
