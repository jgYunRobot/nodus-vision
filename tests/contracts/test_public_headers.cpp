#include <gtest/gtest.h>

#include <nodus_vision/camera_adapter.hpp>
#include <nodus_vision/camera_contracts.hpp>
#include <nodus_vision/frame_store.hpp>

namespace nodus_vision {

TEST(PublicContracts, DefaultsAreExplicitAndVendorNeutral)
{
    StreamProfile profile;
    FrameIdentity identity;
    CameraHealthSnapshot health;

    EXPECT_EQ(profile.format, PixelFormat::e_UNKNOWN);
    EXPECT_EQ(identity.capture_generation, 0U);
    EXPECT_EQ(health.lifecycle, CameraLifecycle::e_DISCONNECTED);
    EXPECT_STREQ(toString(PixelFormat::e_RGB8), "rgb8");
}

} // namespace nodus_vision
