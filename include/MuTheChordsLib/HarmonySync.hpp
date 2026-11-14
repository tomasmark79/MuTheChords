#pragma once

#include <memory>
#include <string>

namespace muthechords {

  struct HarmonyState {
    int root = 0;
    int scale = 0;

    bool operator==(const HarmonyState &other) const noexcept {
      return root == other.root && scale == other.scale;
    }

    bool operator!=(const HarmonyState &other) const noexcept { return !(*this == other); }
  };

  // Opt-in, per-user bus shared by plugin formats and host processes. All calls,
  // including destruction, belong on the message thread. Never call from audio.
  // Lock acquisition is bounded to 20 ms; filesystem I/O itself has no deadline.
  class HarmonySync {
  public:
    explicit HarmonySync(std::string channelName = {});
    ~HarmonySync();
    HarmonySync(const HarmonySync &) = delete;
    HarmonySync &operator=(const HarmonySync &) = delete;

    bool join(HarmonyState &inOut);
    void leave();
    bool publish(const HarmonyState &state);
    // False means unchanged OR failure; error() distinguishes failures.
    bool read(HarmonyState &inOut);
    [[nodiscard]] bool joined() const;
    [[nodiscard]] std::string error() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
  };

} // namespace muthechords
