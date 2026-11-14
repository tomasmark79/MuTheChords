#pragma once

#include <Utils/Platform/IPlatformInfo.hpp>
#include <memory>

namespace muthechords::utils {

  class PlatformInfoFactory {
  public:
    [[nodiscard]]
    static std::unique_ptr<IPlatformInfo> createForCurrentPlatform();

    [[nodiscard]]
    static std::unique_ptr<IPlatformInfo> create(Platform platform);
  };

} // namespace muthechords::utils