#include <MuTheChordsLib/HarmonySync.hpp>
#include <gtest/gtest.h>
#include <string>

namespace {
  const muthechords::HarmonyState a{2, 3}, b{11, 8}, c{7, 43};
}

TEST(HarmonySyncTest, JoinPublishReadAndLeave) {
  const std::string channel = "gtest-harmony-sync-channel";
  muthechords::HarmonySync first(channel), second(channel), isolated(channel + "-other");

  auto state = a;
  ASSERT_TRUE(first.join(state));
  EXPECT_EQ(state, a);

  state = b;
  ASSERT_TRUE(second.join(state));
  EXPECT_EQ(state, a);
  EXPECT_FALSE(second.read(state));
  EXPECT_TRUE(second.error().empty());

  ASSERT_TRUE(first.publish(b));
  EXPECT_TRUE(second.read(state));
  EXPECT_EQ(state, b);
  EXPECT_FALSE(second.read(state));

  ASSERT_TRUE(second.publish(c));
  EXPECT_TRUE(first.read(state));
  EXPECT_EQ(state, c);

  auto bad = c;
  bad.scale = 44;
  EXPECT_FALSE(first.publish(bad));
  EXPECT_FALSE(first.error().empty());
  EXPECT_FALSE(second.read(state));
  EXPECT_EQ(state, c);

  state = a;
  EXPECT_TRUE(isolated.join(state));
  EXPECT_EQ(state, a);

  second.leave();
  EXPECT_FALSE(second.joined());
  EXPECT_FALSE(second.publish(a));
  EXPECT_TRUE(first.publish(b));
  first.leave();

  state = c;
  EXPECT_TRUE(second.join(state));
  EXPECT_EQ(state, c);
  second.leave();
  isolated.leave();
}
