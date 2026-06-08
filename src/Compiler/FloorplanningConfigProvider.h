#pragma once

#include <filesystem>

namespace FOEDAG {

// Resolves the device config used by floorplanning. Stateless: every call
// re-validates the current device's config.json and falls back to vpr when
// needed. Diagnostics are written to the compiler Messages log.
class FloorplanningConfigProvider {
 public:
  // Return the config to use for floorplanning:
  //   - the device config.json path when it has all required keys;
  //   - otherwise a freshly generated fallback
  //     (failback_floorplanning_config.json) produced from
  //     `vpr --show_arch_resources`;
  //   - an empty path if neither is usable.
  //
  // Cheap when config.json is valid (just a key check); vpr only runs when it is
  // missing/malfunctioning, so callers can call this right before they need it.
  static std::filesystem::path getEffectiveConfig();
};

}  // namespace FOEDAG
