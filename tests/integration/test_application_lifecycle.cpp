#include <gtest/gtest.h>

#include "vision_application.hpp"

namespace nodus_vision {
namespace {
VisionConfig makeFakeConfig()
{
    VisionConfig config;
    config.schema_version = 1;
    config.device_id = "fake";
    config.component_id = "camera.fake";
    config.adapter = "fake";
    config.fake = {4, 3, 30, 1U, 7U, true, false, false, "fake"};
    config.provider = {"127.0.0.1", 0, "http://127.0.0.1:8900", 2, 1, 1000, 8192, 4096, 1000};
    return config;
}
} // namespace

TEST(VisionApplication, StartsFakeProviderAndStopsIdempotently)
{
    VisionApplication application(makeFakeConfig());
    application.startApplication();
    EXPECT_GT(application.getBoundPort(), 0);
    EXPECT_EQ(application.getHealthSnapshot().state, ProviderState::e_READY);
    application.stopApplication();
    application.stopApplication();
}
} // namespace nodus_vision
