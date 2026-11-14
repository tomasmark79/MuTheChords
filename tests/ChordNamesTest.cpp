#include <MuTheChordsLib/ChordEngine.hpp>
#include <MuTheChordsLib/ChordNames.hpp>
#include <gtest/gtest.h>
#include <string>

namespace {
  muthechords::ChordSnapshot make(int key, int scale, int degree, int count, int inversion = 0,
                                  int transpose = 0) {
    muthechords::ChordSnapshot s;
    const auto &intervals = muthechords::scales[static_cast<size_t>(scale)];
    s.key = (key + transpose + 120) % 12;
    s.scale = scale;
    s.degree = degree;
    s.root = 60 + key + intervals[static_cast<size_t>(degree)] + transpose;
    s.intendedCount = count;
    for (int i = 0; i < count; ++i) {
      const int d = degree + i * 2;
      const int note = 60 + key + intervals[static_cast<size_t>(d % intervals.size())] +
                       12 * (d / intervals.size()) + transpose + (i < inversion ? 12 : 0);
      if (note >= 0 && note <= 127 && s.count < 8) {
        s.notes[static_cast<size_t>(s.count++)] = note;
      }
    }
    std::sort(s.notes.begin(), s.notes.begin() + s.count);
    const auto end = std::unique(s.notes.begin(), s.notes.begin() + s.count);
    s.count = static_cast<int>(end - s.notes.begin());
    return s;
  }

  int decode(const std::string &name) {
    EXPECT_FALSE(name.empty());
    int pitch = 0;
    switch (name[0]) {
    case 'C': pitch = 0; break;
    case 'D': pitch = 2; break;
    case 'E': pitch = 4; break;
    case 'F': pitch = 5; break;
    case 'G': pitch = 7; break;
    case 'A': pitch = 9; break;
    case 'B': pitch = 11; break;
    default: ADD_FAILURE() << "Invalid pitch: " << name; break;
    }
    size_t i = 1;
    while (i < name.size() && (name[i] == 'b' || name[i] == '#')) {
      pitch += name[i++] == 'b' ? -1 : 1;
    }
    EXPECT_LT(i, name.size());
    size_t consumed = 0;
    const int octave = std::stoi(name.substr(i), &consumed);
    EXPECT_EQ(consumed, name.size() - i);
    return (octave + 2) * 12 + pitch;
  }
} // namespace

TEST(ChordNamesTest, StandardSymbolsAndSpellings) {
  using muthechords::Spelling;
  EXPECT_EQ(muthechords::chordName(make(0, 0, 0, 3), Spelling::Auto), "C");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 1, 3), Spelling::Auto), "Dm");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 4, 4), Spelling::Auto), "G7");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 0, 5), Spelling::Auto), "Cmaj9");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 1, 4, 1), Spelling::Auto), "Dm7/F");
  EXPECT_EQ(muthechords::chordName(make(3, 1, 0, 3), Spelling::Auto), "Ebm");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 0, 3, 0, 2), Spelling::Auto), "D");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 6, 3), Spelling::Auto), "Bdim");
  EXPECT_EQ(muthechords::chordName(make(0, 7, 2, 3), Spelling::Auto), "Ebaug");
  EXPECT_EQ(muthechords::chordName(make(0, 7, 6, 4), Spelling::Auto), "Bdim7");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 6, 4), Spelling::Auto), "Bm7b5");
  EXPECT_EQ(muthechords::chordName(make(0, 7, 0, 4), Spelling::Auto), "Cm(maj7)");
  EXPECT_EQ(muthechords::chordName(make(0, 7, 4, 5), Spelling::Auto), "G7(b9)");
  EXPECT_EQ(muthechords::chordName(make(0, 3, 0, 6), Spelling::Auto), "Cmaj11(#11)");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 1, 7), Spelling::Auto), "Dm13");
  EXPECT_EQ(muthechords::chordName(make(0, 0, 0, 2), Spelling::Auto), "C3 + E3");

  auto cDorian = make(0, 1, 0, 3);
  EXPECT_EQ(muthechords::pitchName(63, cDorian, Spelling::Auto), "Eb3");
  EXPECT_EQ(muthechords::pitchName(70, cDorian, Spelling::Auto), "Bb3");
  auto fSharp = make(6, 0, 0, 3);
  EXPECT_EQ(muthechords::pitchName(65, fSharp, Spelling::Auto), "E#3");
  auto cSharp = make(1, 7, 0, 3);
  EXPECT_EQ(muthechords::pitchName(60, cSharp, Spelling::Auto), "B#2");
  auto eFlatMinor = make(3, 5, 0, 3);
  EXPECT_EQ(muthechords::pitchName(59, eFlatMinor, Spelling::Auto), "Cb3");

  for (int midi = 0; midi < 128; ++midi) {
    for (const auto &s : {fSharp, cSharp, eFlatMinor}) {
      ASSERT_EQ(decode(muthechords::pitchName(midi, s, Spelling::Auto)), midi);
    }
  }
}
