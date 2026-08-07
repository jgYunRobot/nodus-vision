#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "encoded_preview_cache.hpp"

namespace nodus_vision {
TEST(EncodedPreviewCache, RetainsOnlyNewerPreviewPerKind) {
    EncodedPreviewCache cache;
    auto first = std::make_shared<EncodedPreview>();
    first->identity = {1U, 1U, 1, 1, 0.0, DeviceTimestampDomain::e_UNKNOWN};
    first->jpeg = {1U};
    auto newer = std::make_shared<EncodedPreview>();
    newer->identity = {1U, 2U, 2, 2, 0.0, DeviceTimestampDomain::e_UNKNOWN};
    newer->jpeg = {2U};
    EXPECT_TRUE(cache.publishPreview(PreviewKind::e_COLOR, first));
    EXPECT_FALSE(cache.publishPreview(PreviewKind::e_COLOR, first));
    EXPECT_TRUE(cache.publishPreview(PreviewKind::e_COLOR, newer));
    EXPECT_EQ(cache.acquirePreview(PreviewKind::e_COLOR)->identity.frame_number, 2U);
    EXPECT_NE(cache.acquireFreshPreview(PreviewKind::e_COLOR, std::chrono::milliseconds(100)),
              nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(cache.acquireFreshPreview(PreviewKind::e_COLOR, std::chrono::milliseconds(1)),
              nullptr);
    cache.invalidateGeneration(2U);
    EXPECT_EQ(cache.acquirePreview(PreviewKind::e_COLOR), nullptr);
    EXPECT_TRUE(cache.publishPreview(PreviewKind::e_DEPTH, newer));
    cache.clear();
    EXPECT_EQ(cache.acquirePreview(PreviewKind::e_DEPTH), nullptr);
}
}  // namespace nodus_vision
