/** @file test_recording_contracts.cpp @brief recording identity contract를 검증한다. */

#include <gtest/gtest.h>

#include <stdexcept>

#include "recording_contracts.hpp"

namespace nodus_vision {

TEST(RecordingContracts, AcceptsClosedFilesystemRecordingIdentity) {
    EXPECT_TRUE(isRecordingIdValid("episode-0001-front"));
    EXPECT_TRUE(isRecordingIdValid("a._-9"));
    EXPECT_FALSE(isRecordingIdValid(".."));
    EXPECT_FALSE(isRecordingIdValid("/tmp/escape"));
    EXPECT_FALSE(isRecordingIdValid("a%2fb"));
    EXPECT_FALSE(isRecordingIdValid("A-upper"));
}

TEST(RecordingContracts, PersistsOnlySafeCanonicalStartRequest) {
    const RecordingStartRequest request{"start-001", "episode-0001-front", ""};
    EXPECT_EQ(serializeRecordingStartRequest(request),
              "{\"schema_version\":1,\"request_id\":\"start-001\",\"recording_id\":\"episode-0001-"
              "front\"}");
    EXPECT_THROW(serializeRecordingStartRequest({"start/unsafe", "episode-0001-front", ""}),
                 std::invalid_argument);
}

}  // namespace nodus_vision
