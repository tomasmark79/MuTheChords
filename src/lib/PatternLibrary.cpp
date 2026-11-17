#include <MuTheChordsLib/PatternLibrary.hpp>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

#if defined(_WIN32)
#include <Utils/Platform/WindowsHeaders.hpp>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#if CHORD_EMBED_PRIVATE_PATTERNS
#include "ChordPrivatePatterns.h"
#endif

namespace muthechords {
  namespace {
    constexpr auto primaryFilename = "MuTheChords.patterns.json";
    constexpr auto fallbackFilename = "Chord Instrument.patterns.json";
    constexpr double maxBeats = 100000;

    std::filesystem::path currentModuleDirectory() {
#if defined(_WIN32)
      HMODULE hm = nullptr;
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&currentModuleDirectory), &hm) != 0) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(hm, path, MAX_PATH) != 0) {
          return std::filesystem::path(path).parent_path();
        }
      }
#elif defined(__linux__) || defined(__APPLE__)
      Dl_info info{};
      if (dladdr(reinterpret_cast<const void *>(&currentModuleDirectory), &info) != 0 &&
          info.dli_fname != nullptr) {
        std::error_code ec;
        auto p = std::filesystem::canonical(info.dli_fname, ec);
        if (!ec) {
          return p.parent_path();
        }
        return std::filesystem::path(info.dli_fname).parent_path();
      }
#endif
      auto platformInfo = utils::UtilsFactory::createPlatformInfo();
      auto execDir = platformInfo->getExecutableDirectory();
      if (execDir.hasValue()) {
        return execDir.value();
      }
      return std::filesystem::current_path();
    }

    double numeric(const nlohmann::json &object, const char *key, double low, double high,
                   bool integer = false) {
      if (!object.is_object() || !object.contains(key)) {
        throw std::runtime_error(std::string("Missing numeric field: ") + key);
      }
      const auto &value = object.at(key);
      if (!value.is_number() || value.is_boolean()) {
        throw std::runtime_error(std::string("Missing numeric field: ") + key);
      }
      const double n = value.get<double>();
      if (!std::isfinite(n) || n < low || n > high || (integer && std::floor(n) != n)) {
        throw std::runtime_error(std::string("Invalid numeric field: ") + key);
      }
      return n;
    }

    std::string stringField(const nlohmann::json &object, const char *key,
                            bool allowEmpty = false) {
      if (!object.is_object() || !object.contains(key)) {
        if (allowEmpty) {
          return {};
        }
        throw std::runtime_error(std::string("Invalid string field: ") + key);
      }
      const auto &value = object.at(key);
      if (!value.is_string()) {
        throw std::runtime_error(std::string("Invalid string field: ") + key);
      }
      const auto str = value.get<std::string>();
      if ((!allowEmpty && str.empty()) || str.size() > 4096) {
        throw std::runtime_error(std::string("Invalid string field: ") + key);
      }
      return str;
    }
  } // namespace

  std::filesystem::path PatternLibrary::discoverFile() {
    if (const char *env = std::getenv("MUTHECHORDS_PATTERN_LIBRARY"); env && *env) {
      std::filesystem::path p(env);
      return p.is_absolute() ? p : (std::filesystem::current_path() / p);
    }
    if (const char *env = std::getenv("CHORD_PATTERN_LIBRARY"); env && *env) {
      std::filesystem::path p(env);
      return p.is_absolute() ? p : (std::filesystem::current_path() / p);
    }

    auto parent = currentModuleDirectory();
    for (int i = 0; i < 8; ++i) {
      for (const char *fname : {primaryFilename, fallbackFilename}) {
        const auto candidate = parent / fname;
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
          return candidate;
        }
      }
      const auto next = parent.parent_path();
      if (next == parent || next.empty()) {
        break;
      }
      parent = next;
    }
    return {};
  }

  PatternLibrary::PatternLibrary()
      : fileReader_(utils::UtilsFactory::createFileReader()),
        jsonSerializer_(utils::UtilsFactory::createJsonSerializer()) {
#if CHORD_EMBED_PRIVATE_PATTERNS
    const char *env1 = std::getenv("MUTHECHORDS_PATTERN_LIBRARY");
    const char *env2 = std::getenv("CHORD_PATTERN_LIBRARY");
    if ((!env1 || !*env1) && (!env2 || !*env2)) {
      int size = 0;
      const auto *json = chord_private_patterns::getNamedResource(
          chord_private_patterns::namedResourceList[0], size);
      loadJson(std::string(json, static_cast<size_t>(size)));
      return;
    }
#endif
    loadFile(discoverFile());
  }

  PatternLibrary::PatternLibrary(const std::filesystem::path &file,
                                 std::shared_ptr<utils::IFileReader> fileReader,
                                 std::shared_ptr<utils::IJsonSerializer> jsonSerializer)
      : fileReader_(fileReader ? std::move(fileReader) : utils::UtilsFactory::createFileReader()),
        jsonSerializer_(jsonSerializer ? std::move(jsonSerializer)
                                       : utils::UtilsFactory::createJsonSerializer()) {
    loadFile(file);
  }

  void PatternLibrary::loadFromJsonString(const std::string &json) {
    data.clear();
    failure.clear();
    loadJson(json);
  }

  void PatternLibrary::loadFile(const std::filesystem::path &file) {
    try {
      if (file.empty() || !fileReader_->exists(file)) {
        throw std::runtime_error(
            "External pattern library not found. Set MUTHECHORDS_PATTERN_LIBRARY or "
            "place MuTheChords.patterns.json alongside the plugin.");
      }
      auto sizeRes = fileReader_->getSize(file);
      if (!sizeRes || sizeRes.value() > 64ULL * 1024ULL * 1024ULL) {
        throw std::runtime_error("Pattern library exceeds 64 MiB");
      }
      auto contentRes = fileReader_->read(file);
      if (!contentRes) {
        throw std::runtime_error(contentRes.error().message);
      }
      loadJson(contentRes.value());
    } catch (const std::exception &e) {
      failure = e.what();
    }
  }

  void PatternLibrary::loadJson(const std::string &json) {
    try {
      auto parsedRes = jsonSerializer_->parse(json);
      if (!parsedRes) {
        throw std::runtime_error(parsedRes.error().toString());
      }
      const auto &document = parsedRes.value();
      if (numeric(document, "version", 1, 1, true) != 1) {
        throw std::runtime_error("Unsupported pattern library version");
      }
      if (!document.contains("patterns") || !document["patterns"].is_array()) {
        throw std::runtime_error("Invalid pattern count");
      }
      const auto &patterns = document["patterns"];
      if (patterns.empty() || patterns.size() > 10000) {
        throw std::runtime_error("Invalid pattern count");
      }
      std::vector<Pattern> loaded;
      loaded.reserve(patterns.size());
      std::set<std::string> ids;
      size_t totalEvents = 0;
      for (const auto &value : patterns) {
        Pattern pattern;
        pattern.id = stringField(value, "id");
        if (!ids.insert(pattern.id).second) {
          throw std::runtime_error("Duplicate pattern ID");
        }
        pattern.name = stringField(value, "name");
        pattern.category = stringField(value, "category");
        pattern.group = stringField(value, "group", true);
        pattern.length = numeric(value, "length", .000001, maxBeats);
        pattern.numerator = static_cast<int>(numeric(value, "numerator", 1, 32, true));
        pattern.denominator = static_cast<int>(numeric(value, "denominator", 1, 32, true));
        if ((pattern.denominator & (pattern.denominator - 1)) != 0) {
          throw std::runtime_error("Invalid time signature denominator");
        }
        pattern.sourceRoot = static_cast<int>(numeric(value, "sourceRoot", 0, 127, true));
        if (!value.contains("events") || !value["events"].is_array()) {
          throw std::runtime_error("Invalid event count");
        }
        const auto &events = value["events"];
        if (events.empty() || events.size() > 100000) {
          throw std::runtime_error("Invalid event count");
        }
        totalEvents += events.size();
        if (totalEvents > 1000000) {
          throw std::runtime_error("Too many pattern events");
        }
        pattern.events.reserve(events.size());
        for (const auto &event : events) {
          PatternNote note;
          note.beat = numeric(event, "beat", 0, maxBeats);
          note.duration = numeric(event, "duration", 0, maxBeats);
          note.note = static_cast<int>(numeric(event, "note", 0, 127, true));
          note.velocity = static_cast<int>(numeric(event, "velocity", 1, 127, true));
          if (note.beat + note.duration > pattern.length + 1e-8) {
            throw std::runtime_error("Event extends beyond pattern length");
          }
          pattern.events.push_back(note);
        }
        pattern.playback = pattern.preparePlayback();
        loaded.push_back(std::move(pattern));
      }
      data = std::move(loaded);
    } catch (const std::exception &e) {
      failure = e.what();
    }
  }

} // namespace muthechords
