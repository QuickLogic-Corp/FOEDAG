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

#include "Compiler/TilesCfgParser.h"

#include "Compiler/QLDeviceManager.h"

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace FOEDAG;

// parseLayoutGeometry() is written into a generated device's config.json so the
// generated package describes its own fabric instead of the one it was copied
// from (issue #2257). It must agree with how the device packages' own
// aurora/add_layout.py reads the same file back in read_vars_from_arch(): the
// array is the layout less its io and empty rings, and BRAM_COLS/DSP_COLS are one
// lower than the <region> they produce.

namespace {

const char* const TILES = R"(  <tiles>
    <tile name="clb" width="1" height="1"/>
    <tile name="bram" width="1" height="6"/>
    <tile name="dsp" width="1" height="3"/>
  </tiles>
)";

// TURNKEY-FPGA3030 as add_layout.py writes it: "BRAM_COLS": "12,25" and
// "DSP_COLS": "6,19" become regions at startx 13/26 and 7/20.
std::string fpga3030Layout(const std::string& name) {
  return "  <fixed_layout name=\"" + name + R"(" width="34" height="34">
    <row type="io_top" starty="H-2" priority="100"/>
    <col type="io_left" startx="1" priority="100"/>
    <col type="EMPTY" startx="0" priority="100"/>
    <region type="dsp" startx="7" endx="7" starty="2" endy="H-3" priority="30"/>
    <region type="io_dsp_top" startx="7" endx="7" starty="H-2" endy="H-2" priority="200"/>
    <region type="dsp" startx="20" endx="20" starty="2" endy="H-3" priority="30"/>
    <region type="bram" startx="13" endx="13" starty="2" endy="H-3" priority="30"/>
    <region type="bram" startx="26" endx="26" starty="2" endy="H-3" priority="30"/>
  </fixed_layout>
)";
}

std::filesystem::path writeArch(const std::string& stem, const std::string& layouts) {
  const auto dir = std::filesystem::temp_directory_path() / "foedag_tilescfg_test";
  std::filesystem::create_directories(dir);
  const auto path = dir / (stem + ".xml");
  std::ofstream out(path.string());
  out << "<architecture>\n" << TILES << "  <layout>\n" << layouts << "  </layout>\n</architecture>\n";
  return path;
}

}  // namespace

TEST(ParseLayoutGeometry, ReadsTheArrayAndColumnsOfTheNamedLayout) {
  const auto path = writeArch("geom_one", fpga3030Layout("FPGA3030"));
  LayoutGeometryResult geometry = parseLayoutGeometry(path, "FPGA3030");

  EXPECT_TRUE(geometry.error.empty()) << geometry.error;
  EXPECT_EQ(geometry.array_x, 30);  // 34 - 4
  EXPECT_EQ(geometry.array_y, 30);
  // the values the shipped TURNKEY-FPGA3030 config.json carries, so a generated
  // copy is written the same way it was shipped
  EXPECT_EQ(geometry.bram_cols, "12,25");
  EXPECT_EQ(geometry.dsp_cols, "6,19");
}

TEST(ParseLayoutGeometry, DerivesTheSameCountsFromWhatItWrote) {
  // the point of writing it: the derivation must answer for the generated device
  const auto path = writeArch("geom_one", fpga3030Layout("FPGA3030"));
  LayoutGeometryResult geometry = parseLayoutGeometry(path, "FPGA3030");
  ASSERT_TRUE(geometry.error.empty()) << geometry.error;

  json config;
  config["DEVICE_SIZE"] =
      std::to_string(geometry.array_x) + "x" + std::to_string(geometry.array_y);
  config["BRAM_COLS"] = geometry.bram_cols;
  config["DSP_COLS"] = geometry.dsp_cols;
  config["BRAM_SIZE"] = "1x6";
  config["DSP_SIZE"] = "1x3";
  config["IO_CAPACITY"] = "20";

  std::string error;
  const auto resources = QLDeviceManager::deriveResourceCounts(config, 34, 34, &error);
  ASSERT_FALSE(resources.empty()) << error;

  // FPGA3030's retired resources.json: clb 780 / bram 10 / dsp 20 / io 2400
  int clb = 0, bram = 0, dsp = 0, io = 0;
  for (const auto& [name, count] : resources) {
    if (name == "clb") clb = count;
    if (name == "bram") bram = count;
    if (name == "dsp") dsp = count;
    if (name == "io") io = count;
  }
  EXPECT_EQ(clb, 780);
  EXPECT_EQ(bram, 10);
  EXPECT_EQ(dsp, 20);
  EXPECT_EQ(io, 2400);
}

TEST(ParseLayoutGeometry, PicksTheNamedLayoutOutOfSeveral) {
  const auto path =
      writeArch("geom_multi", fpga3030Layout("FPGA3030") + fpga3030Layout("FPGA1616"));
  LayoutGeometryResult geometry = parseLayoutGeometry(path, "FPGA1616");

  EXPECT_TRUE(geometry.error.empty()) << geometry.error;
  EXPECT_EQ(geometry.array_x, 30);
}

TEST(ParseLayoutGeometry, FallsBackToTheFirstLayoutLikeAddLayoutDoes) {
  // a generated device's layout is renamed, so the name need not be found
  const auto path =
      writeArch("geom_multi", fpga3030Layout("FPGA3030") + fpga3030Layout("FPGA1616"));
  LayoutGeometryResult geometry = parseLayoutGeometry(path, "NO_SUCH_LAYOUT");

  EXPECT_TRUE(geometry.error.empty()) << geometry.error;
  EXPECT_EQ(geometry.array_x, 30);
}

TEST(ParseLayoutGeometry, ReportsALayoutWithNoBlockColumns) {
  // legal - a fabric of nothing but clb. Empty lists, not an error.
  const char* const bare = R"(  <fixed_layout name="BARE" width="12" height="10">
    <col type="io_left" startx="1" priority="100"/>
  </fixed_layout>
)";
  LayoutGeometryResult geometry = parseLayoutGeometry(writeArch("geom_bare", bare), "BARE");

  EXPECT_TRUE(geometry.error.empty()) << geometry.error;
  EXPECT_EQ(geometry.array_x, 8);
  EXPECT_EQ(geometry.bram_cols, "");
  EXPECT_EQ(geometry.dsp_cols, "");
}

TEST(ParseLayoutGeometry, ReportsAnArchitectureWithNoLayout) {
  const auto dir = std::filesystem::temp_directory_path() / "foedag_tilescfg_test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "geom_nolayout.xml";
  {
    std::ofstream out(path.string());
    out << "<architecture>\n" << TILES << "</architecture>\n";
  }

  EXPECT_FALSE(parseLayoutGeometry(path, "FPGA3030").error.empty());
}

TEST(ParseLayoutGeometry, ReportsALayoutSmallerThanItsRings) {
  const char* const tiny = R"(  <fixed_layout name="TINY" width="3" height="3">
  </fixed_layout>
)";
  EXPECT_FALSE(parseLayoutGeometry(writeArch("geom_tiny", tiny), "TINY").error.empty());
}
