/**
 * @file camera_contracts.cpp
 * @brief camera-neutral enum serialization을 구현한다.
 */

#include <nodus_vision/camera_contracts.hpp>

namespace nodus_vision {

const char* toString(PixelFormat format) noexcept
{
    switch (format) {
    case PixelFormat::e_RGB8:
        return "rgb8";
    case PixelFormat::e_BGR8:
        return "bgr8";
    case PixelFormat::e_Z16:
        return "z16";
    case PixelFormat::e_UNKNOWN:
    default:
        return "unknown";
    }
}

const char* toString(DeviceTimestampDomain domain) noexcept
{
    switch (domain) {
    case DeviceTimestampDomain::e_HARDWARE_CLOCK:
        return "hardware_clock";
    case DeviceTimestampDomain::e_SYSTEM_TIME:
        return "system_time";
    case DeviceTimestampDomain::e_GLOBAL_TIME:
        return "global_time";
    case DeviceTimestampDomain::e_UNKNOWN:
    default:
        return "unknown";
    }
}

const char* toString(DistortionModel model) noexcept
{
    switch (model) {
    case DistortionModel::e_NONE:
        return "none";
    case DistortionModel::e_MODIFIED_BROWN_CONRADY:
        return "modified_brown_conrady";
    case DistortionModel::e_INVERSE_BROWN_CONRADY:
        return "inverse_brown_conrady";
    case DistortionModel::e_FTHETA:
        return "ftheta";
    case DistortionModel::e_BROWN_CONRADY:
        return "brown_conrady";
    case DistortionModel::e_KANNALA_BRANDT4:
        return "kannala_brandt4";
    case DistortionModel::e_UNKNOWN:
    default:
        return "unknown";
    }
}

const char* toString(CameraLifecycle lifecycle) noexcept
{
    switch (lifecycle) {
    case CameraLifecycle::e_CONNECTED:
        return "connected";
    case CameraLifecycle::e_STREAMING:
        return "streaming";
    case CameraLifecycle::e_DEGRADED:
        return "degraded";
    case CameraLifecycle::e_DISCONNECTED:
    default:
        return "disconnected";
    }
}

} // namespace nodus_vision
