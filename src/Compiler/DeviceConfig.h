#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace FOEDAG {

// Loads a device config.json and lets callers validate it against a required
// set of keys. Construction performs the load (file must exist and contain a
// valid JSON object); validate() then checks that a caller-supplied list of
// keys is present, so different consumers can assert their own needs.
//
// When validation fails (missing file, bad JSON, or any missing required key)
// callers are expected to start the fallback procedure.
class DeviceConfig {
 public:
  explicit DeviceConfig(const std::filesystem::path& configFile);

  // True when the file existed and parsed into a JSON object.
  bool isLoaded() const { return m_loaded; }

  // Check that every key in requiredKeys is present in the config. Returns true
  // on success. On failure returns false and sets error()/missingKeys(); if the
  // config never loaded it returns false with the load error preserved.
  bool validate(const std::vector<std::string>& requiredKeys);

  const std::string& error() const { return m_error; }
  const std::vector<std::string>& missingKeys() const { return m_missingKeys; }

  // Convenience: the keys DeviceGridDescriptor / generate_floorplanning need.
  static const std::vector<std::string>& floorplanningRequiredKeys();

 private:
  bool m_loaded = false;
  std::string m_error;
  std::vector<std::string> m_missingKeys;
  std::set<std::string> m_keys;  // top-level keys present in config.json
};

}  // namespace FOEDAG
