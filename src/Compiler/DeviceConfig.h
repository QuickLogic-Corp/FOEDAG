#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace FOEDAG {

// Loads a device config.json and ensures it can drive floorplanning.
//
// On construction it loads the given config.json and checks the floorplanning
// required keys. If the file is missing/malfunctioning or any required key is
// absent, it runs `vpr --show_arch_resources` (serial/blocking), parses the
// resource report and writes a fallback config to the project folder
// (failback_floorplanning_config.json), then validates that instead.
//
// After construction: isValid() tells whether a usable config exists (original
// or fallback); effectiveConfigPath() is the file the caller should consume;
// fallbackUsed()/fallbackConfigPath() expose whether/where a fallback was made.
class DeviceConfig {
 public:
  explicit DeviceConfig(const std::filesystem::path& configFile);

  bool isValid() const { return m_valid; }
  const std::string& error() const { return m_error; }
  const std::vector<std::string>& missingKeys() const { return m_missingKeys; }

  bool fallbackUsed() const { return m_fallbackUsed; }
  const std::filesystem::path& fallbackConfigPath() const {
    return m_fallbackConfigPath;
  }

  // The config file the caller should use: the original when it is valid,
  // otherwise the generated fallback. Empty if neither is usable.
  const std::filesystem::path& effectiveConfigPath() const {
    return m_effectiveConfigPath;
  }

  // Validate the currently loaded config against an arbitrary key list. Returns
  // true on success; on failure sets error()/missingKeys().
  bool validate(const std::vector<std::string>& requiredKeys);

  // The keys DeviceGridDescriptor / generate_floorplanning need.
  static const std::vector<std::string>& floorplanningRequiredKeys();

 private:
  // Load + collect top-level keys from configFile. Returns false (and sets
  // error()) if the file is missing or not a JSON object.
  bool load(const std::filesystem::path& configFile);

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
