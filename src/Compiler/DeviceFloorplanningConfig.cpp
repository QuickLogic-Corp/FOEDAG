#include "Compiler/DeviceFloorplanningConfig.h"

#include <QProcess>
#include <QString>
#include <QStringList>

#include <fstream>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "Compiler/QLDeviceManager.h"
#include "MainWindow/Session.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"
#include "nlohmann_json/json.hpp"

extern FOEDAG::Session* GlobalSession;

namespace FOEDAG {

using json = nlohmann::ordered_json;

namespace {

// The VPR device grid wraps the device core with an IO ring this many cells
// wide on each side. config.json is in core coordinates:
//   DEVICE_SIZE  = grid - 2*ring                (both sides)
//   <col in cfg> = grid column - (ring - 1)     (1-based core column)
constexpr int kGridRingPerSide = 2;

// Parse "<w>x<h>" into a pair. Returns false on malformed input.
bool parseWxH(const QString& value, int& w, int& h) {
  const QStringList parts = value.trimmed().split("x");
  if (parts.size() != 2) return false;
  bool okW = false, okH = false;
  w = parts.at(0).trimmed().toInt(&okW);
  h = parts.at(1).trimmed().toInt(&okH);
  return okW && okH;
}

// Parse "a,b,c" into integers. Empty string yields an empty list.
bool parseCsvInts(const QString& value, std::vector<int>& out) {
  const QStringList parts = value.trimmed().split(",");
  for (const QString& rawPart : parts) {
    const QString part = rawPart.trimmed();
    if (part.isEmpty()) continue;
    bool ok = false;
    const int v = part.toInt(&ok);
    if (!ok) return false;
    out.push_back(v);
  }
  return true;
}

// Shift raw grid columns into config (1-based core) columns and join as CSV.
std::string toConfigColumns(const std::vector<int>& gridColumns) {
  std::vector<std::string> shifted;
  for (int col : gridColumns) {
    shifted.push_back(std::to_string(col - (kGridRingPerSide - 1)));
  }
  return StringUtils::join(shifted, ",");
}

}  // namespace

const std::vector<std::string>& DeviceFloorplanningConfig::floorplanningRequiredKeys() {
  static const std::vector<std::string> keys = {
      "DEVICE_SIZE", "DSP_COLS", "BRAM_COLS", "DSP_SIZE", "BRAM_SIZE"};
  return keys;
}

DeviceFloorplanningConfig& DeviceFloorplanningConfig::instance() {
  static DeviceFloorplanningConfig s_instance;
  return s_instance;
}

void DeviceFloorplanningConfig::reset() {
  m_valid = false;
  m_fallbackUsed = false;
  m_error.clear();
  m_missingKeys.clear();
  m_keys.clear();
  m_fallbackConfigPath.clear();
  m_effectiveConfigPath.clear();
}

void DeviceFloorplanningConfig::refresh() {
  reset();

  const std::filesystem::path configFile =
      QLDeviceManager::getInstance()->deviceConfigJSONPath();

  // 1. Try the current device's config.json as-is.
  if (load(configFile) && validate(floorplanningRequiredKeys())) {
    m_valid = true;
    m_effectiveConfigPath = configFile;
    return;
  }

  // 2. Missing/malfunctioning: generate a fallback from vpr and validate it.
  if (generateFallbackConfig()) {
    m_fallbackUsed = true;
    if (load(m_fallbackConfigPath) && validate(floorplanningRequiredKeys())) {
      m_valid = true;
      m_effectiveConfigPath = m_fallbackConfigPath;
      return;
    }
  }
  // Otherwise m_valid stays false and m_error describes the failure.
}

bool DeviceFloorplanningConfig::load(const std::filesystem::path& configFile) {
  m_keys.clear();

  if (!FileUtils::FileExists(configFile)) {
    m_error = "config.json not found: " + configFile.string();
    return false;
  }

  json config;
  try {
    config = json::parse(FileUtils::GetFileContent(configFile));
  } catch (const json::parse_error& e) {
    m_error = "config.json parse error: " + std::string(e.what());
    return false;
  }

  if (!config.is_object()) {
    m_error = "config.json root is not a JSON object";
    return false;
  }

  for (const auto& item : config.items()) {
    m_keys.insert(item.key());
  }
  return true;
}

bool DeviceFloorplanningConfig::validate(const std::vector<std::string>& requiredKeys) {
  m_missingKeys.clear();

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

bool DeviceFloorplanningConfig::generateFallbackConfig() {
  if (GlobalSession == nullptr || GlobalSession->GetCompiler() == nullptr) {
    m_error = "cannot generate fallback config: no active compiler session";
    return false;
  }
  CompilerOpenFPGA_ql* compiler =
      dynamic_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());
  if (compiler == nullptr || compiler->ProjManager() == nullptr) {
    m_error = "cannot generate fallback config: no project/compiler available";
    return false;
  }

  const std::filesystem::path archFile =
      QLDeviceManager::getInstance()->deviceVPRArchitectureFile();
  const std::filesystem::path blifFile = compiler->getPostSynthBlifFilePath();
  const std::filesystem::path projectDir = compiler->ProjManager()->projectPath();

  if (!FileUtils::FileExists(archFile)) {
    m_error = "fallback: VPR architecture file not found: " + archFile.string();
    return false;
  }
  if (!FileUtils::FileExists(blifFile)) {
    m_error = "fallback: post-synthesis blif not found: " + blifFile.string();
    return false;
  }

  // Run vpr --show_arch_resources serially and capture its stdout.
  QProcess vpr;
  vpr.setWorkingDirectory(QString::fromStdString(projectDir.string()));
  QStringList args;
  args << QString::fromStdString(archFile.string())
       << QString::fromStdString(blifFile.string())
       << "--circuit_format" << "eblif"
       << "--timing_analysis" << "off"
       << "--show_arch_resources";
  vpr.start("vpr", args);
  if (!vpr.waitForStarted(30000)) {
    m_error = "fallback: failed to start vpr";
    return false;
  }
  if (!vpr.waitForFinished(-1)) {
    m_error = "fallback: vpr --show_arch_resources did not finish";
    return false;
  }
  if (vpr.exitStatus() != QProcess::NormalExit || vpr.exitCode() != 0) {
    m_error = "fallback: vpr --show_arch_resources failed (exit code " +
              std::to_string(vpr.exitCode()) + ")";
    return false;
  }

  // Parse the FLOORPLAN_* block (raw grid coordinates).
  int gridW = 0, gridH = 0;
  int dspW = 1, dspH = 1, bramW = 1, bramH = 1;
  std::vector<int> dspGridCols, bramGridCols;
  bool haveGrid = false, haveDspSize = false, haveBramSize = false;

  const QString output = QString::fromUtf8(vpr.readAllStandardOutput());
  for (const QString& rawLine : output.split('\n')) {
    const QString line = rawLine.trimmed();
    if (line.startsWith("FLOORPLAN_DEVICE_GRID ")) {
      haveGrid = parseWxH(line.section(' ', 1), gridW, gridH);
    } else if (line.startsWith("FLOORPLAN_DSP_COLUMNS ")) {
      parseCsvInts(line.section(' ', 1), dspGridCols);
    } else if (line.startsWith("FLOORPLAN_BRAM_COLUMNS ")) {
      parseCsvInts(line.section(' ', 1), bramGridCols);
    } else if (line.startsWith("FLOORPLAN_DSP_TILE_SIZE ")) {
      haveDspSize = parseWxH(line.section(' ', 1), dspW, dspH);
    } else if (line.startsWith("FLOORPLAN_BRAM_TILE_SIZE ")) {
      haveBramSize = parseWxH(line.section(' ', 1), bramW, bramH);
    }
  }

  if (!haveGrid || !haveDspSize || !haveBramSize) {
    m_error = "fallback: could not parse vpr resource report";
    return false;
  }

  // Convert raw grid coordinates into config (core) coordinates.
  const int deviceW = gridW - 2 * kGridRingPerSide;
  const int deviceH = gridH - 2 * kGridRingPerSide;
  if (deviceW <= 0 || deviceH <= 0) {
    m_error = "fallback: implausible device grid size from vpr";
    return false;
  }

  json cfg;
  cfg["DEVICE_SIZE"] = std::to_string(deviceW) + "x" + std::to_string(deviceH);
  cfg["DSP_SIZE"] = std::to_string(dspW) + "x" + std::to_string(dspH);
  cfg["BRAM_SIZE"] = std::to_string(bramW) + "x" + std::to_string(bramH);
  cfg["DSP_COLS"] = toConfigColumns(dspGridCols);
  cfg["BRAM_COLS"] = toConfigColumns(bramGridCols);

  const std::filesystem::path outPath =
      projectDir / "failback_floorplanning_config.json";
  std::ofstream out(outPath);
  if (!out) {
    m_error = "fallback: cannot write " + outPath.string();
    return false;
  }
  out << cfg.dump(2) << "\n";
  out.close();

  m_fallbackConfigPath = outPath;
  return true;
}

}  // namespace FOEDAG
