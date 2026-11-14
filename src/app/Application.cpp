#include <MuTheChordsLib/ChordNames.hpp>
#include <MuTheChordsLib/MuTheChordsLib.hpp>
#include <MuTheChordsLib/NoteNames.hpp>
#include <Utils/UtilsFactory.hpp>
#include <cxxopts.hpp>
#include <iostream>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/version.h>
#endif

const std::string appName = "MuTheChords";
const std::string NA = "[Not Found]";

int main(int argc, char **argv) {
  using namespace muthechords;
  using namespace muthechords::logging;
  using namespace muthechords::utils;

  try {
    cxxopts::Options options(appName, "MuTheChords - Music Theory Chords C++ Application");
    options.add_options()("h,help", "Print usage");
    options.add_options()("w,write2file", "Write output to file",
                          cxxopts::value<bool>()->default_value("false"));
    options.add_options()("s,scales", "List all available musical scales",
                          cxxopts::value<bool>()->default_value("false"));
    options.add_options()("n,note", "Trigger MIDI note for chord preview (0-127)",
                          cxxopts::value<int>()->default_value("60"));
    auto result = options.parse(argc, argv);

#if !defined(__EMSCRIPTEN__)
    if (result.contains("help")) {
#else
    if (result.count("help") > 0) {
#endif
      std::cout << options.help() << '\n';
      return EXIT_SUCCESS;
    }

    auto ctx = UtilsFactory::createFullContext(
        appName, LoggerConfig{.level = Level::LOG_INFO,
                              .enableFileLogging = result["write2file"].as<bool>(),
                              .logFilePath = "application.log",
                              .colorOutput = true,
                              .appPrefix = appName});

    ctx.logger->infoStream()
        << appName << " (c) "
        << ctx.customStringsLoader->getLocalizedString("Author", "cs").value_or(NA) << " - "
        << ctx.customStringsLoader->getLocalizedString("GitHub", "cs").value_or(NA) << ": "
        << ctx.customStringsLoader->getCustomKey("GitHub", "url").value_or(NA);
    ctx.logger->infoStream() << ctx.platformInfo->getPlatformName() << " platform detected.";
#if defined(__EMSCRIPTEN__)
    ctx.logger->infoStream() << "Compiled with Emscripten " << __EMSCRIPTEN_MAJOR__ << '.'
                             << __EMSCRIPTEN_MINOR__ << '.' << __EMSCRIPTEN_TINY__;
#endif

    auto library = std::make_unique<muthechords::v1::MuTheChordsLib>(ctx);
    if (!library->isInitialized()) {
      ctx.logger->errorStream() << "Library initialization failed";
      return EXIT_FAILURE;
    }

    if (result["scales"].as<bool>()) {
      ctx.logger->infoStream() << "Available scales (" << scaleCount << "):";
      for (int i = 0; i < scaleCount; ++i) {
        ctx.logger->infoStream() << "  [" << i << "] " << scaleNames[static_cast<size_t>(i)];
      }
    }

    library->performLibraryTask();

    const int midiNote = result["note"].as<int>();
    Settings s;
    s.full = true;
    auto snap = library->computeChord(midiNote, s);
    if (snap.count > 0) {
      std::string notesList;
      for (int i = 0; i < snap.count; ++i) {
        if (i > 0) notesList += " ";
        notesList += noteNameStd(snap.notes[static_cast<size_t>(i)]);
      }
      ctx.logger->infoStream() << "Input " << noteNameStd(midiNote) << " -> " << chordName(snap)
                               << " [" << notesList << "]";
    }

    ctx.logger->infoStream() << appName << " ... shutting down";
    return EXIT_SUCCESS;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
