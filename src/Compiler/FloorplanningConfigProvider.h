#pragma once

#include <filesystem>

namespace FOEDAG {

// Resolves the device config used by floorplanning. Stateless.
class FloorplanningConfigProvider {
 public:
  // Path to the current device's config.json, or an empty path if it is not on
  // disk (reported to the compiler Messages log).
  //
  // It is the only source of floorplanning geometry; there is no fallback. Its
  // *contents* are validated by each consumer -- DeviceGridDescriptor for the
  // widget, generate_floorplanning.py for the constraints -- each of which
  // reports precisely what it could not read, so this does not re-check them.
  static std::filesystem::path getConfig();
};

}  // namespace FOEDAG
