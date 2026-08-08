/**
 * @file recording_contracts.hpp
 * @brief Vision-owned recording identity와 request value contract를 제공한다.
 */

#ifndef NODUS_VISION_RECORDING_CONTRACTS_HPP_
#define NODUS_VISION_RECORDING_CONTRACTS_HPP_

#include <string>

namespace nodus_vision {

/** @brief recording lifecycle의 stable public state다. */
enum class RecordingState {
    e_DISABLED,
    e_IDLE,
    e_PREPARING,
    e_RECORDING,
    e_FINALIZING,
    e_FINALIZED,
    e_FAULTED
};

/** @brief recording artifact가 닫힌 실제 원인이다. */
enum class RecordingStopReason { e_REQUESTED, e_MAX_DURATION, e_APPLICATION_SHUTDOWN };

/** @brief persisted start request의 filesystem-safe value다. */
struct RecordingStartRequest {
    std::string request_id;
    std::string recording_id;
    std::string canonical_json;
};

/** @brief recording ID가 one filesystem component closed format인지 검증한다. */
bool isRecordingIdValid(const std::string& recording_id) noexcept;
/** @brief request ID가 bounded public identifier인지 검증한다. */
bool isRecordingRequestIdValid(const std::string& request_id) noexcept;
/** @brief start request를 canonical compact JSON으로 serialize한다. */
std::string serializeRecordingStartRequest(const RecordingStartRequest& request);
/** @brief stop reason을 stable manifest 문자열로 변환한다. */
const char* getRecordingStopReasonName(RecordingStopReason reason) noexcept;

}  // namespace nodus_vision

#endif  // NODUS_VISION_RECORDING_CONTRACTS_HPP_
