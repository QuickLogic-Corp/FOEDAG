#include "QLDeviceLayoutInfo.h"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

#include "Compiler/Compiler.h"
#include "CompilerOpenFPGA_ql.h"
#include "MainWindow/Session.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "nlohmann_json/json.hpp"

extern FOEDAG::Session* GlobalSession;

using json = nlohmann::ordered_json;

namespace FOEDAG {

namespace {

// The IO ring and the EMPTY ring the layout generator wraps every fabric in:
// grid width == arrayX + 2 (io_left/io_right) + 2 (EMPTY cols), same for height.
// add_layout.py encodes the same constant as a literal '+ 4'.
constexpr int GRID_RING_TILES = 4;

Compiler* compiler() {
  return (GlobalSession != nullptr) ? GlobalSession->GetCompiler() : nullptr;
}

void message(const std::string& text) {
  if (Compiler* c = compiler()) {
    c->Message(text);
  }
}

std::filesystem::path projectPath() {
  Compiler* c = compiler();
  if ((c == nullptr) || (c->ProjManager() == nullptr)) {
    return std::filesystem::path();
  }
  const std::string project_path = c->ProjManager()->projectPath();
  return project_path.empty() ? std::filesystem::path()
                              : std::filesystem::path(project_path);
}

// A config.json value that may legitimately be written as a JSON string or as a
// JSON number. The v2.8 samples quote everything except MARGIN and the RESOURCES
// counts, but that is a convention of the generator, not a guarantee.
bool asString(const json& value, std::string& out) {
  if (value.is_string()) {
    out = value.get<std::string>();
    return true;
  }
  if (value.is_number_integer()) {
    out = std::to_string(value.get<long long>());
    return true;
  }
  return false;
}

bool lookupString(const json& object, const std::string& key, std::string& out) {
  return object.is_object() && object.contains(key) && asString(object[key], out);
}

// "8x6" -> 8, 6. The array dimensions, NOT the grid: see GRID_RING_TILES.
bool parseDeviceSize(const std::string& text, int& out_x, int& out_y) {
  static const std::regex size_regex(R"(^\s*(\d+)\s*[xX]\s*(\d+)\s*$)");
  std::smatch match;
  if (!std::regex_match(text, match, size_regex)) {
    return false;
  }
  try {
    out_x = std::stoi(match[1].str());
    out_y = std::stoi(match[2].str());
  } catch (const std::exception&) {
    return false;
  }
  return (out_x > 0) && (out_y > 0);
}

std::string joinColumns(const std::set<int>& columns) {
  std::string joined;
  for (auto it = columns.begin(); it != columns.end(); ++it) {
    if (it != columns.begin()) {
      joined += ",";
    }
    joined += std::to_string(*it);
  }
  return joined;
}

}  // namespace

std::set<int> QLDeviceLayoutInfo::parseColumnList(const std::string& text) {
  std::set<int> columns;
  std::string token;
  std::istringstream stream(text);
  while (std::getline(stream, token, ',')) {
    std::istringstream inner(token);
    std::string word;
    while (inner >> word) {
      try {
        columns.insert(std::stoi(word));
      } catch (const std::exception&) {
        // a non-numeric entry is dropped rather than failing the whole resolve:
        // this file is informational and must never fail a compile.
      }
    }
  }
  return columns;
}

// Will Packing() re-shape this fabric? If so the config states the fabric the
// package ships, not the one the run will target, and reporting it now would
// publish a geometry that is about to be replaced.
//
// Mirrors the three arms of the mode resolver in CompilerOpenFPGA_ql::Packing().
bool QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
    const QLDeviceLayoutSettings& layout_settings, const QLDeviceTarget& device_target) {

  // A project-level custom_layout.yml overrides whatever the package asked for, and
  // its geometry only reaches us once the script has run.
  if (layout_settings.device_type == "CUSTOM") {
    const std::filesystem::path project_path = projectPath();
    if (!project_path.empty()) {
      std::error_code ec;
      if (std::filesystem::exists(project_path / ".." / "custom_layout.yml", ec)) {
        return true;
      }
    }
  }

  if (layout_settings.layout_mode_present) {
    // AUTO sizes from the packing run's log; RESOURCES sizes from counts. Only
    // CUSTOM states its geometry outright.
    return (layout_settings.layout_mode == "AUTO") ||
           (layout_settings.layout_mode == "RESOURCES");
  }

  // A package predating the contract is re-shaped on the strength of its layout
  // name alone, so DEVICE_SIZE there describes a fabric the run will discard.
  if (!layout_settings.device_type_present) {
    const std::string& layout_name = device_target.device_variant_layout.name;
    return (layout_name == "FPGA_AUTO") || (layout_name == "FPGA_CUSTOM");
  }

  return false;
}

QLDeviceLayoutInfo::QLDeviceLayoutInfo(QLDeviceTarget device_target) {
  QLDeviceManager* device_manager = QLDeviceManager::getInstance();
  if (device_manager == nullptr) {
    return;
  }
  if (!device_manager->isDeviceTargetValid(device_target)) {
    device_target = device_manager->getCurrentDeviceTarget();
    if (!device_manager->isDeviceTargetValid(device_target)) {
      return;
    }
  }

  const QLDeviceLayoutSettings layout_settings =
      device_manager->deviceLayoutSettings(device_target);

  // A corrupt or invalid config is the caller's problem to report - Packing()
  // already fails the run over it. Here it just means "no answer".
  if (layout_settings.config_parse_failed || layout_settings.invalid) {
    return;
  }

  const bool deferred = layoutIsResolvedDuringPacking(layout_settings, device_target);

  const bool ok = deferred ? resolveFromAutoDeviceLog()
                           : resolveFromDeviceConfig(layout_settings, device_target);
  if (!ok) {
    return;
  }

  if (m_layout.layoutMode.empty()) {
    m_layout.layoutMode = layout_settings.layout_mode_present
                              ? layout_settings.layout_mode
                              : layout_settings.device_type;
  }
  if (m_layout.layoutName.empty()) {
    // After a re-shape the run targets a generated layout, and the device target
    // still names the one the package ships. Read it from the compiler rather
    // than taking it from the caller: setCurrentDeviceTarget() is reached again
    // from every QLSettingsManager::getInstance(), so a caller-supplied name
    // would be silently dropped by the next settings read.
    Compiler* c = compiler();
    const std::string generated =
        (c != nullptr) ? c->AutoLayoutGeneratedLayoutName() : std::string();
    m_layout.layoutName =
        generated.empty() ? device_target.device_variant_layout.name : generated;
  }
  m_layout.resolved = true;

  crossCheckAgainstDeviceVariantLayout(device_target);
}

bool QLDeviceLayoutInfo::parseDeviceConfig(const json& config_json,
                                          const std::string& layout_mode,
                                          QLDeviceLayout& out_layout) {
  if (!config_json.is_object()) {
    return false;
  }

  // LAYOUT_MODE: CUSTOM keeps its geometry in its own section. Its column lists
  // are optional there, so each key falls back to the top level independently -
  // taking the whole section or nothing would silently drop a column list the
  // package only stated once.
  json custom_section = json::object();
  if ((layout_mode == "CUSTOM") && config_json.contains("DEVICE_TYPE_SETTINGS") &&
      config_json["DEVICE_TYPE_SETTINGS"].is_object() &&
      config_json["DEVICE_TYPE_SETTINGS"].contains("CUSTOM")) {
    custom_section = config_json["DEVICE_TYPE_SETTINGS"]["CUSTOM"];
  }

  std::string array_x_text;
  std::string array_y_text;
  if (lookupString(custom_section, "ARRAY_X", array_x_text) &&
      lookupString(custom_section, "ARRAY_Y", array_y_text)) {
    try {
      out_layout.arrayX = std::stoi(array_x_text);
      out_layout.arrayY = std::stoi(array_y_text);
    } catch (const std::exception&) {
      return false;
    }
  } else {
    std::string device_size;
    if (!lookupString(config_json, "DEVICE_SIZE", device_size) ||
        !parseDeviceSize(device_size, out_layout.arrayX, out_layout.arrayY)) {
      return false;
    }
  }
  if ((out_layout.arrayX <= 0) || (out_layout.arrayY <= 0)) {
    return false;
  }

  out_layout.width = out_layout.arrayX + GRID_RING_TILES;
  out_layout.height = out_layout.arrayY + GRID_RING_TILES;

  std::string bram_cols;
  if (!lookupString(custom_section, "BRAM_COLS", bram_cols)) {
    lookupString(config_json, "BRAM_COLS", bram_cols);
  }
  std::string dsp_cols;
  if (!lookupString(custom_section, "DSP_COLS", dsp_cols)) {
    lookupString(config_json, "DSP_COLS", dsp_cols);
  }
  out_layout.bramCols = parseColumnList(bram_cols);
  out_layout.dspCols = parseColumnList(dsp_cols);
  out_layout.source = "config.json";
  out_layout.resolved = true;
  return true;
}

bool QLDeviceLayoutInfo::resolveFromDeviceConfig(
    const QLDeviceLayoutSettings& layout_settings, QLDeviceTarget device_target) {
  QLDeviceManager* device_manager = QLDeviceManager::getInstance();
  json config_json;
  if ((device_manager == nullptr) ||
      !device_manager->loadDeviceConfigJSON(device_target, config_json)) {
    return false;
  }
  const std::string layout_mode =
      layout_settings.layout_mode_present ? layout_settings.layout_mode : std::string();
  return parseDeviceConfig(config_json, layout_mode, m_layout);
}

bool QLDeviceLayoutInfo::parseAutoDeviceLog(const std::string& log_text,
                                            QLDeviceLayout& out_layout) {
  // The companion of the 'Layout for ... has been created' line Packing() already
  // matches. That one carries the size; this one is the only place the column
  // lists are ever written down in plaintext.
  static const std::regex calculated_layout_regex(
      R"(Calculated layout: WIDTH=(\d+), HEIGHT=(\d+), ARRAY_X=(\d+), ARRAY_Y=(\d+), )"
      R"(BRAM_COLS=([\d ]*), DSP_COLS=([\d ]*))");

  bool found = false;
  std::istringstream log_stream(log_text);
  std::string line;
  while (std::getline(log_stream, line)) {
    std::smatch match;
    if (!std::regex_search(line, match, calculated_layout_regex)) {
      continue;
    }
    // Last match wins: re-running a project appends another resolve to the log.
    try {
      out_layout.width = std::stoi(match[1].str());
      out_layout.height = std::stoi(match[2].str());
      out_layout.arrayX = std::stoi(match[3].str());
      out_layout.arrayY = std::stoi(match[4].str());
    } catch (const std::exception&) {
      continue;
    }
    out_layout.bramCols = parseColumnList(match[5].str());
    out_layout.dspCols = parseColumnList(match[6].str());
    found = true;
  }
  if (!found) {
    return false;
  }
  out_layout.source = "auto_device.log";
  out_layout.resolved = true;
  return true;
}

bool QLDeviceLayoutInfo::resolveFromAutoDeviceLog() {
  const std::filesystem::path project_path = projectPath();
  if (project_path.empty()) {
    return false;
  }
  std::ifstream log_stream((project_path / "auto_device.log").string());
  if (!log_stream.is_open()) {
    return false;
  }
  std::ostringstream contents;
  contents << log_stream.rdbuf();
  return parseAutoDeviceLog(contents.str(), m_layout);
}

// The shipped <fixed_layout> is an independent witness for a FIXED part, so a
// disagreement means DEVICE_SIZE does not mean what this code assumes it means.
// Reported and then ignored: the config is what add_layout.py acts on.
void QLDeviceLayoutInfo::crossCheckAgainstDeviceVariantLayout(
    const QLDeviceTarget& device_target) const {
  const QLDeviceVariantLayout& variant_layout = device_target.device_variant_layout;
  if ((variant_layout.width <= 0) || (variant_layout.height <= 0)) {
    return;
  }
  if ((variant_layout.width == m_layout.width) &&
      (variant_layout.height == m_layout.height)) {
    return;
  }
  message("[WARNING] Device layout from " + m_layout.source + " is " +
          std::to_string(m_layout.width) + "x" + std::to_string(m_layout.height) +
          ", but the architecture declares " + std::to_string(variant_layout.width) +
          "x" + std::to_string(variant_layout.height) + " for layout '" +
          variant_layout.name + "'.\n");
}

std::filesystem::path QLDeviceLayoutInfo::deviceLayoutJSONPath() {
  const std::filesystem::path project_path = projectPath();
  if (project_path.empty()) {
    return std::filesystem::path();
  }
  return project_path / "device_layout.json";
}

bool QLDeviceLayoutInfo::writeDeviceLayoutJSON() const {
  if (!m_layout.resolved) {
    return false;
  }
  const std::filesystem::path filepath = deviceLayoutJSONPath();
  if (filepath.empty()) {
    return false;
  }

  json layout_json;
  layout_json["width"] = m_layout.width;
  layout_json["height"] = m_layout.height;
  layout_json["array_x"] = m_layout.arrayX;
  layout_json["array_y"] = m_layout.arrayY;
  // Strings, in the config.json shape, so the file can be diffed against the
  // device package without converting a numbering.
  layout_json["bram_cols"] = joinColumns(m_layout.bramCols);
  layout_json["dsp_cols"] = joinColumns(m_layout.dspCols);
  layout_json["layout_name"] = m_layout.layoutName;
  layout_json["layout_mode"] = m_layout.layoutMode;
  layout_json["source"] = m_layout.source;

  const std::string contents = layout_json.dump(2) + "\n";

  // setCurrentDeviceTarget() is reached again from every
  // QLSettingsManager::getInstance(), so an unconditional write would rewrite this
  // file many times per run and churn its mtime for anything watching it.
  std::ifstream existing(filepath.string());
  if (existing.is_open()) {
    std::ostringstream previous;
    previous << existing.rdbuf();
    if (previous.str() == contents) {
      return true;
    }
  }
  existing.close();

  std::ofstream out(filepath.string());
  if (!out.is_open()) {
    message("[WARNING] Cannot write " + filepath.string() + "\n");
    return false;
  }
  out << contents;
  return out.good();
}

void QLDeviceLayoutInfo::removeStaleDeviceLayoutJSON() {
  const std::filesystem::path filepath = deviceLayoutJSONPath();
  if (filepath.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(filepath, ec);
}

void QLDeviceLayoutInfo::refresh(QLDeviceTarget device_target) {
  if (projectPath().empty()) {
    // No project: device selection in the new-project wizard has nowhere to write
    // and nothing to invalidate.
    return;
  }
  QLDeviceLayoutInfo layout_info(device_target);
  if (!layout_info.resolved()) {
    // AUTO/RESOURCES before packing, or a package that states no geometry. Either
    // way the previous run's file must not be left behind to be read as current.
    removeStaleDeviceLayoutJSON();
    return;
  }
  layout_info.writeDeviceLayoutJSON();
}

}  // namespace FOEDAG
