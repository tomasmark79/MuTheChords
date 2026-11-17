#include <MuTheChordsLib/PerformanceEngine.hpp>
#include <gtest/gtest.h>
#include <vector>

using namespace muthechords;

namespace {
  struct Message {
    Event event;
    int sample;
  };

  struct Fixture {
    PerformanceEngine engine;
    Pattern pattern;
    PerformanceSettings settings;
    std::vector<Message> output;

    Fixture() {
      pattern.length = 4;
      pattern.sourceRoot = 60;
      engine.setOutput(this, [](void *ctx, Event event, int sample) {
        static_cast<Fixture *>(ctx)->output.push_back({event, sample});
      });
    }

    void configure() { engine.configure(&pattern, settings); }
    void advance(double beat, int samples = 400) { engine.advance(beat, .01, 0, samples); }
  };

  ChordSnapshot makeChord(int root = 62) {
    ChordSnapshot c;
    c.root = root;
    c.key = 0;
    c.scale = 0;
    c.degree = 1;
    c.count = c.intendedCount = 3;
    c.notes = {root, root + 3, root + 7};
    return c;
  }
} // namespace

TEST(PerformanceEngineTest, VoicesLoopsAndPlaybackModes) {
  {
    Fixture f;
    f.pattern.events = {{0, 1, 60, 90}, {0, 1, 64, 90}, {0, 1, 67, 90}};
    f.settings.voices = 1;
    f.configure();
    auto c = makeChord(60);
    c.count = c.intendedCount = 8;
    c.notes = {48, 60, 64, 67, 71, 74, 77, 81};
    f.engine.trigger(c, 1, 0, false);
    f.advance(0, 1);
    bool high = false;
    for (const auto &m : f.output) {
      if (m.event.on && m.event.note == 81) {
        high = true;
      }
    }
    EXPECT_TRUE(high);
  }
  {
    Fixture f;
    f.pattern.length = 16;
    f.pattern.events = {{0, .5, 60, 90}, {4, .5, 60, 90}, {8, .5, 64, 90}, {12, .5, 67, 90}};
    f.settings.loopStart = 1;
    f.settings.loopEnd = 3;
    f.settings.playback = 2;
    f.configure();
    f.engine.trigger(makeChord(), 1, 0, false);
    f.advance(0, 801);
    std::vector<int> starts;
    for (const auto &m : f.output) {
      if (m.event.on) {
        starts.push_back(m.sample);
      }
    }
    EXPECT_EQ(starts, (std::vector<int>{0, 400, 800}));
  }
  {
    Fixture f;
    f.pattern.length = 16;
    f.pattern.events = {{4, .5, 60, 90}, {8, .5, 64, 90}};
    f.settings.loopStart = 1;
    f.settings.loopEnd = 3;
    f.settings.playback = 1;
    f.configure();
    f.engine.trigger(makeChord(), 1, 0, false);
    f.advance(0, 801);
    int starts = 0;
    for (const auto &m : f.output) {
      starts += m.event.on ? 1 : 0;
    }
    EXPECT_EQ(starts, 2);
    EXPECT_FALSE(f.engine.isActive());
    EXPECT_FALSE(f.engine.isSounding(62));
  }
}

TEST(PerformanceEngineTest, BassAndBlockSplittingEquivalence) {
  {
    Fixture f;
    f.pattern.category = "Bass";
    f.pattern.sourceRoot = 41;
    f.pattern.events = {{0, .5, 48, 90}, {1, .5, 50, 80}, {2, .5, 51, 70}};
    f.settings.passing = 1;
    f.configure();
    auto chord = makeChord(62);
    chord.notes = {62, 66, 69};
    f.engine.trigger(chord, 1, 0, false);
    f.advance(0, 1);
    ASSERT_FALSE(f.output.empty());
    EXPECT_EQ(f.output.front().event.note, 62);
    f.advance(.01, 250);
    ASSERT_GE(f.output.size(), 5u);
    EXPECT_EQ(f.output[2].event.note, 64);
    EXPECT_EQ(f.output[4].event.note, 66);
  }
  {
    Fixture whole, split;
    whole.pattern.events = {{1, .5, 67, 99}, {0, .25, 60, 42}, {.25, .75, 64, 87}};
    split.pattern = whole.pattern;
    whole.configure();
    split.configure();
    whole.engine.trigger(makeChord(), 1, 0, false);
    split.engine.trigger(makeChord(), 1, 0, false);
    whole.advance(0, 401);
    for (int block = 0; block < 5; ++block) {
      const auto before = split.output.size();
      split.advance(block, block == 4 ? 1 : 100);
      for (size_t i = before; i < split.output.size(); ++i) {
        split.output[i].sample += block * 100;
      }
    }
    ASSERT_EQ(whole.output.size(), split.output.size());
    for (size_t i = 0; i < whole.output.size(); ++i) {
      EXPECT_EQ(whole.output[i].sample, split.output[i].sample);
      EXPECT_EQ(whole.output[i].event.on, split.output[i].event.on);
      EXPECT_EQ(whole.output[i].event.note, split.output[i].event.note);
    }
  }
  {
    Fixture soft, loud;
    soft.pattern.events = {{0, .5, 60, 100}};
    loud.pattern = soft.pattern;
    soft.configure();
    loud.configure();
    soft.engine.trigger(makeChord(), 1, 0, false, 0, 50);
    loud.engine.trigger(makeChord(), 1, 0, false, 0, 120);
    soft.advance(0, 1);
    loud.advance(0, 1);
    ASSERT_FALSE(soft.output.empty());
    ASSERT_FALSE(loud.output.empty());
    EXPECT_EQ(soft.output.front().event.velocity, 50);
    EXPECT_EQ(loud.output.front().event.velocity, 120);
  }
}

TEST(PerformanceEngineTest, FractionalLoopPreservesEventOrderAndSkipsEventsOutsideRegion) {
  for (bool prepared : {false, true}) {
    Fixture f;
    f.pattern.length = 4.375;
    f.pattern.events = {
        {4.25, .125, 67, 90}, {4, .125, 64, 80}, {3.5, .25, 60, 127}, {4, .25, 60, 70}};
    if (prepared) {
      f.pattern.playback = f.pattern.preparePlayback();
    }
    f.settings.loopStart = 1;
    f.settings.playback = 2;
    f.configure();
    f.engine.trigger(makeChord(), 1, 0, false);
    f.engine.advance(0, 1.0 / 16, 0, 13);
    std::vector<std::pair<int, int>> attacks;
    for (const auto &message : f.output) {
      if (message.event.on) {
        attacks.emplace_back(message.sample, message.event.note);
      }
    }
    EXPECT_EQ(attacks,
              (std::vector<std::pair<int, int>>{
                  {0, 65}, {0, 62}, {4, 69}, {6, 65}, {6, 62}, {10, 69}, {12, 65}, {12, 62}}));
  }
}
