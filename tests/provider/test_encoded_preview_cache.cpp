#include <gtest/gtest.h>

#include "encoded_preview_cache.hpp"

namespace nodus_vision {
TEST(EncodedPreviewCache, RetainsOnlyNewerPreviewPerKind)
{
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
    cache.invalidateGeneration(2U);
    EXPECT_EQ(cache.acquirePreview(PreviewKind::e_COLOR), nullptr);
}
} // namespace nodus_vision
