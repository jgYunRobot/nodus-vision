/**
 * @file frame_store.hpp
 * @brief immutable latest-only captured frame slot을 제공한다.
 */

#ifndef NODUS_VISION_FRAME_STORE_HPP_
#define NODUS_VISION_FRAME_STORE_HPP_

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

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const CapturedFrame> m_latest_frame;
};

} // namespace nodus_vision

#endif // NODUS_VISION_FRAME_STORE_HPP_
