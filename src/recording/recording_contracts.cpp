/** @file recording_contracts.cpp @brief recording value contract를 구현한다. */

#include "recording_contracts.hpp"

#include <boost/json.hpp>
#include <stdexcept>

namespace nodus_vision {

bool isRecordingIdValid(const std::string& recording_id) noexcept {
    if (recording_id.empty() || recording_id.size() > 64U || recording_id == "." ||
        recording_id == "..") {
        return false;
    }
    for (std::size_t index = 0; index < recording_id.size(); ++index) {
        const char character = recording_id.at(index);
        const bool lowercase_alphanumeric =
            (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        const bool permitted_suffix = character == '.' || character == '_' || character == '-';
        if ((!lowercase_alphanumeric && !permitted_suffix) ||
            (index == 0U && !lowercase_alphanumeric)) {
            return false;
        }
    }
    return true;
}

bool isRecordingRequestIdValid(const std::string& request_id) noexcept {
    if (request_id.empty() || request_id.size() > 128U) {
        return false;
    }
    for (const unsigned char character : request_id) {
        if (character < 0x21U || character > 0x7EU || character == '/' || character == '\\') {
            return false;
        }
    }
    return true;
}

std::string serializeRecordingStartRequest(const RecordingStartRequest& request) {
    if (!isRecordingRequestIdValid(request.request_id) ||
        !isRecordingIdValid(request.recording_id)) {
        throw std::invalid_argument("Recording start request has an unsafe identity.");
    }
    if (!request.canonical_json.empty()) {
        return request.canonical_json;
    }
    boost::json::object root;
    root["schema_version"] = 1;
    root["request_id"] = request.request_id;
    root["recording_id"] = request.recording_id;
    return boost::json::serialize(root);
}

const char* getRecordingStopReasonName(RecordingStopReason reason) noexcept {
    switch (reason) {
        case RecordingStopReason::e_REQUESTED:
            return "requested";
        case RecordingStopReason::e_MAX_DURATION:
            return "max_duration";
        case RecordingStopReason::e_APPLICATION_SHUTDOWN:
            return "application_shutdown";
    }
    return "application_shutdown";
}

}  // namespace nodus_vision
