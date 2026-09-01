/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Compiler/QLDeviceLayoutInfo.h"

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

// The shape of a v2.8 device config, trimmed to the keys the layout resolver
// reads. Full samples live in the device packages; everything omitted here is
// deliberately irrelevant to geometry.
json baseConfig() {
  return json{{"DEVICE_SIZE", "8x6"}, {"BRAM_COLS", "3"}, {"DSP_COLS", "6"},
              {"BRAM_SIZE", "1x6"},   {"DSP_SIZE", "1x3"}, {"IO_CAPACITY", "20"}};
}

QLDeviceLayout parseConfig(const json& config, const std::string& layout_mode = "") {
  QLDeviceLayout layout;
  QLDeviceLayoutInfo::parseDeviceConfig(config, layout_mode, layout);
  return layout;
}

QLDeviceLayout parseLog(const std::string& text) {
  QLDeviceLayout layout;
  QLDeviceLayoutInfo::parseAutoDeviceLog(text, layout);
  return layout;
}

// The line add_layout.py prints for the AUTO run recorded in the reference
// jpeg_top flow: a 100x102 array with one BRAM and one DSP column.
// Byte-identical to the four v2.8 samples supplied with the request
// (task workspace: config.json/config_{fixed,custom,auto,resources}.json). Full
// files, not a hand-trimmed subset, so a resolve exercised here sees every key
// QLDeviceManager and add_layout.py also read alongside the geometry ones.
constexpr const char* SAMPLE_CONFIG_FIXED = R"json({
  "CONFIG_FILE_VERSION": "v2.8",
  "DEVICE_DATA_VERSION": "v2.0",
  "CUSTOMER_NAME": "EVAL-MSC-CUSTOM",
  "DEVICE_SIZE": "8x6",
  "TAPEOUT_DATE": "2026Q2",
  "AURORA_SETTINGS_TEMPLATE": "aurora/settings_template.json",
  "AURORA_YOSYS_TEMPLATE_SCRIPT": "aurora/aurora_template_script.ys",
  "AURORA_SYNPLIFY_TEMPLATE_SCRIPT": "aurora/aurora_template_script.prj",
  "AURORA_OPENFPGA_TEMPLATE_SCRIPT": "aurora/aurora_template_script.openfpga",
  "POWER_TEMPLATE": "aurora/power_template.json",
  "PIN_TABLE": "aurora/efpga_pinmap.csv",
  "FABRIC_KEY": "aurora/fabric_key.xml",
  "FIXED_SIM_OPENFPGA": "aurora/fixed_sim_openfpga.xml",
  "REPACK_DESIGN_CONSTRAINT": "aurora/repack_design_constraint.xml",
  "BITSTREAM_ANNOTATION": "aurora/bitstream_annotation.xml",
  "BITSTREAM_REMAPPING": "aurora/bitstream_remapping.xml",
  "FPGA_IO_MAP": "aurora/fpga_io_map.xml",
  "SB_MAPS": "aurora/SB_MAPS.yml",
  "CORNER_VPR_ARCH": "vpr.xml",
  "CORNER_SB_TEMPLATE_DIR": "CSV",
  "CORNER_RRGRAPH_BIN": "rr_graph.bin",
  "CORNER_ROUTER_LOOKAHEAD_BIN": "router_lookahead.bin",
  "CORNER_OPENFPGA_ARCH": "openfpga.xml",
  "CORNER_POWER_DATA": "power_data.json",
  "CORNERS": {
    "SSPG_0P72_M40C": "LVT/SSPG_0P72_M40C",
    "SSPG_0P72_125C": "LVT/SSPG_0P72_125C",
    "TT_0P80_25C": "LVT/TT_0P80_25C",
    "FFPG_0P88_125C": "LVT/FFPG_0P88_125C",
    "FFPG_0P88_M40C": "LVT/FFPG_0P88_M40C"
  },
  "AURORA_VERSION": "nightly",
  "BRAM_TYPE": "TDP_ECC",
  "CONFIG_TYPE": "WLBL",
  "CRR_VERSION": "v2.4",
  "BRAM_VERSION": "v4.0",
  "DSP_VERSION": "v4.0",
  "IO_PAD_VERSION": "v3.0",
  "DSP_COLS": "6",
  "DSP_SIZE": "1x3",
  "BRAM_COLS": "3",
  "BRAM_SIZE": "1x6",
  "IO_CAPACITY": "20",
  "DEVICE_TYPE": "FIXED"
}
)json";
constexpr const char* SAMPLE_CONFIG_CUSTOM = R"json({
  "CONFIG_FILE_VERSION": "v2.8",
  "DEVICE_DATA_VERSION": "v2.0",
  "CUSTOMER_NAME": "EVAL-MSC-CUSTOM",
  "DEVICE_SIZE": "8x6",
  "TAPEOUT_DATE": "2026Q2",
  "AURORA_SETTINGS_TEMPLATE": "aurora/settings_template.json",
  "AURORA_YOSYS_TEMPLATE_SCRIPT": "aurora/aurora_template_script.ys",
  "AURORA_SYNPLIFY_TEMPLATE_SCRIPT": "aurora/aurora_template_script.prj",
  "AURORA_OPENFPGA_TEMPLATE_SCRIPT": "aurora/aurora_template_script.openfpga",
  "POWER_TEMPLATE": "aurora/power_template.json",
  "PIN_TABLE": "aurora/efpga_pinmap.csv",
  "FABRIC_KEY": "aurora/fabric_key.xml",
  "FIXED_SIM_OPENFPGA": "aurora/fixed_sim_openfpga.xml",
  "REPACK_DESIGN_CONSTRAINT": "aurora/repack_design_constraint.xml",
  "BITSTREAM_ANNOTATION": "aurora/bitstream_annotation.xml",
  "BITSTREAM_REMAPPING": "aurora/bitstream_remapping.xml",
  "FPGA_IO_MAP": "aurora/fpga_io_map.xml",
  "SB_MAPS": "aurora/SB_MAPS.yml",
  "CORNER_VPR_ARCH": "vpr.xml",
  "CORNER_SB_TEMPLATE_DIR": "CSV",
  "CORNER_RRGRAPH_BIN": "rr_graph.bin",
  "CORNER_ROUTER_LOOKAHEAD_BIN": "router_lookahead.bin",
  "CORNER_OPENFPGA_ARCH": "openfpga.xml",
  "CORNER_POWER_DATA": "power_data.json",
  "CORNERS": {
    "SSPG_0P72_M40C": "LVT/SSPG_0P72_M40C",
    "SSPG_0P72_125C": "LVT/SSPG_0P72_125C",
    "TT_0P80_25C": "LVT/TT_0P80_25C",
    "FFPG_0P88_125C": "LVT/FFPG_0P88_125C",
    "FFPG_0P88_M40C": "LVT/FFPG_0P88_M40C"
  },
  "AURORA_VERSION": "nightly",
  "BRAM_TYPE": "TDP_ECC",
  "CONFIG_TYPE": "WLBL",
  "CRR_VERSION": "v2.4",
  "BRAM_VERSION": "v4.0",
  "DSP_VERSION": "v4.0",
  "IO_PAD_VERSION": "v3.0",
  "DSP_COLS": "6",
  "DSP_SIZE": "1x3",
  "BRAM_COLS": "3",
  "BRAM_SIZE": "1x6",
  "IO_CAPACITY": "20",
  "DEVICE_TYPE": "CUSTOM",
  "DEVICE_TYPE_SETTINGS": {
    "LAYOUT_MODE": "CUSTOM",
    "MARGIN": 1.2,
    "CUSTOM": {
      "ARRAY_X": "8",
      "ARRAY_Y": "6",
      "BRAM_COLS": "3",
      "DSP_COLS": "6"
    }
  }
}
)json";
constexpr const char* SAMPLE_CONFIG_AUTO = R"json({
  "CONFIG_FILE_VERSION": "v2.8",
  "DEVICE_DATA_VERSION": "v2.0",
  "CUSTOMER_NAME": "EVAL-MSC-CUSTOM",
  "DEVICE_SIZE": "8x6",
  "TAPEOUT_DATE": "2026Q2",
  "AURORA_SETTINGS_TEMPLATE": "aurora/settings_template.json",
  "AURORA_YOSYS_TEMPLATE_SCRIPT": "aurora/aurora_template_script.ys",
  "AURORA_SYNPLIFY_TEMPLATE_SCRIPT": "aurora/aurora_template_script.prj",
  "AURORA_OPENFPGA_TEMPLATE_SCRIPT": "aurora/aurora_template_script.openfpga",
  "POWER_TEMPLATE": "aurora/power_template.json",
  "PIN_TABLE": "aurora/efpga_pinmap.csv",
  "FABRIC_KEY": "aurora/fabric_key.xml",
  "FIXED_SIM_OPENFPGA": "aurora/fixed_sim_openfpga.xml",
  "REPACK_DESIGN_CONSTRAINT": "aurora/repack_design_constraint.xml",
  "BITSTREAM_ANNOTATION": "aurora/bitstream_annotation.xml",
  "BITSTREAM_REMAPPING": "aurora/bitstream_remapping.xml",
  "FPGA_IO_MAP": "aurora/fpga_io_map.xml",
  "SB_MAPS": "aurora/SB_MAPS.yml",
  "CORNER_VPR_ARCH": "vpr.xml",
  "CORNER_SB_TEMPLATE_DIR": "CSV",
  "CORNER_RRGRAPH_BIN": "rr_graph.bin",
  "CORNER_ROUTER_LOOKAHEAD_BIN": "router_lookahead.bin",
  "CORNER_OPENFPGA_ARCH": "openfpga.xml",
  "CORNER_POWER_DATA": "power_data.json",
  "CORNERS": {
    "SSPG_0P72_M40C": "LVT/SSPG_0P72_M40C",
    "SSPG_0P72_125C": "LVT/SSPG_0P72_125C",
    "TT_0P80_25C": "LVT/TT_0P80_25C",
    "FFPG_0P88_125C": "LVT/FFPG_0P88_125C",
    "FFPG_0P88_M40C": "LVT/FFPG_0P88_M40C"
  },
  "AURORA_VERSION": "nightly",
  "BRAM_TYPE": "TDP_ECC",
  "CONFIG_TYPE": "WLBL",
  "CRR_VERSION": "v2.4",
  "BRAM_VERSION": "v4.0",
  "DSP_VERSION": "v4.0",
  "IO_PAD_VERSION": "v3.0",
  "DSP_COLS": "6",
  "DSP_SIZE": "1x3",
  "BRAM_COLS": "3",
  "BRAM_SIZE": "1x6",
  "IO_CAPACITY": "20",
  "DEVICE_TYPE": "CUSTOM",
  "DEVICE_TYPE_SETTINGS": {
    "LAYOUT_MODE": "AUTO",
    "MARGIN": 1.2
  }
}
)json";
constexpr const char* SAMPLE_CONFIG_RESOURCES = R"json({
  "CONFIG_FILE_VERSION": "v2.8",
  "DEVICE_DATA_VERSION": "v2.0",
  "CUSTOMER_NAME": "EVAL-MSC-CUSTOM",
  "DEVICE_SIZE": "8x6",
  "TAPEOUT_DATE": "2026Q2",
  "AURORA_SETTINGS_TEMPLATE": "aurora/settings_template.json",
  "AURORA_YOSYS_TEMPLATE_SCRIPT": "aurora/aurora_template_script.ys",
  "AURORA_SYNPLIFY_TEMPLATE_SCRIPT": "aurora/aurora_template_script.prj",
  "AURORA_OPENFPGA_TEMPLATE_SCRIPT": "aurora/aurora_template_script.openfpga",
  "POWER_TEMPLATE": "aurora/power_template.json",
  "PIN_TABLE": "aurora/efpga_pinmap.csv",
  "FABRIC_KEY": "aurora/fabric_key.xml",
  "FIXED_SIM_OPENFPGA": "aurora/fixed_sim_openfpga.xml",
  "REPACK_DESIGN_CONSTRAINT": "aurora/repack_design_constraint.xml",
  "BITSTREAM_ANNOTATION": "aurora/bitstream_annotation.xml",
  "BITSTREAM_REMAPPING": "aurora/bitstream_remapping.xml",
  "FPGA_IO_MAP": "aurora/fpga_io_map.xml",
  "SB_MAPS": "aurora/SB_MAPS.yml",
  "CORNER_VPR_ARCH": "vpr.xml",
  "CORNER_SB_TEMPLATE_DIR": "CSV",
  "CORNER_RRGRAPH_BIN": "rr_graph.bin",
  "CORNER_ROUTER_LOOKAHEAD_BIN": "router_lookahead.bin",
  "CORNER_OPENFPGA_ARCH": "openfpga.xml",
  "CORNER_POWER_DATA": "power_data.json",
  "CORNERS": {
    "SSPG_0P72_M40C": "LVT/SSPG_0P72_M40C",
    "SSPG_0P72_125C": "LVT/SSPG_0P72_125C",
    "TT_0P80_25C": "LVT/TT_0P80_25C",
    "FFPG_0P88_125C": "LVT/FFPG_0P88_125C",
    "FFPG_0P88_M40C": "LVT/FFPG_0P88_M40C"
  },
  "AURORA_VERSION": "nightly",
  "BRAM_TYPE": "TDP_ECC",
  "CONFIG_TYPE": "WLBL",
  "CRR_VERSION": "v2.4",
  "BRAM_VERSION": "v4.0",
  "DSP_VERSION": "v4.0",
  "IO_PAD_VERSION": "v3.0",
  "DSP_COLS": "6",
  "DSP_SIZE": "1x3",
  "BRAM_COLS": "3",
  "BRAM_SIZE": "1x6",
  "IO_CAPACITY": "20",
  "DEVICE_TYPE": "CUSTOM",
  "DEVICE_TYPE_SETTINGS": {
    "LAYOUT_MODE": "RESOURCES",
    "MARGIN": 1.2,
    "RESOURCES": {
      "clb": 100,
      "bram": 6,
      "dsp": 1,
      "io": 1281
    }
  }
}
)json";


constexpr const char* AUTO_DEVICE_LOG =
    "Calculated layout: WIDTH=104, HEIGHT=106, ARRAY_X=100, ARRAY_Y=102, "
    "BRAM_COLS=3, DSP_COLS=98\n"
    "Warning: xmllint not found; skipping optional XML formatting.\n"
    "Layout for FPGA_CUSTOM with width 104 and height 106 has been created in "
    "architecture file.\n";

}  // namespace

// --- config.json ------------------------------------------------------------

TEST(QLDeviceLayoutInfo, FixedDeviceResolvesFromTopLevelKeys) {
  json config = baseConfig();
  config["DEVICE_TYPE"] = "FIXED";

  const QLDeviceLayout layout = parseConfig(config);
  EXPECT_TRUE(layout.resolved);
  // DEVICE_SIZE is the ARRAY, so the grid is 4 larger in each direction.
  EXPECT_EQ(layout.arrayX, 8);
  EXPECT_EQ(layout.arrayY, 6);
  EXPECT_EQ(layout.width, 12);
  EXPECT_EQ(layout.height, 10);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({6}));
  EXPECT_EQ(layout.source, "config.json");
}

TEST(QLDeviceLayoutInfo, CustomLayoutModeResolvesFromItsOwnSection) {
  json config = baseConfig();
  config["DEVICE_TYPE"] = "CUSTOM";
  config["DEVICE_TYPE_SETTINGS"] = json{
      {"LAYOUT_MODE", "CUSTOM"},
      {"MARGIN", 1.2},
      {"CUSTOM", json{{"ARRAY_X", "8"}, {"ARRAY_Y", "6"}, {"BRAM_COLS", "3"}, {"DSP_COLS", "6"}}}};

  const QLDeviceLayout layout = parseConfig(config, "CUSTOM");
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.width, 12);
  EXPECT_EQ(layout.height, 10);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({6}));
}

TEST(QLDeviceLayoutInfo, CustomSectionFallsBackPerKeyToTopLevel) {
  // BRAM_COLS/DSP_COLS are optional in the CUSTOM section. Taking the section
  // wholesale would drop a column list the package only stated once.
  json config = baseConfig();
  config["DEVICE_TYPE"] = "CUSTOM";
  config["DEVICE_TYPE_SETTINGS"] =
      json{{"LAYOUT_MODE", "CUSTOM"},
           {"CUSTOM", json{{"ARRAY_X", "25"}, {"ARRAY_Y", "18"}, {"DSP_COLS", "8"}}}};

  const QLDeviceLayout layout = parseConfig(config, "CUSTOM");
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.width, 29);
  EXPECT_EQ(layout.height, 22);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));  // from the top level
  EXPECT_EQ(layout.dspCols, std::set<int>({8}));   // from the section
}

TEST(QLDeviceLayoutInfo, AcceptsUnquotedNumbers) {
  // The v2.8 samples quote every geometry value, but that is the generator's
  // convention rather than a guarantee.
  json config = baseConfig();
  config["BRAM_COLS"] = 3;
  config["DSP_COLS"] = 6;

  const QLDeviceLayout layout = parseConfig(config);
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({6}));
}

TEST(QLDeviceLayoutInfo, MultiColumnDeviceRoundTrips) {
  // TURNKEY-FPGA126126, the largest shipped part with both column types.
  json config;
  config["DEVICE_SIZE"] = "126x126";
  config["BRAM_COLS"] = "12,24,36,48,60,73,85,97,109,121";
  config["DSP_COLS"] = "6,18,30,42,54,67,79,91,103,115";

  const QLDeviceLayout layout = parseConfig(config);
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.width, 130);
  EXPECT_EQ(layout.height, 130);
  EXPECT_EQ(layout.bramCols.size(), 10u);
  EXPECT_EQ(layout.dspCols.size(), 10u);
  EXPECT_EQ(*layout.bramCols.begin(), 12);
  EXPECT_EQ(*layout.dspCols.rbegin(), 115);
}

TEST(QLDeviceLayoutInfo, MissingDeviceSizeIsUnresolvedNotAnError) {
  json config;
  config["BRAM_COLS"] = "3";
  EXPECT_FALSE(parseConfig(config).resolved);
}

TEST(QLDeviceLayoutInfo, MalformedDeviceSizeIsUnresolved) {
  json config = baseConfig();
  config["DEVICE_SIZE"] = "8-6";
  EXPECT_FALSE(parseConfig(config).resolved);
}

// --- auto_device.log --------------------------------------------------------

TEST(QLDeviceLayoutInfo, AutoDeviceLogCarriesTheColumnLists) {
  const QLDeviceLayout layout = parseLog(AUTO_DEVICE_LOG);
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.width, 104);
  EXPECT_EQ(layout.height, 106);
  EXPECT_EQ(layout.arrayX, 100);
  EXPECT_EQ(layout.arrayY, 102);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({98}));
  EXPECT_EQ(layout.source, "auto_device.log");
}

TEST(QLDeviceLayoutInfo, AccessorsReflectResolvedLayout) {
  QLDeviceLayoutInfo info{parseLog(AUTO_DEVICE_LOG)};
  EXPECT_EQ(info.width(), 104);
  EXPECT_EQ(info.height(), 106);
}

TEST(QLDeviceLayoutInfo, LogColumnsAreSpaceSeparated) {
  // The RESOURCES golden from featuretests/layout_resources.
  const QLDeviceLayout layout = parseLog(
      "Calculated layout: WIDTH=29, HEIGHT=22, ARRAY_X=25, ARRAY_Y=18, "
      "BRAM_COLS=3 13 18 23, DSP_COLS=8\n");
  EXPECT_TRUE(layout.resolved);
  EXPECT_EQ(layout.width, 29);
  EXPECT_EQ(layout.height, 22);
  EXPECT_EQ(layout.bramCols, std::set<int>({3, 13, 18, 23}));
  EXPECT_EQ(layout.dspCols, std::set<int>({8}));
}

TEST(QLDeviceLayoutInfo, EmptyColumnListIsValid) {
  const QLDeviceLayout layout = parseLog(
      "Calculated layout: WIDTH=12, HEIGHT=10, ARRAY_X=08, ARRAY_Y=06, "
      "BRAM_COLS=, DSP_COLS=\n");
  EXPECT_TRUE(layout.resolved);
  EXPECT_TRUE(layout.bramCols.empty());
  EXPECT_TRUE(layout.dspCols.empty());
}

TEST(QLDeviceLayoutInfo, LastCalculatedLayoutWins) {
  const QLDeviceLayout layout = parseLog(
      "Calculated layout: WIDTH=12, HEIGHT=10, ARRAY_X=08, ARRAY_Y=06, "
      "BRAM_COLS=3, DSP_COLS=6\n"
      "Calculated layout: WIDTH=29, HEIGHT=22, ARRAY_X=25, ARRAY_Y=18, "
      "BRAM_COLS=3 13, DSP_COLS=8\n");
  EXPECT_EQ(layout.width, 29);
  EXPECT_EQ(layout.bramCols, std::set<int>({3, 13}));
}

TEST(QLDeviceLayoutInfo, LogWithoutCalculatedLayoutIsUnresolved) {
  // What LAYOUT_MODE: CUSTOM produces - it returns before build_layout().
  EXPECT_FALSE(parseLog("Layout for FPGA_CUSTOM with width 12 and height 10 has "
                        "been created in architecture file.\n")
                   .resolved);
}

TEST(QLDeviceLayoutInfo, EmptyLogIsUnresolved) {
  EXPECT_FALSE(parseLog("").resolved);
}

// --- column list parsing ----------------------------------------------------

TEST(QLDeviceLayoutInfo, ColumnListAcceptsBothSeparators) {
  const std::set<int> expected{6, 18, 30};
  EXPECT_EQ(QLDeviceLayoutInfo::parseColumnList("6,18,30"), expected);
  EXPECT_EQ(QLDeviceLayoutInfo::parseColumnList("6 18 30"), expected);
  EXPECT_EQ(QLDeviceLayoutInfo::parseColumnList("6, 18,30"), expected);
}

TEST(QLDeviceLayoutInfo, ColumnListIsSortedAndDeduplicated) {
  EXPECT_EQ(QLDeviceLayoutInfo::parseColumnList("30,6,18,6"),
            std::set<int>({6, 18, 30}));
}

TEST(QLDeviceLayoutInfo, ColumnListOfNothingIsEmpty) {
  EXPECT_TRUE(QLDeviceLayoutInfo::parseColumnList("").empty());
  EXPECT_TRUE(QLDeviceLayoutInfo::parseColumnList("   ").empty());
}

// --- which modes are resolved during packing --------------------------------

namespace {

QLDeviceTarget targetWithLayoutName(const std::string& layout_name) {
  QLDeviceTarget target;
  target.device_variant_layout.name = layout_name;
  return target;
}

QLDeviceLayoutSettings settings(const std::string& device_type,
                                const std::string& layout_mode) {
  QLDeviceLayoutSettings layout_settings;
  layout_settings.config_found = true;
  if (!device_type.empty()) {
    layout_settings.device_type = device_type;
    layout_settings.device_type_present = true;
  }
  if (!layout_mode.empty()) {
    layout_settings.layout_mode = layout_mode;
    layout_settings.layout_mode_present = true;
    layout_settings.device_type_settings_present = true;
  }
  return layout_settings;
}

bool deferred(const std::string& device_type, const std::string& layout_mode,
              const std::string& layout_name = "FPGA0806") {
  return QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
      settings(device_type, layout_mode), targetWithLayoutName(layout_name));
}

}  // namespace

TEST(QLDeviceLayoutInfo, FixedAndCustomAreKnownBeforePacking) {
  EXPECT_FALSE(deferred("FIXED", ""));
  EXPECT_FALSE(deferred("CUSTOM", "CUSTOM"));
}

TEST(QLDeviceLayoutInfo, AutoAndResourcesAreDeferredToPacking) {
  EXPECT_TRUE(deferred("CUSTOM", "AUTO"));
  EXPECT_TRUE(deferred("CUSTOM", "RESOURCES"));
}

TEST(QLDeviceLayoutInfo, PreContractPackageIsDeferredOnItsLayoutName) {
  // No DEVICE_TYPE: Packing() re-shapes on the layout name alone, so the
  // config describes a fabric the run will discard.
  EXPECT_TRUE(deferred("", "", "FPGA_AUTO"));
  EXPECT_TRUE(deferred("", "", "FPGA_CUSTOM"));
  EXPECT_FALSE(deferred("", "", "FPGA3648"));
}

TEST(QLDeviceLayoutInfo, SampleFixedConfigResolvesImmediately) {
  // config_fixed.json: DEVICE_TYPE: FIXED, no DEVICE_TYPE_SETTINGS at all.
  const json config = json::parse(SAMPLE_CONFIG_FIXED);
  EXPECT_FALSE(QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
      settings("FIXED", ""), targetWithLayoutName("FPGA0806")));

  QLDeviceLayout layout;
  ASSERT_TRUE(QLDeviceLayoutInfo::parseDeviceConfig(config, "", layout));
  EXPECT_EQ(layout.arrayX, 8);
  EXPECT_EQ(layout.arrayY, 6);
  EXPECT_EQ(layout.width, 12);
  EXPECT_EQ(layout.height, 10);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({6}));
}

TEST(QLDeviceLayoutInfo, SampleCustomConfigResolvesImmediately) {
  // config_custom.json: DEVICE_TYPE: CUSTOM, LAYOUT_MODE: CUSTOM, geometry in
  // DEVICE_TYPE_SETTINGS.CUSTOM.
  const json config = json::parse(SAMPLE_CONFIG_CUSTOM);
  EXPECT_FALSE(QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
      settings("CUSTOM", "CUSTOM"), targetWithLayoutName("FPGA0806")));

  QLDeviceLayout layout;
  ASSERT_TRUE(QLDeviceLayoutInfo::parseDeviceConfig(config, "CUSTOM", layout));
  EXPECT_EQ(layout.width, 12);
  EXPECT_EQ(layout.height, 10);
  EXPECT_EQ(layout.bramCols, std::set<int>({3}));
  EXPECT_EQ(layout.dspCols, std::set<int>({6}));
}

TEST(QLDeviceLayoutInfo, SampleAutoConfigIsDeferredToPacking) {
  // config_auto.json: LAYOUT_MODE: AUTO. The config carries a DEVICE_SIZE that
  // would parse if asked, but the geometry it describes is the package's own,
  // not the one the run will produce - layoutIsResolvedDuringPacking() must
  // defer regardless of what parseDeviceConfig() alone could extract.
  const json config = json::parse(SAMPLE_CONFIG_AUTO);
  EXPECT_TRUE(QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
      settings("CUSTOM", "AUTO"), targetWithLayoutName("FPGA0806")));

  QLDeviceLayout layout;
  EXPECT_TRUE(QLDeviceLayoutInfo::parseDeviceConfig(config, "AUTO", layout));
}

TEST(QLDeviceLayoutInfo, SampleResourcesConfigIsDeferredToPacking) {
  // config_resources.json: LAYOUT_MODE: RESOURCES. Same reasoning as AUTO above -
  // add_layout.py sizes the fabric from DEVICE_TYPE_SETTINGS.RESOURCES and MARGIN,
  // and only auto_device.log records what it produced.
  const json config = json::parse(SAMPLE_CONFIG_RESOURCES);
  EXPECT_TRUE(QLDeviceLayoutInfo::layoutIsResolvedDuringPacking(
      settings("CUSTOM", "RESOURCES"), targetWithLayoutName("FPGA0806")));
  ASSERT_TRUE(config["DEVICE_TYPE_SETTINGS"]["RESOURCES"].is_object());
  EXPECT_EQ(config["DEVICE_TYPE_SETTINGS"]["RESOURCES"]["bram"], 6);
}
