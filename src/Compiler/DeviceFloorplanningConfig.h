#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace FOEDAG {

// Provides the device config.json used by floorplanning, with a vpr-based
// fallback when it is missing or malfunctioning.
//
// Validation is expensive only in the fallback case (it runs
// `vpr --show_arch_resources`), so it is computed once per device context:
// call refresh() on project open and on device change. Consumers then read the
// cached effectiveConfigPath() on demand without re-running vpr.
class DeviceFloorplanningConfig {
 public:
  static DeviceFloorplanningConfig& instance();

  // Re-validate the current device's config.json and, if needed, regenerate the
  // fallback (failback_floorplanning_config.json) from vpr. Call on project
  // open and on device change.
  void refresh();

  bool isValid() const { return m_valid; }
  const std::string& error() const { return m_error; }
  const std::vector<std::string>& missingKeys() const { return m_missingKeys; }

  bool fallbackUsed() const { return m_fallbackUsed; }
  const std::filesystem::path& fallbackConfigPath() const {
    return m_fallbackConfigPath;
  }

  // The config file consumers should use: the original when valid, otherwise
  // the generated fallback. Empty if neither is usable.
  const std::filesystem::path& effectiveConfigPath() const {
    return m_effectiveConfigPath;
  }

  // The keys DeviceGridDescriptor / generate_floorplanning need.
  static const std::vector<std::string>& floorplanningRequiredKeys();

  DeviceFloorplanningConfig(const DeviceFloorplanningConfig&) = delete;
  DeviceFloorplanningConfig& operator=(const DeviceFloorplanningConfig&) = delete;

 private:
  DeviceFloorplanningConfig() = default;

  void reset();

  // Load + collect top-level keys from configFile. Returns false (and sets
  // error()) if the file is missing or not a JSON object.
  bool load(const std::filesystem::path& configFile);

  // Check that every key in requiredKeys is present in the loaded config.
  bool validate(const std::vector<std::string>& requiredKeys);

  // Run `vpr --show_arch_resources`, parse it and write the fallback config to
  // the project folder. On success sets m_fallbackConfigPath. Inputs (arch,
  // blif, project dir) are pulled from QLDeviceManager/GlobalSession.
  bool generateFallbackConfig();

  bool m_valid = false;
  bool m_fallbackUsed = false;
  std::string m_error;
  std::vector<std::string> m_missingKeys;
  std::set<std::string> m_keys;  // top-level keys of the currently loaded config
  std::filesystem::path m_fallbackConfigPath;
  std::filesystem::path m_effectiveConfigPath;
};

}  // namespace FOEDAG
