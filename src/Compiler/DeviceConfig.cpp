#include "Compiler/DeviceConfig.h"

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"
#include "nlohmann_json/json.hpp"

namespace FOEDAG {

using json = nlohmann::ordered_json;

const std::vector<std::string>& DeviceConfig::floorplanningRequiredKeys() {
  static const std::vector<std::string> keys = {
      "DEVICE_SIZE", "DSP_COLS", "BRAM_COLS", "DSP_SIZE", "BRAM_SIZE"};
  return keys;
}

DeviceConfig::DeviceConfig(const std::filesystem::path& configFile) {
  if (!FileUtils::FileExists(configFile)) {
    m_error = "config.json not found: " + configFile.string();
    return;
  }

  json config;
  try {
    config = json::parse(FileUtils::GetFileContent(configFile));
  } catch (const json::parse_error& e) {
    m_error = "config.json parse error: " + std::string(e.what());
    return;
  }

  if (!config.is_object()) {
    m_error = "config.json root is not a JSON object";
    return;
  }

  for (const auto& item : config.items()) {
    m_keys.insert(item.key());
  }
  m_loaded = true;
}

bool DeviceConfig::validate(const std::vector<std::string>& requiredKeys) {
  m_missingKeys.clear();

  if (!m_loaded) {
    // m_error already describes the load failure.
    return false;
  }

  for (const std::string& key : requiredKeys) {
    if (m_keys.find(key) == m_keys.end()) {
      m_missingKeys.push_back(key);
    }
  }

  if (!m_missingKeys.empty()) {
    m_error = "config.json is missing required key(s): " +
              StringUtils::join(m_missingKeys, ", ");
    return false;
  }

  m_error.clear();
  return true;
}

}  // namespace FOEDAG
