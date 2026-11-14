#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace muthechords {

  // Single producer / single consumer, three owned slots. The reader gets the
  // latest frame even if it has been closed for many audio callbacks. Neither
  // thread waits, and the writer never modifies the reader-owned slot.
  template <class Frame>
  class DisplayMailbox {
  public:
    void publish(const Frame &frame) {
      frames[static_cast<std::size_t>(writer)] = frame;
      writer = middle.exchange(writer | dirty, std::memory_order_acq_rel) & indexMask;
    }

    bool read(Frame &frame) {
      if ((middle.load(std::memory_order_acquire) & dirty) == 0) {
        return false;
      }
      reader = middle.exchange(reader, std::memory_order_acq_rel) & indexMask;
      frame = frames[static_cast<std::size_t>(reader)];
      return true;
    }

  private:
    static constexpr int dirty = 4;
    static constexpr int indexMask = 3;
    std::array<Frame, 3> frames{};
    int writer = 0;
    int reader = 1;
    std::atomic<int> middle{2};
    static_assert(std::atomic<int>::is_always_lock_free,
                  "Display updates must not lock the audio thread");
    static_assert(std::is_trivially_copyable<Frame>::value,
                  "Audio display frames must not own dynamic storage");
  };

} // namespace muthechords
