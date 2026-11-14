#include <MuTheChordsLib/PatternLibrary.hpp>
#include <Utils/UtilsFactory.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace {
  const std::string validJson =
      R"({"version":1,"patterns":[{"id":"test","name":"Pattern 2","category":"Test","group":"","length":4,"numerator":4,"denominator":4,"sourceRoot":48,"events":[{"beat":0.125,"duration":0.75,"note":55,"velocity":93}]}]})";

  std::string replaceFirst(std::string s, const std::string &from, const std::string &to) {
    const auto pos = s.find(from);
    if (pos != std::string::npos) {
      s.replace(pos, from.size(), to);
    }
    return s;
  }
} // namespace

TEST(PatternLibraryTest, LoadAndValidatePatterns) {
  auto writer = muthechords::utils::UtilsFactory::createFileWriter();
  const auto file = std::filesystem::temp_directory_path() / "muthechords-pattern-test.json";
  struct Cleanup {
    std::filesystem::path file;
    ~Cleanup() {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }
  } cleanup{file};

  ASSERT_TRUE(writer->write(file, validJson).hasValue());
  muthechords::PatternLibrary good(file);
  EXPECT_TRUE(good.error().empty()) << good.error();
  ASSERT_EQ(good.patterns().size(), 1u);

  const auto &pattern = good.patterns().front();
  EXPECT_EQ(pattern.sourceRoot, 48);
  EXPECT_DOUBLE_EQ(pattern.events[0].beat, .125);
  EXPECT_DOUBLE_EQ(pattern.events[0].duration, .75);
  EXPECT_EQ(pattern.events[0].note, 55);
  EXPECT_EQ(pattern.events[0].velocity, 93);

  for (const auto &bad :
       {replaceFirst(validJson, "\"version\":1", "\"version\":2"),
        replaceFirst(validJson, "\"note\":55", "\"note\":128"),
        replaceFirst(validJson, "\"velocity\":93", "\"velocity\":0"),
        replaceFirst(validJson, "\"duration\":0.75", "\"duration\":-1"),
        replaceFirst(validJson, "\"denominator\":4", "\"denominator\":3"),
        replaceFirst(validJson, "\"beat\":0.125", "\"beat\":4"),
        replaceFirst(validJson, "\"note\":55", "\"note\":true"), std::string("{broken")}) {
    ASSERT_TRUE(writer->write(file, bad).hasValue());
    muthechords::PatternLibrary rejected(file);
    EXPECT_FALSE(rejected.error().empty());
    EXPECT_TRUE(rejected.patterns().empty());
  }

  EXPECT_EQ(good.patterns().size(), 1u);
  std::filesystem::remove(file);
  muthechords::PatternLibrary missing(file);
  EXPECT_FALSE(missing.error().empty());
  EXPECT_TRUE(missing.patterns().empty());
}
