#include "ProfileState.h"
#include <Utils/UtilsFactory.hpp>
#include <cmath>
#include <limits>

namespace muthechords {
  namespace {
    bool numeric(const juce::var &value) {
      return value.isInt() || value.isInt64() || value.isDouble();
    }

    juce::StringArray categories(const std::vector<Pattern> &patterns) {
      juce::StringArray result;
      for (const auto &pattern : patterns) {
        result.addIfNotAlreadyThere(juce::String(pattern.category));
      }
      return result;
    }

    int categoryIndex(const std::vector<Pattern> &patterns, int selected) {
      return selected >= 0 && selected < static_cast<int>(patterns.size())
                 ? categories(patterns).indexOf(
                       juce::String(patterns[static_cast<size_t>(selected)].category))
                 : 0;
    }

    juce::Result fail(const juce::String &message) { return juce::Result::fail(message); }
  } // namespace

  juce::String ProfileState::encode(juce::AudioProcessorValueTreeState &parameters,
                                    const std::vector<Pattern> &patterns, int selectedPattern,
                                    const juce::ValueTree &loopRegions) {
    auto state = parameters.copyState();
    auto *values = new juce::DynamicObject;
    for (const auto child : state) {
      const auto id = child.getProperty("id").toString();
      if (id.isNotEmpty() && id != "perfPreview") {
        if (auto *parameter = parameters.getParameter(id)) {
          values->setProperty(
              id, static_cast<double>(parameter->convertFrom0to1(parameter->getValue())));
        }
      }
    }
    const bool selected =
        selectedPattern >= 0 && selectedPattern < static_cast<int>(patterns.size());
    values->setProperty("perfPattern", selected ? selectedPattern : 0);
    if (parameters.getParameter("perfLoopStart") && parameters.getParameter("perfLoopEnd")) {
      int start = 0, end = 0;
      if (selected) {
        for (const auto entry : loopRegions) {
          if (entry.getProperty("id").toString() ==
              juce::String(patterns[static_cast<size_t>(selectedPattern)].id)) {
            start = static_cast<int>(entry.getProperty("start", 0));
            end = static_cast<int>(entry.getProperty("end", 0));
            break;
          }
        }
      }
      values->setProperty("perfLoopStart", start);
      values->setProperty("perfLoopEnd", end);
    }
    if (parameters.getParameter("perfCategory") != nullptr) {
      values->setProperty("perfCategory", categoryIndex(patterns, selectedPattern));
    }
    auto *object = new juce::DynamicObject;
    object->setProperty("format", "MuTheChordsProfile");
    object->setProperty("version", 2);
    object->setProperty("parameters", juce::var(values));
    object->setProperty("patternId",
                        selected ? juce::String(patterns[static_cast<size_t>(selectedPattern)].id)
                                 : juce::String());
    juce::Array<juce::var> regions;
    for (const auto entry : loopRegions) {
      auto *row = new juce::DynamicObject;
      row->setProperty("id", entry.getProperty("id"));
      row->setProperty("start", entry.getProperty("start"));
      row->setProperty("end", entry.getProperty("end"));
      regions.add(juce::var(row));
    }
    object->setProperty("loopRegions", regions);
    if (selected) {
      object->setProperty("categoryName",
                          juce::String(patterns[static_cast<size_t>(selectedPattern)].category));
    }
    return juce::JSON::toString(juce::var(object));
  }

  juce::Result ProfileState::decode(const juce::String &json,
                                    juce::AudioProcessorValueTreeState &parameters,
                                    const std::vector<Pattern> &patterns,
                                    juce::ValueTree &validatedState) {
    juce::var document;
    const auto parsed = juce::JSON::parse(json, document);
    if (parsed.failed()) {
      return fail("Invalid profile JSON: " + parsed.getErrorMessage());
    }
    auto *object = document.getDynamicObject();
    if (object == nullptr) {
      return fail("Profile must be a JSON object");
    }
    const auto format = object->getProperty("format");
    if (!format.isString() || (format.toString() != "MuTheChordsProfile" &&
                               format.toString() != "ChordInstrumentProfile")) {
      return fail("Unknown profile format");
    }
    const auto version = object->getProperty("version");
    if (!numeric(version) || static_cast<double>(version) != 2) {
      return fail("Unsupported profile version");
    }
    for (const auto &property : object->getProperties()) {
      const auto name = property.name.toString();
      if (name != "format" && name != "version" && name != "parameters" && name != "patternId" &&
          name != "categoryName" && name != "loopRegions") {
        return fail("Unknown profile field: " + name);
      }
    }
    if (object->hasProperty("categoryName") && !object->getProperty("categoryName").isString()) {
      return fail("Invalid categoryName");
    }
    auto *values = object->getProperty("parameters").getDynamicObject();
    if (values == nullptr) {
      return fail("Profile parameters must be an object");
    }
    const auto idValue = object->getProperty("patternId");
    if (!idValue.isString()) {
      return fail("Missing or invalid patternId");
    }
    const auto patternId = idValue.toString();
    int selected = -1;
    if (patternId.isEmpty()) {
      if (!patterns.empty()) {
        return fail("Profile has no pattern ID for the installed library");
      }
    } else {
      for (size_t i = 0; i < patterns.size(); ++i) {
        if (juce::String(patterns[i].id) == patternId) {
          selected = static_cast<int>(i);
          break;
        }
      }
      if (selected < 0) {
        return fail("Profile pattern is not installed: " + patternId);
      }
    }

    auto candidate = parameters.copyState();
    for (auto child : candidate) {
      const auto id = child.getProperty("id").toString();
      if (id.isNotEmpty() && id != "perfPreview" && parameters.getParameter(id) != nullptr &&
          !values->hasProperty(id)) {
        if (id == "perfPlayback" || id == "perfLoopStart" || id == "perfLoopEnd" ||
            id == "voicingPreset" || id == "strum") {
          auto *p = parameters.getParameter(id);
          child.setProperty("value", p->convertFrom0to1(p->getDefaultValue()), nullptr);
        } else {
          return fail("Missing profile parameter: " + id);
        }
      }
    }
    for (const auto &property : values->getProperties()) {
      const auto id = property.name.toString();
      auto *parameter = parameters.getParameter(id);
      if (parameter == nullptr) {
        return fail("Unknown profile parameter: " + id);
      }
      if (!numeric(property.value)) {
        return fail("Profile parameter must be numeric: " + id);
      }
      const double value = static_cast<double>(property.value);
      if (!std::isfinite(value)) {
        return fail("Non-finite profile parameter: " + id);
      }
      if (id == "perfPreview") {
        if (value != 0) {
          return fail("Profiles cannot start playback");
        }
        continue;
      }
      if (id == "perfPattern" || id == "perfCategory") {
        if (value < 0 || value > std::numeric_limits<int>::max() || std::floor(value) != value) {
          return fail("Invalid profile library index: " + id);
        }
        if (patterns.empty() && value != 0) {
          return fail("Empty-library profile indices must be zero");
        }
      } else {
        const double low = parameter->convertFrom0to1(0), high = parameter->convertFrom0to1(1);
        if (value < low || value > high) {
          return fail("Profile parameter out of range: " + id);
        }
        const auto &range = parameter->getNormalisableRange();
        const bool integerParameter =
            dynamic_cast<juce::AudioParameterInt *>(parameter) != nullptr ||
            dynamic_cast<juce::AudioParameterChoice *>(parameter) != nullptr ||
            dynamic_cast<juce::AudioParameterBool *>(parameter) != nullptr;
        if (integerParameter && std::floor(value) != value) {
          return fail("Invalid discrete profile parameter: " + id);
        }
        if (range.interval > 0) {
          const double steps = (value - range.start) / range.interval;
          if (std::abs(steps - std::round(steps)) > 1.0e-6) {
            return fail("Invalid discrete profile parameter: " + id);
          }
        } else if (parameter->isDiscrete() && std::floor(value) != value) {
          return fail("Invalid discrete profile parameter: " + id);
        }
      }
      auto child = candidate.getChildWithProperty("id", id);
      if (!child.isValid()) {
        return fail("Missing parameter state: " + id);
      }
      child.setProperty("value", value, nullptr);
    }
    candidate.getChildWithProperty("id", "perfPattern")
        .setProperty("value", std::max(0, selected), nullptr);
    auto category = candidate.getChildWithProperty("id", "perfCategory");
    if (category.isValid()) {
      category.setProperty("value", categoryIndex(patterns, selected), nullptr);
    }
    auto preview = candidate.getChildWithProperty("id", "perfPreview");
    if (preview.isValid()) {
      preview.setProperty("value", 0, nullptr);
    }
    candidate.setProperty("patternId", patternId, nullptr);
    auto oldRegions = candidate.getChildWithName("LoopRegions");
    if (oldRegions.isValid()) {
      candidate.removeChild(oldRegions, nullptr);
    }
    juce::ValueTree regions("LoopRegions");
    if (object->hasProperty("loopRegions")) {
      const auto *rows = object->getProperty("loopRegions").getArray();
      if (!rows || rows->size() > static_cast<int>(patterns.size())) {
        return fail("Invalid loop regions");
      }
      juce::StringArray seen;
      for (const auto &value : *rows) {
        auto *row = value.getDynamicObject();
        if (!row || row->getProperties().size() != 3 || !row->getProperty("id").isString()) {
          return fail("Invalid loop region");
        }
        const auto id = row->getProperty("id").toString();
        const auto start = row->getProperty("start"), end = row->getProperty("end");
        if (seen.contains(id) || !numeric(start) || !numeric(end)) {
          return fail("Invalid loop region");
        }
        const double a = static_cast<double>(start), b = static_cast<double>(end);
        if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b < 0 || a > 800000 || b > 800000 ||
            std::floor(a) != a || std::floor(b) != b) {
          return fail("Invalid loop region");
        }
        bool exists = false;
        for (const auto &p : patterns) {
          if (juce::String(p.id) == id) {
            exists = true;
            break;
          }
        }
        if (!exists) {
          return fail("Loop region pattern is not installed");
        }
        seen.add(id);
        juce::ValueTree entry("Region");
        entry.setProperty("id", id, nullptr);
        entry.setProperty("start", static_cast<int>(a), nullptr);
        entry.setProperty("end", static_cast<int>(b), nullptr);
        regions.addChild(entry, -1, nullptr);
      }
    }
    candidate.addChild(regions, -1, nullptr);
    validatedState = candidate;
    return juce::Result::ok();
  }

} // namespace muthechords
