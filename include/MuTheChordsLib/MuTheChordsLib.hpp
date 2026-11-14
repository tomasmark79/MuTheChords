#pragma once

#include <MuTheChordsLib/ChordEngine.hpp>
#include <MuTheChordsLib/ChordNames.hpp>
#include <MuTheChordsLib/ChordSnapshot.hpp>
#include <MuTheChordsLib/DisplayMailbox.hpp>
#include <MuTheChordsLib/HarmonySync.hpp>
#include <MuTheChordsLib/MidiDiagnostics.hpp>
#include <MuTheChordsLib/NoteNames.hpp>
#include <MuTheChordsLib/PatternLibrary.hpp>
#include <MuTheChordsLib/PerformanceEngine.hpp>
#include <MuTheChordsLib/PerformanceTypes.hpp>
#include <MuTheChordsLib/Scales.hpp>
#include <MuTheChordsLib/Voicing.hpp>
#include <Utils/UtilsFactory.hpp>

#include <memory>

namespace muthechords::v1 {
  using namespace muthechords::utils;

  class MuTheChordsLib {
  public:
    explicit MuTheChordsLib(const UtilsFactory::ApplicationContext &context);
    ~MuTheChordsLib();
    MuTheChordsLib(const MuTheChordsLib &other) = delete;
    MuTheChordsLib &operator=(const MuTheChordsLib &other) = delete;
    MuTheChordsLib(MuTheChordsLib &&other) = delete;
    MuTheChordsLib &operator=(MuTheChordsLib &&other) = delete;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]]
    const std::shared_ptr<muthechords::assets::IAssetManager> &getAssetManager() const noexcept;
    [[nodiscard]] const Engine &getEngine() const noexcept { return engine_; }
    [[nodiscard]] Engine &getEngine() noexcept { return engine_; }
    [[nodiscard]] const PerformanceEngine &getPerformanceEngine() const noexcept {
      return performanceEngine_;
    }
    [[nodiscard]] PerformanceEngine &getPerformanceEngine() noexcept { return performanceEngine_; }

    void performLibraryTask();
    [[nodiscard]] ChordSnapshot computeChord(int midiNote, const Settings &settings = {});

  private:
    bool isInitialized_{false};
    static constexpr const char *libName_ = "MuTheChordsLib v0.6.6";

    std::shared_ptr<muthechords::logging::ILogger> logger_;
    std::shared_ptr<muthechords::assets::IAssetManager> assetManager_;
    Engine engine_;
    PerformanceEngine performanceEngine_;
  };

} // namespace muthechords::v1
