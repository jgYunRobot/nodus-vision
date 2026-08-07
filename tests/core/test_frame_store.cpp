#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include <nodus_vision/frame_store.hpp>

namespace nodus_vision {
namespace {

class TestFrame final : public CapturedFrame {
public:
    TestFrame(std::uint64_t generation, std::uint64_t frame_number, std::shared_ptr<std::atomic<int>> lifetime)
        : m_lifetime(std::move(lifetime))
    {
        m_snapshot.identity.capture_generation = generation;
        m_snapshot.identity.frame_number = frame_number;
    }

    ~TestFrame() override { ++(*m_lifetime); }
    const FrameSnapshot& getSnapshot() const noexcept override { return m_snapshot; }
    std::optional<VideoFrameView> getColorFrameView() const override { return std::nullopt; }
    std::optional<VideoFrameView> getDepthPreviewFrameView() const override { return std::nullopt; }
    std::optional<PixelPointResult> queryPixelPoint(int, int) const override { return std::nullopt; }
    RoiDepthResult queryDepthInRoi(int, int, int, int) const override { return {}; }
    PointCloudSnapshot buildPointCloudSnapshot(std::size_t, int) const override { return {}; }

private:
    FrameSnapshot m_snapshot;
    std::shared_ptr<std::atomic<int>> m_lifetime;
};

std::shared_ptr<const CapturedFrame> makeFrame(std::uint64_t generation, std::uint64_t frame_number)
{
    return std::make_shared<TestFrame>(generation, frame_number, std::make_shared<std::atomic<int>>(0));
}

} // namespace

TEST(FrameStore, StartsEmptyAndRejectsNull)
{
    FrameStore store;
    EXPECT_EQ(store.acquireLatestFrame(), nullptr);
    EXPECT_FALSE(store.publishFrame(nullptr));
}

TEST(FrameStore, ReplacesOnlyWithNewerIdentity)
{
    FrameStore store;
    EXPECT_TRUE(store.publishFrame(makeFrame(1U, 2U)));
    EXPECT_FALSE(store.publishFrame(makeFrame(1U, 2U)));
    EXPECT_FALSE(store.publishFrame(makeFrame(1U, 1U)));
    EXPECT_FALSE(store.publishFrame(makeFrame(0U, 99U)));
    EXPECT_TRUE(store.publishFrame(makeFrame(2U, 1U)));
    EXPECT_EQ(store.acquireLatestFrame()->getSnapshot().identity.capture_generation, 2U);
}

TEST(FrameStore, RetainsOwnerAfterReplacement)
{
    FrameStore store;
    const std::shared_ptr<std::atomic<int>> lifetime = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<const CapturedFrame> first = std::make_shared<TestFrame>(1U, 1U, lifetime);
    ASSERT_TRUE(store.publishFrame(first));
    std::shared_ptr<const CapturedFrame> reader = store.acquireLatestFrame();
    ASSERT_TRUE(store.publishFrame(makeFrame(1U, 2U)));
    first.reset();
    EXPECT_EQ(lifetime->load(), 0);
    reader.reset();
    EXPECT_EQ(lifetime->load(), 1);
}

TEST(FrameStore, ConcurrentReadersSeeValidLatestOwner)
{
    FrameStore store;
    std::atomic<bool> writer_done{false};
    std::atomic<int> read_count{0};
    std::thread writer([&store, &writer_done]() {
        for (std::uint64_t frame_number = 1; frame_number <= 100U; ++frame_number) {
            EXPECT_TRUE(store.publishFrame(makeFrame(1U, frame_number)));
        }
        writer_done.store(true);
    });
    std::thread reader([&store, &writer_done, &read_count]() {
        while (!writer_done.load()) {
            const std::shared_ptr<const CapturedFrame> frame = store.acquireLatestFrame();
            if (frame != nullptr) {
                EXPECT_EQ(frame->getSnapshot().identity.capture_generation, 1U);
                ++read_count;
            }
        }
    });
    writer.join();
    reader.join();
    EXPECT_EQ(store.acquireLatestFrame()->getSnapshot().identity.frame_number, 100U);
    EXPECT_GE(read_count.load(), 0);
}

} // namespace nodus_vision
