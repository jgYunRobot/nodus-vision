#include <gtest/gtest.h>

#include <stdexcept>

#include "intel_d435_adapter.hpp"

namespace nodus_vision {

TEST(IntelD435Adapter, RejectsInvalidConfigWithoutOpeningDevice)
{
    IntelD435AdapterConfig config;
    config.depth_width = 0;
    EXPECT_THROW(validateIntelD435AdapterConfig(config), std::invalid_argument);
}

TEST(IntelD435Adapter, ConstructsAndDestroysWithoutOpeningDevice)
{
    IntelD435AdapterConfig config;
    config.device_id = "test_d435";
    EXPECT_NO_THROW({ IntelD435Adapter adapter(config); });
}

} // namespace nodus_vision
