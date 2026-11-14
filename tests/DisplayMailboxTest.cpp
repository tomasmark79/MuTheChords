#include <MuTheChordsLib/DisplayMailbox.hpp>
#include <atomic>
#include <gtest/gtest.h>
#include <thread>

namespace {
  struct Frame {
    int sequence = 0;
    std::array<int, 128> values{};
  };
} // namespace

TEST(DisplayMailboxTest, LatestFrameAndCoherentConcurrentTransfer) {
  muthechords::DisplayMailbox<Frame> mailbox;
  Frame read;
  EXPECT_FALSE(mailbox.read(read));

  for (int i = 1; i <= 100; ++i) {
    Frame frame;
    frame.sequence = i;
    frame.values.fill(i);
    mailbox.publish(frame);
  }
  ASSERT_TRUE(mailbox.read(read));
  EXPECT_EQ(read.sequence, 100);

  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 101; i <= 100000; ++i) {
      Frame frame;
      frame.sequence = i;
      frame.values.fill(i);
      mailbox.publish(frame);
    }
    done.store(true, std::memory_order_release);
  });

  int previous = 100;
  while (!done.load(std::memory_order_acquire)) {
    if (mailbox.read(read)) {
      ASSERT_GE(read.sequence, previous);
      previous = read.sequence;
      for (int value : read.values) {
        ASSERT_EQ(value, read.sequence);
      }
    }
  }
  writer.join();
  if (mailbox.read(read)) {
    previous = read.sequence;
  }
  EXPECT_EQ(previous, 100000);
}
