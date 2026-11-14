#include <MuTheChordsLib/ChordEngine.hpp>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace {
  struct Rig {
    muthechords::Engine engine;
    std::vector<muthechords::Event> events;

    Rig() {
      engine.setOutput(
          this, [](void *p, muthechords::Event e) { static_cast<Rig *>(p)->events.push_back(e); });
    }

    std::vector<int> notes() const {
      std::vector<int> result;
      for (int n = 0; n < 128; ++n) {
        if (engine.isSounding(n)) {
          result.push_back(n);
        }
      }
      return result;
    }
  };
} // namespace

TEST(ChordEngineTest, VoicingsAndInversions) {
  const std::vector<std::pair<int, std::vector<int>>> cases{
      {muthechords::rootPosition, {60, 64, 67}},    {muthechords::firstInversion, {64, 67, 72}},
      {muthechords::secondInversion, {67, 72, 76}}, {muthechords::bassDown, {48, 64, 67}},
      {muthechords::topUp, {60, 64, 79}},           {muthechords::openVoicing, {60, 67, 76}},
      {muthechords::wideVoicing, {48, 67, 76}},     {muthechords::dropTwo, {52, 60, 67}},
      {muthechords::bassOctave, {48, 60, 64, 67}},  {muthechords::octaveUp, {72, 76, 79}},
      {muthechords::octaveDown, {48, 52, 55}}};

  for (const auto &[voice, expected] : cases) {
    Rig r;
    auto s = r.engine.settings();
    s.full = true;
    s.inversion = voice;
    r.engine.configure(s);
    r.engine.noteOn(2, 60, 83);
    EXPECT_EQ(r.notes(), expected);
    for (auto e : r.events) {
      EXPECT_EQ(e.channel, 2);
      EXPECT_EQ(e.velocity, 83);
    }
    r.engine.noteOff(2, 60);
    EXPECT_TRUE(r.notes().empty());
  }

  Rig r;
  auto s = r.engine.settings();
  s.full = true;
  s.chord = 2;
  s.inversion = muthechords::dropTwo;
  r.engine.configure(s);
  r.engine.noteOn(1, 60, 100);
  EXPECT_EQ(r.notes(), std::vector<int>({55, 60, 64, 71}));

  r.engine.reset();
  s.chord = 5;
  s.inversion = muthechords::bassOctave;
  r.engine.configure(s);
  r.engine.noteOn(1, 60, 100);
  EXPECT_EQ(r.notes(), std::vector<int>({48, 60, 64, 67, 71, 74, 77, 81}));
  EXPECT_EQ(r.engine.snapshot().count, 8);
  r.engine.noteOff(1, 60);
  EXPECT_TRUE(r.notes().empty());

  s.chord = 1;
  s.inversion = muthechords::octaveDown;
  r.engine.configure(s);
  r.engine.noteOn(1, 0, 100);
  EXPECT_TRUE(r.notes().empty());
}

TEST(ChordEngineTest, BasicDiatonicAndDeferredSettings) {
  {
    Rig r;
    r.engine.noteOn(1, 36, 87);
    EXPECT_EQ(r.notes(), std::vector<int>({36, 40, 43}));
    for (auto e : r.events) {
      EXPECT_EQ(e.velocity, 87);
    }
    r.engine.noteOn(1, 38, 60);
    EXPECT_EQ(r.notes(), std::vector<int>({38, 41, 45}));
    r.engine.noteOff(1, 36);
    EXPECT_EQ(r.notes().size(), 3u);
    r.engine.noteOff(1, 38);
    EXPECT_TRUE(r.notes().empty());
  }
  {
    Rig r;
    auto s = r.engine.settings();
    s.full = true;
    s.chord = 2;
    r.engine.configure(s);
    r.engine.noteOn(1, 43, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({43, 47, 50, 53}));
    s.inversion = 1;
    s.transpose = 12;
    r.engine.configure(s);
    EXPECT_EQ(r.notes(), std::vector<int>({43, 47, 50, 53}));
    r.engine.noteOn(1, 36, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({52, 55, 59, 60}));
  }
}

TEST(ChordEngineTest, OutsideScaleAndRelativeMapping) {
  {
    Rig r;
    auto s = r.engine.settings();
    s.scale = 1;
    r.engine.configure(s);
    r.engine.noteOn(1, 39, 100);
    EXPECT_FALSE(r.notes().empty());
    r.engine.noteOff(1, 39);
    r.engine.noteOn(1, 40, 100);
    EXPECT_TRUE(r.notes().empty());
    s.outside = 1;
    r.engine.configure(s);
    r.engine.noteOn(1, 40, 90);
    EXPECT_EQ(r.notes(), std::vector<int>({40}));
    r.engine.noteOff(1, 40);
    s.outside = 4;
    s.scale = 0;
    r.engine.configure(s);
    r.engine.noteOn(1, 37, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({36, 40, 43}));
    s.outside = 3;
    r.engine.configure(s);
    r.engine.noteOn(1, 37, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({38, 41, 45}));
  }
  {
    Rig r;
    auto s = r.engine.settings();
    s.mapping = 1;
    s.root = 2;
    s.scale = 5;
    r.engine.configure(s);
    r.engine.noteOn(1, 36, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({38, 41, 45}));
    r.engine.noteOff(1, 36);
    r.engine.noteOn(1, 40, 100);
    EXPECT_EQ(r.notes(), std::vector<int>({41, 45, 48}));
  }
}

TEST(ChordEngineTest, LatchPedalAndReset) {
  Rig r;
  auto s = r.engine.settings();
  s.latch = true;
  r.engine.configure(s);
  r.engine.noteOn(1, 36, 100);
  r.engine.noteOff(1, 36);
  EXPECT_EQ(r.notes().size(), 3u);
  r.engine.noteOn(1, 36, 100);
  EXPECT_TRUE(r.notes().empty());
  r.engine.noteOn(1, 38, 100);
  s.latch = false;
  r.engine.configure(s);
  EXPECT_TRUE(r.notes().empty());

  s.pedal = 1;
  r.engine.configure(s);
  r.engine.pedal(1, 127);
  r.engine.noteOn(1, 36, 100);
  r.engine.noteOff(1, 36);
  EXPECT_FALSE(r.notes().empty());
  r.engine.noteOn(1, 38, 100);
  EXPECT_EQ(r.notes(), std::vector<int>({38, 41, 45}));
  r.engine.noteOff(1, 38);
  r.engine.pedal(1, 0);
  EXPECT_TRUE(r.notes().empty());

  s.pedal = 2;
  r.engine.configure(s);
  r.engine.noteOn(1, 36, 100);
  r.engine.pedal(1, 127);
  EXPECT_EQ(r.notes().size(), 3u);
  r.engine.noteOn(1, 38, 100);
  EXPECT_EQ(r.notes().size(), 4u);
  r.engine.pedal(1, 0);
  EXPECT_EQ(r.notes().size(), 4u);
  s.mode = 1;
  r.engine.configure(s);
  EXPECT_TRUE(r.notes().empty());
  r.engine.noteOn(1, 36, 100);
  EXPECT_TRUE(r.notes().empty());
}

TEST(ChordEngineTest, AllScalesAndRandomizedNoteBalance) {
  {
    Rig r;
    auto s = r.engine.settings();
    s.full = true;
    for (int scaleIndex = 0; scaleIndex < muthechords::scaleCount; ++scaleIndex) {
      s.scale = scaleIndex;
      const auto &scale = muthechords::scales[static_cast<size_t>(scaleIndex)];
      for (int degree = 0; degree < scale.size(); ++degree) {
        for (int type = 0; type < 6; ++type) {
          s.chord = type;
          r.engine.configure(s);
          const int input = 48 + scale[static_cast<size_t>(degree)];
          r.engine.noteOn(1, input, 90);
          ASSERT_FALSE(r.notes().empty());
          for (int note : r.notes()) {
            ASSERT_TRUE(r.engine.inScale(note));
          }
          r.engine.noteOff(1, input);
          ASSERT_TRUE(r.notes().empty());
        }
      }
    }
  }
  {
    Rig r;
    std::array<std::array<bool, 128>, 16> active{};
    std::mt19937 random(42);
    for (int i = 0; i < 20000; ++i) {
      r.events.clear();
      int op = static_cast<int>(random() % 8), n = static_cast<int>(random() % 128),
          ch = static_cast<int>(random() % 16) + 1;
      if (op < 3) {
        r.engine.noteOn(ch, n, static_cast<int>(random() % 127) + 1);
      } else if (op < 5) {
        r.engine.noteOff(ch, n);
      } else if (op == 5) {
        r.engine.pedal(ch, random() % 2 ? 127 : 0);
      } else if (op == 6) {
        auto s = r.engine.settings();
        s.scale = static_cast<int>(random() % muthechords::scaleCount);
        s.chord = static_cast<int>(random() % 6);
        s.transpose = static_cast<int>(random() % 73) - 36;
        s.inversion = static_cast<int>(random() % muthechords::voicingCount);
        s.latch = random() % 2;
        s.mode = static_cast<int>(random() % 2);
        s.full = random() % 2;
        r.engine.configure(s);
      } else {
        r.engine.reset();
      }
      for (auto e : r.events) {
        ASSERT_GE(e.note, 0);
        ASSERT_LT(e.note, 128);
        auto &playing = active[static_cast<size_t>(e.channel - 1)][static_cast<size_t>(e.note)];
        ASSERT_NE(playing, e.on);
        playing = e.on;
      }
    }
    r.events.clear();
    r.engine.reset();
    for (auto e : r.events) {
      active[static_cast<size_t>(e.channel - 1)][static_cast<size_t>(e.note)] = e.on;
    }
    for (auto c : active) {
      for (bool n : c) {
        ASSERT_FALSE(n);
      }
    }
  }
}
