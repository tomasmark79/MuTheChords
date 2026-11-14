#include <MuTheChordsLib/MuTheChordsLib.hpp>

namespace muthechords::v1 {

  MuTheChordsLib::MuTheChordsLib(const UtilsFactory::ApplicationContext &context)
      : logger_(context.logger ? context.logger
                               : std::make_shared<muthechords::logging::NullLogger>()),
        assetManager_(context.assetManager) {

    if (!assetManager_ || !assetManager_->validate()) {
      logger_->errorStream() << "Invalid or missing asset manager";
      return;
    }
    logger_->infoStream() << libName_ << " initialized (" << scaleCount << " scales, "
                          << voicingCount << " voicings) ...";
    isInitialized_ = true;
  }

  MuTheChordsLib::~MuTheChordsLib() {
    if (isInitialized_) {
      logger_->infoStream() << libName_ << " ... destructed";
    } else {
      logger_->infoStream() << libName_ << " ... (not initialized) destructed";
    }
  }

  bool MuTheChordsLib::isInitialized() const noexcept { return isInitialized_; }

  const std::shared_ptr<muthechords::assets::IAssetManager> &
      MuTheChordsLib::getAssetManager() const noexcept {
    return assetManager_;
  }

  void MuTheChordsLib::performLibraryTask() {
    if (!isInitialized_) {
      logger_->errorStream() << "Cannot perform task: " << libName_ << " is not initialized";
      return;
    }
    logger_->infoStream() << "Performing a task in " << libName_ << " using the asset manager";

    const char *assetName = "MuTheChordsLogo.svg";
    auto asset = assetManager_->getAssetsPath() / assetName;
    if (std::filesystem::exists(asset)) {
      logger_->infoStream() << "Successfully loaded asset: " << assetName;
    } else {
      logger_->errorStream() << "Failed to load asset: " << assetName;
    }

    Settings s = engine_.settings();
    s.full = true;
    engine_.configure(s);
    engine_.noteOn(1, 60, 100);
    const auto snap = engine_.snapshot();
    logger_->infoStream() << "Sample C major triad: " << chordName(snap, Spelling::Auto) << " ("
                          << snap.count << " notes)";
    engine_.noteOff(1, 60);
  }

  ChordSnapshot MuTheChordsLib::computeChord(int midiNote, const Settings &settings) {
    engine_.reset(true);
    engine_.configure(settings);
    engine_.noteOn(1, midiNote, 100);
    const auto snap = engine_.snapshot();
    engine_.noteOff(1, midiNote);
    return snap;
  }

} // namespace muthechords::v1
