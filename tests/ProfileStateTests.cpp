#include "ProfileState.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
  void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
  }
  using APVTS = juce::AudioProcessorValueTreeState;
  class Processor final : public juce::AudioProcessor {
  public:
    explicit Processor(int patternCount = 3)
        : parameters(*this, nullptr, "TestState", layout(patternCount)) {}
    APVTS parameters;
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
    juce::AudioProcessorEditor *createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Profile tests"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override {}
    void setStateInformation(const void *, int) override {}

  private:
    static APVTS::ParameterLayout layout(int patternCount) {
      APVTS::ParameterLayout result;
      auto integer = [&](const char *id, int low, int high, int value) {
        result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, id, low,
                                                             high, value));
      };
      integer("root", 0, 11, 0);
      integer("transpose", -36, 36, 0);
      integer("low", 0, 127, 36);
      integer("high", 0, 127, 59);
      integer("inversion", 0, 6, 0);
      for (const auto *id : {"scale", "chord", "mapping", "outside", "pedal", "alt", "mode",
                             "polarity", "spelling", "perfRate", "perfDivision", "perfVoices",
                             "perfPassing", "perfChange", "perfTransition", "perfCategory"})
        result.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{id, 1}, id, juce::StringArray{"One", "Two", "Three"}, 0));
      for (const auto *id : {"full", "latch", "perfPreview", "harmonySync"})
        result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, id, false));
      juce::StringArray choices;
      for (int i = 0; i < std::max(2, patternCount); ++i)
        choices.add(juce::String(i));
      result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"perfPattern", 1},
                                                              "Pattern", choices, 0));
      return result;
    }
  };
  void set(Processor &processor, const char *id, float value) {
    auto *parameter = processor.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
  }
  double value(const juce::ValueTree &state, const char *id) {
    return static_cast<double>(state.getChildWithProperty("id", id).getProperty("value"));
  }
  muthechords::Pattern pattern(const char *id, const char *category) {
    muthechords::Pattern result;
    result.id = id;
    result.name = id;
    result.category = category;
    result.events.push_back({0, 1, 60, 100});
    return result;
  }
} // namespace

int main() {
  try {
    const std::vector<muthechords::Pattern> patterns{pattern("a", "Keys"), pattern("b", "Guitar"),
                                                     pattern("c", "Keys")};
    Processor original;
    for (const auto child : original.parameters.copyState()) {
      const auto id = child.getProperty("id").toString();
      set(original, id.toRawUTF8(), id == "transpose" ? -12.0f : 1.0f);
    }
    set(original, "root", 9);
    set(original, "spelling", 2);
    set(original, "perfVoices", 2);
    {
      // Host state is serialized through XML, whose attributes restore as
      // strings even though the live parameters still have numeric values.
      const auto xml = original.parameters.copyState().createXml();
      original.parameters.replaceState(juce::ValueTree::fromXml(*xml));
      juce::ValueTree roundTrip;
      const auto result = muthechords::ProfileState::decode(
          muthechords::ProfileState::encode(original.parameters, patterns, 2), original.parameters,
          patterns, roundTrip);
      if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
      require(value(roundTrip, "alt") == 1 && value(roundTrip, "root") == 9,
              "XML host restore survives profile round trip");
    }
    // Explicit selection wins over a stale APVTS index/category.
    const auto json = muthechords::ProfileState::encode(original.parameters, patterns, 2);
    auto document = juce::JSON::parse(json);
    require(document["format"].toString() == "MuTheChordsProfile" &&
                static_cast<int>(document["version"]) == 2,
            "versioned format");
    require(document["patternId"].toString() == "c", "stable pattern ID exported");
    auto *properties = document["parameters"].getDynamicObject();
    require(properties != nullptr && !properties->hasProperty("perfPreview"),
            "live preview excluded");
    for (const auto &property : properties->getProperties())
      require(property.value.isInt() || property.value.isInt64() || property.value.isDouble(),
              "XML-restored export values are numeric");
    require(static_cast<double>(properties->getProperty("harmonySync")) == 1,
            "profile includes harmony sync");
    require(static_cast<int>(properties->getProperty("perfPattern")) == 2 &&
                static_cast<int>(properties->getProperty("perfCategory")) == 0,
            "selection and category derived on export");
    require(!document.getDynamicObject()->hasProperty("events") &&
                !document.getDynamicObject()->hasProperty("patterns"),
            "library content excluded");

    Processor restored;
    set(restored, "perfPreview", 1);
    const auto liveBefore = restored.parameters.copyState();
    juce::ValueTree decoded;
    require(muthechords::ProfileState::decode(json, restored.parameters, patterns, decoded).wasOk(),
            "round trip");
    require(restored.parameters.copyState().isEquivalentTo(liveBefore),
            "successful decode leaves live state unchanged");
    for (const auto &property : properties->getProperties()) {
      const auto id = property.name.toString();
      require(value(decoded, id.toRawUTF8()) == static_cast<double>(property.value),
              "all persistent parameters round trip");
    }
    require(value(decoded, "perfPreview") == 0 &&
                decoded.getProperty("patternId").toString() == "c",
            "restored preview is stopped");
    restored.parameters.replaceState(decoded);
    require(restored.parameters.getRawParameterValue("root")->load() == 9 &&
                restored.parameters.getRawParameterValue("perfVoices")->load() == 2,
            "validated state is replaceable");

    const std::vector<muthechords::Pattern> reordered{patterns[2], patterns[1]};
    Processor smaller(2);
    require(muthechords::ProfileState::decode(json, smaller.parameters, reordered, decoded).wasOk(),
            "stable ID survives smaller reordered library");
    require(value(decoded, "perfPattern") == 0 && value(decoded, "perfCategory") == 0,
            "stable ID overrides old index");
    const std::vector<muthechords::Pattern> categoryReordered{patterns[1], patterns[2],
                                                              patterns[0]};
    require(muthechords::ProfileState::decode(json, restored.parameters, categoryReordered, decoded)
                .wasOk(),
            "category reordering");
    require(value(decoded, "perfPattern") == 1 && value(decoded, "perfCategory") == 1,
            "category follows current pattern");

    auto invalid = [&](const juce::String &broken) {
      auto untouched = juce::ValueTree("Sentinel");
      untouched.setProperty("keep", 42, nullptr);
      const auto before = untouched.createCopy();
      const auto live = restored.parameters.copyState();
      require(muthechords::ProfileState::decode(broken, restored.parameters, patterns, untouched)
                  .failed(),
              "invalid profile rejected");
      require(untouched.isEquivalentTo(before) &&
                  restored.parameters.copyState().isEquivalentTo(live),
              "failure is atomic");
    };
    auto changed = [&](const std::function<void(juce::DynamicObject &)> &mutate) {
      auto copy = juce::JSON::parse(json);
      mutate(*copy.getDynamicObject());
      invalid(juce::JSON::toString(copy));
    };
    invalid("{");
    invalid("[]");
    invalid("null");
    changed([](auto &o) { o.setProperty("format", "Other"); });
    changed([](auto &o) { o.setProperty("version", 1); });
    changed([](auto &o) { o.setProperty("version", 3); });
    auto oldVersion = juce::JSON::parse(json);
    oldVersion.getDynamicObject()->setProperty("version", 1);
    require(muthechords::ProfileState::decode(juce::JSON::toString(oldVersion), restored.parameters,
                                              patterns, decoded)
                    .getErrorMessage() == "Unsupported profile version",
            "v1 is explicitly unsupported");
    changed([](auto &o) { o.setProperty("version", "1"); });
    changed([](auto &o) { o.setProperty("parameters", juce::Array<juce::var>{}); });
    changed([](auto &o) { o.removeProperty("parameters"); });
    changed([](auto &o) { o.removeProperty("patternId"); });
    changed([](auto &o) { o.setProperty("patternId", "missing"); });
    changed([](auto &o) { o.setProperty("patternId", ""); });
    changed([](auto &o) { o.setProperty("categoryName", 7); });
    changed([](auto &o) { o.setProperty("activePlayback", true); });
    changed([](auto &o) {
      o.getProperty("parameters").getDynamicObject()->removeProperty("spelling");
    });
    changed([](auto &o) {
      o.getProperty("parameters").getDynamicObject()->removeProperty("harmonySync");
    });
    for (const juce::var bad : {juce::var("9"), juce::var(true), juce::var(), juce::var(12),
                                juce::var(-1), juce::var(1.25), juce::var(1.000000001)})
      changed([&](auto &o) {
        o.getProperty("parameters").getDynamicObject()->setProperty("root", bad);
      });
    changed(
        [](auto &o) { o.getProperty("parameters").getDynamicObject()->setProperty("alt", "2.0"); });
    changed(
        [](auto &o) { o.getProperty("parameters").getDynamicObject()->setProperty("full", .5); });
    changed([](auto &o) {
      o.getProperty("parameters").getDynamicObject()->setProperty("spelling", 1.5);
    });
    changed([](auto &o) {
      o.getProperty("parameters").getDynamicObject()->setProperty("perfPattern", -.5);
    });
    changed(
        [](auto &o) { o.getProperty("parameters").getDynamicObject()->setProperty("unknown", 0); });
    changed([](auto &o) {
      o.getProperty("parameters").getDynamicObject()->setProperty("perfPreview", 1);
    });
    auto inactivePreview = juce::JSON::parse(json);
    inactivePreview["parameters"].getDynamicObject()->setProperty("perfPreview", 0);
    require(muthechords::ProfileState::decode(juce::JSON::toString(inactivePreview),
                                              restored.parameters, patterns, decoded)
                    .wasOk() &&
                value(decoded, "perfPreview") == 0,
            "explicit inactive preview is harmless");
    auto overflow = json.replace("\"root\": 9", "\"root\": 1e999");
    require(overflow != json, "nonfinite test replacement");
    invalid(overflow);

    Processor empty;
    const auto emptyJSON = muthechords::ProfileState::encode(empty.parameters, {}, -1);
    require(muthechords::ProfileState::decode(emptyJSON, empty.parameters, {}, decoded).wasOk(),
            "Chords profile with empty library");
    require(value(decoded, "perfPattern") == 0 && value(decoded, "perfCategory") == 0,
            "empty library positions");
    require(muthechords::ProfileState::decode(json, empty.parameters, {}, decoded).failed(),
            "nonempty ID requires installed pattern");
    std::cout << "Profile format, complete settings, stable IDs, validation "
                 "and atomicity checks passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
