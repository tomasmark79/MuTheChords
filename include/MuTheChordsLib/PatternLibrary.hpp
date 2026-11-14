#pragma once

#include <MuTheChordsLib/PerformanceTypes.hpp>
#include <Utils/UtilsFactory.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace muthechords {

  // Construct only off the audio thread. All file I/O and validation finish here;
  // the resulting vectors remain immutable throughout the processor lifetime.
  class PatternLibrary {
  public:
    PatternLibrary();
    explicit PatternLibrary(const std::filesystem::path &file,
                            std::shared_ptr<utils::IFileReader> fileReader = nullptr,
                            std::shared_ptr<utils::IJsonSerializer> jsonSerializer = nullptr);

    [[nodiscard]] const std::vector<Pattern> &patterns() const noexcept { return data; }
    [[nodiscard]] const std::string &error() const noexcept { return failure; }
    [[nodiscard]] static std::filesystem::path discoverFile();

    void loadFromJsonString(const std::string &json);

  private:
    void loadFile(const std::filesystem::path &file);
    void loadJson(const std::string &json);

    std::shared_ptr<utils::IFileReader> fileReader_;
    std::shared_ptr<utils::IJsonSerializer> jsonSerializer_;
    std::vector<Pattern> data;
    std::string failure;
  };

} // namespace muthechords
