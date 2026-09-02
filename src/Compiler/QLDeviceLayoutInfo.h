#ifndef QLDEVICELAYOUTINFO_H
#define QLDEVICELAYOUTINFO_H

#include <filesystem>
#include <set>
#include <string>

#include "QLDeviceManager.h"

namespace FOEDAG {

// Resolved fabric geometry of a device.
//
// Columns are 1-based ARRAY column indices - the numbering config.json and
// add_layout.py use. The architecture writes them at startx = col + 1, and
// fp::DeviceGridDescriptor uses raw startx, so a value from here needs
// converting before it is handed to either.
class QLDeviceLayout {
 public:
  int width = 0;   // grid width, == arrayX + 4 (+2 IO ring, +2 EMPTY ring)
  int height = 0;  // grid height, == arrayY + 4
  int arrayX = 0;
  int arrayY = 0;
  std::set<int> bramCols;  // may be empty
  std::set<int> dspCols;   // may be empty
  std::string bramSize;    // config.json BRAM_SIZE, e.g. "1x6"; empty when absent
  std::string dspSize;     // config.json DSP_SIZE, e.g. "1x3"; empty when absent
  std::string layoutName;
  std::string layoutMode;  // "FIXED" | "CUSTOM" | "AUTO" | "RESOURCES"
  std::string source;      // "config.json" | "auto_device.log"
  bool resolved = false;
};

// Single owner of "what shape is the fabric we are targeting".
//
// Where the answer comes from depends on how the device package asked for its
// layout to be produced, and the two groups resolve at different points in the
// flow:
//
//   DEVICE_TYPE: FIXED, LAYOUT_MODE: CUSTOM -> config.json, known at device selection
//   LAYOUT_MODE: AUTO, LAYOUT_MODE: RESOURCES -> auto_device.log, known only once
//                                                Packing() has run add_layout.py
//
// Constructing before the answer exists is expected, not an error: resolved()
// stays false and nothing is written. Nothing here can fail a compile.
class QLDeviceLayoutInfo {
 public:
  explicit QLDeviceLayoutInfo(QLDeviceTarget device_target = QLDeviceTarget());
  // Wrap an already-resolved layout. Lets the pure parsers below be exercised
  // through the same accessors the flow uses.
  explicit QLDeviceLayoutInfo(const QLDeviceLayout& layout) : m_layout(layout) {}

  bool resolved() const { return m_layout.resolved; }
  int width() const { return m_layout.width; }
  int height() const { return m_layout.height; }
  const std::set<int>& bramCols() const { return m_layout.bramCols; }
  const std::set<int>& dspCols() const { return m_layout.dspCols; }
  const QLDeviceLayout& layout() const { return m_layout; }

  // <projectPath>/device_layout.json. Empty when there is no project.
  static std::filesystem::path deviceLayoutJSONPath();
  bool writeDeviceLayoutJSON() const;
  // Drop a file left by an earlier run, so an unresolved layout reads as "not
  // known yet" rather than as last run's geometry.
  static void removeStaleDeviceLayoutJSON();

  // Resolve for the current device and write device_layout.json when the answer
  // exists; otherwise remove any stale file. The whole feature's entry point.
  static void refresh(QLDeviceTarget device_target = QLDeviceTarget());

  // Pure text -> geometry parsers. Static and public because every edge case that
  // matters (empty column list, comma vs space, string vs number) is one no
  // installed device produces, so the flow cannot reach them.
  //
  // 'layout_mode' selects where the geometry is read from: "CUSTOM" takes
  // DEVICE_TYPE_SETTINGS.CUSTOM, anything else takes the top-level keys.
  static bool parseDeviceConfig(const json& config_json, const std::string& layout_mode,
                                QLDeviceLayout& out_layout);
  static bool parseAutoDeviceLog(const std::string& log_text, QLDeviceLayout& out_layout);
  // Accepts both separators, mirroring split_cols() in add_layout.py. An empty
  // list is a fabric without that column, not an error.
  static std::set<int> parseColumnList(const std::string& text);
  // True when Packing() will re-shape this fabric, so the config describes a
  // layout the run is about to discard.
  static bool layoutIsResolvedDuringPacking(const QLDeviceLayoutSettings& layout_settings,
                                            const QLDeviceTarget& device_target);

 private:
  bool resolveFromDeviceConfig(const QLDeviceLayoutSettings& layout_settings,
                               QLDeviceTarget device_target);
  bool resolveFromAutoDeviceLog();
  // bramSize/dspSize are a static property of the package, not the layout, so
  // AUTO/RESOURCES devices - whose geometry comes from auto_device.log, not
  // config.json - still need this separate fetch to carry them.
  void resolveBlockSizesFromDeviceConfig(const QLDeviceTarget& device_target);
  void crossCheckAgainstDeviceVariantLayout(const QLDeviceTarget& device_target) const;

  QLDeviceLayout m_layout;
};

}  // namespace FOEDAG

#endif  // QLDEVICELAYOUTINFO_H
