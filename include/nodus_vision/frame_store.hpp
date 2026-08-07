/**
 * @file frame_store.hpp
 * @brief immutable latest-only captured frame slot을 제공한다.
 */

#ifndef NODUS_VISION_FRAME_STORE_HPP_
#define NODUS_VISION_FRAME_STORE_HPP_

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <nodus_vision/camera_adapter.hpp>

namespace nodus_vision {

/** @brief frame ordering을 보장하는 latest-wins single-slot store다. */
class FrameStore {
   public:
    /** @brief candidate가 latest slot을 대체하면 true를 반환한다. */
    bool publishFrame(std::shared_ptr<const CapturedFrame> frame);
    /** @return current immutable frame owner 또는 empty slot. */
    std::shared_ptr<const CapturedFrame> acquireLatestFrame() const;
    /** @brief 지정한 age 이내의 current frame만 반환한다. */
    std::shared_ptr<const CapturedFrame> acquireFreshFrame(std::chrono::milliseconds max_age) const;
    /** @brief 지정 identity보다 새로운 frame을 bounded wait한다. */
    std::shared_ptr<const CapturedFrame> waitForFrameAfter(const FrameIdentity& identity,
                                                           std::chrono::milliseconds timeout) const;
    /** @brief current owner를 제거하고 waiter를 깨운다. */
    void clear();

   private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_frame_available;
    std::shared_ptr<const CapturedFrame> m_latest_frame;
    std::chrono::steady_clock::time_point m_published_at{};
};

}  // namespace nodus_vision

#endif  // NODUS_VISION_FRAME_STORE_HPP_
