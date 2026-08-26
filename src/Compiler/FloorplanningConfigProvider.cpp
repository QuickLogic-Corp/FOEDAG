#include "Compiler/FloorplanningConfigProvider.h"

#include <QProcess>
#include <QString>
#include <QStringList>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "Compiler/QLDeviceManager.h"
#include "MainWindow/Session.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "Utils/FileUtils.h"
#include "Utils/QtUtils.h"
#include "Utils/StringUtils.h"
#include "nlohmann_json/json.hpp"

extern FOEDAG::Session* GlobalSession;

namespace FOEDAG {

using json = nlohmann::ordered_json;

namespace {

// The VPR device grid wraps the device core with an IO ring, and its thickness
// is not fixed: 2 cells per side on every device but EVAL-2024Q1-MULTI, where
// it is 1. It is therefore read from the vpr report's FLOORPLAN_IO_BOUNDS
// line, never assumed. config.json is in core coordinates, derived from the
// perimeter rather than from a ring constant:
//   DEVICE_SIZE  = (ioRight - ioLeft - 1) x (ioTop - ioBottom - 1)
//   <col in cfg> = grid column - ioLeft        (1-based core column)
// See aurora2 issue #2283.

// The keys DeviceGridDescriptor / generate_floorplanning need.
const std::vector<std::string>& requiredKeys() {
  static const std::vector<std::string> keys = {
      "DEVICE_SIZE", "DSP_COLS",  "BRAM_COLS", "DSP_SIZE",
      "BRAM_SIZE",   "IO_BOTTOM", "IO_LEFT",   "IO_TOP",
      "IO_RIGHT"};
  return keys;
}

// Parse "<w>x<h>" into a pair. Returns false on malformed input.
bool parseWxH(const QString& value, int& w, int& h) {
  const QStringList parts = value.trimmed().split("x");
  if (parts.size() != 2) return false;
  bool okW = false, okH = false;
  w = parts.at(0).trimmed().toInt(&okW);
  h = parts.at(1).trimmed().toInt(&okH);
  return okW && okH;
}

// Parse "<bottom> <left> <top> <right>" into the four IO perimeter bounds.
bool parseIoBounds(const QString& value, int& bottom, int& left, int& top,
                   int& right) {
  const QStringList parts = QtUtils::StringSplit(value.trimmed(), ' ');
  if (parts.size() != 4) return false;
  bool okB = false, okL = false, okT = false, okR = false;
  bottom = parts.at(0).toInt(&okB);
  left = parts.at(1).toInt(&okL);
  top = parts.at(2).toInt(&okT);
  right = parts.at(3).toInt(&okR);
  return okB && okL && okT && okR;
}

// Parse "a,b,c" into integers. Empty string yields an empty list.
bool parseColumnList(const QString& value, std::vector<int>& out) {
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
// The first core column sits one cell inside the left IO column, and config
// numbers it 1, so the shift is exactly ioLeft whatever the ring thickness.
std::string toConfigColumns(const std::vector<int>& gridColumns, int ioLeft) {
  std::vector<std::string> shifted;
  for (int col : gridColumns) {
    shifted.push_back(std::to_string(col - ioLeft));
  }
  return StringUtils::join(shifted, ",");
}

// Load configFile and return the set of missing required keys. On a load/parse
// failure returns false and sets error (the keys set is then meaningless).
bool checkConfig(const std::filesystem::path& configFile,
                 std::vector<std::string>& missing, std::string& error) {
  missing.clear();

  if (!FileUtils::FileExists(configFile)) {
    error = "config.json not found: " + configFile.string();
    return false;
  }

  json config;
  try {
    config = json::parse(FileUtils::GetFileContent(configFile));
  } catch (const json::parse_error& e) {
    error = "config.json parse error: " + std::string(e.what());
    return false;
  }
  if (!config.is_object()) {
    error = "config.json root is not a JSON object";
    return false;
  }

  for (const std::string& key : requiredKeys()) {
    if (!config.contains(key)) {
      missing.push_back(key);
    }
  }
  if (!missing.empty()) {
    error = "config.json is missing required key(s): " +
            StringUtils::join(missing, ", ");
  }
  return true;
}

// Run `vpr --show_arch_resources`, parse it and write the fallback config to
// the project folder. Returns the written path, or empty (with error set).
std::filesystem::path generateFallbackConfig(std::string& error) {
  if (GlobalSession == nullptr || GlobalSession->GetCompiler() == nullptr) {
    error = "no active compiler session";
    return {};
  }
  CompilerOpenFPGA_ql* compiler =
      dynamic_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());
  if (compiler == nullptr || compiler->ProjManager() == nullptr) {
    error = "no project/compiler available";
    return {};
  }

  // The device VPR arch is shipped encrypted (vpr.xml.en); this provider
  // decrypts it to a temp file (cleaned up when archProvider goes out of scope,
  // i.e. after the vpr run below). Use it instead of the raw arch path.
  VprArchitectureFileProfider archProvider(compiler);
  const std::filesystem::path archFile = archProvider.get();
  const std::filesystem::path blifFile = compiler->getPostSynthBlifFilePath();
  const std::filesystem::path projectDir = compiler->ProjManager()->projectPath();

  if (archFile.empty() || !FileUtils::FileExists(archFile)) {
    error = "VPR architecture file not available";
    return {};
  }
  if (!FileUtils::FileExists(blifFile)) {
    error = "post-synthesis blif not found: " + blifFile.string();
    return {};
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
    error = "failed to start vpr";
    return {};
  }
  if (!vpr.waitForFinished(-1)) {
    error = "vpr --show_arch_resources did not finish";
    return {};
  }
  if (vpr.exitStatus() != QProcess::NormalExit || vpr.exitCode() != 0) {
    const QString stderrTail =
        QString::fromUtf8(vpr.readAllStandardError()).trimmed().right(500);
    error = "vpr --show_arch_resources failed (exit code " +
            std::to_string(vpr.exitCode()) + "): " + stderrTail.toStdString();
    return {};
  }

  // Parse the FLOORPLAN_* block (raw grid coordinates).
  int gridW = 0, gridH = 0;
  int dspW = 1, dspH = 1, bramW = 1, bramH = 1;
  int ioBottom = 0, ioLeft = 0, ioTop = 0, ioRight = 0;
  std::vector<int> dspGridCols, bramGridCols;
  bool haveGrid = false, haveDspSize = false, haveBramSize = false;
  bool haveIoBounds = false;

  const QString output = QString::fromUtf8(vpr.readAllStandardOutput());
  for (const QString& rawLine : output.split('\n')) {
    const QString line = rawLine.trimmed();
    if (line.startsWith("FLOORPLAN_DEVICE_GRID ")) {
      haveGrid = parseWxH(line.section(' ', 1), gridW, gridH);
    } else if (line.startsWith("FLOORPLAN_DSP_COLUMNS ")) {
      parseColumnList(line.section(' ', 1), dspGridCols);
    } else if (line.startsWith("FLOORPLAN_BRAM_COLUMNS ")) {
      parseColumnList(line.section(' ', 1), bramGridCols);
    } else if (line.startsWith("FLOORPLAN_DSP_TILE_SIZE ")) {
      haveDspSize = parseWxH(line.section(' ', 1), dspW, dspH);
    } else if (line.startsWith("FLOORPLAN_BRAM_TILE_SIZE ")) {
      haveBramSize = parseWxH(line.section(' ', 1), bramW, bramH);
    } else if (line.startsWith("FLOORPLAN_IO_BOUNDS ")) {
      haveIoBounds =
          parseIoBounds(line.section(' ', 1), ioBottom, ioLeft, ioTop, ioRight);
    }
  }

  if (!haveGrid || !haveDspSize || !haveBramSize) {
    error = "could not parse vpr resource report";
    return {};
  }
  if (!haveIoBounds) {
    // Guessing a 2-cell ring here would put every IO constraint one cell
    // inside the real perimeter on a ring-1 device, silently. Fail instead.
    error =
        "vpr resource report has no FLOORPLAN_IO_BOUNDS line (stale "
        "hotfix-vpr-floorplan-resources?); the IO perimeter cannot be guessed";
    return {};
  }

  // Convert raw grid coordinates into config (core) coordinates. The core
  // spans the cells strictly inside the perimeter, whatever the ring
  // thickness -- no ring constant is involved.
  const int deviceW = ioRight - ioLeft - 1;
  const int deviceH = ioTop - ioBottom - 1;
  if (deviceW <= 0 || deviceH <= 0 || ioBottom < 0 || ioLeft < 0 ||
      ioRight >= gridW || ioTop >= gridH) {
    error = "implausible device grid size from vpr";
    return {};
  }

  json cfg;
  cfg["DEVICE_SIZE"] = std::to_string(deviceW) + "x" + std::to_string(deviceH);
  cfg["DSP_SIZE"] = std::to_string(dspW) + "x" + std::to_string(dspH);
  cfg["BRAM_SIZE"] = std::to_string(bramW) + "x" + std::to_string(bramH);
  cfg["DSP_COLS"] = toConfigColumns(dspGridCols, ioLeft);
  cfg["BRAM_COLS"] = toConfigColumns(bramGridCols, ioLeft);
  cfg["IO_BOTTOM"] = std::to_string(ioBottom);
  cfg["IO_LEFT"] = std::to_string(ioLeft);
  cfg["IO_TOP"] = std::to_string(ioTop);
  cfg["IO_RIGHT"] = std::to_string(ioRight);

  const std::filesystem::path outPath =
      projectDir / "failback_floorplanning_config.json";
  std::ofstream out(outPath);
  if (!out) {
    error = "cannot write " + outPath.string();
    return {};
  }
  out << cfg.dump(2) << "\n";
  out.close();

  return outPath;
}

}  // namespace

std::filesystem::path FloorplanningConfigProvider::getEffectiveConfig() {
  Compiler* compiler =
      (GlobalSession != nullptr) ? GlobalSession->GetCompiler() : nullptr;

  const std::filesystem::path configFile =
      QLDeviceManager::getInstance()->deviceConfigJSONPath();

  // 1. Use the current device's config.json as-is when it is valid.
  std::vector<std::string> missing;
  std::string error;
  if (checkConfig(configFile, missing, error) && missing.empty()) {
    return configFile;
  }

  // 2. Missing/malfunctioning: announce why and generate a fallback from vpr.
  if (compiler) {
    compiler->Message(
        "Floorplanning: device config.json cannot be used (" + error +
        "). Falling back to 'vpr --show_arch_resources' to generate "
        "failback_floorplanning_config.json.");
  }

  std::string fallbackError;
  const std::filesystem::path fallbackConfig =
      generateFallbackConfig(fallbackError);
  if (!fallbackConfig.empty() &&
      checkConfig(fallbackConfig, missing, fallbackError) && missing.empty()) {
    if (compiler) {
      compiler->Message("Floorplanning: using fallback device config " +
                        fallbackConfig.string() + ".");
    }
    return fallbackConfig;
  }

  if (compiler) {
    compiler->ErrorMessage(
        "Floorplanning: fallback config generation failed (" + fallbackError +
        ").");
  }
  return {};
}

}  // namespace FOEDAG
