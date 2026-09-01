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

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace FOEDAG;

// The power calculator sizes the fabric from these column counts
// (CompilerOpenFPGA_ql::configurePowerCalculatorInput). It used to divide them back
// out of deviceResourceInformation(), which reports nothing for a package whose
// config.json lacks the geometry keys - and the calculator then took 0 bram and dsp
// columns, and a clb count too high by their sum, as fact. The architecture is the
// only source that answers for a generated device too, so these pin the shapes
// aurora's add_layout.py emits.

namespace {

const char* const TILES = R"(  <tiles>
    <tile name="clb" width="1" height="1"/>
    <tile name="bram" width="1" height="6"/>
    <tile name="dsp" width="1" height="3"/>
  </tiles>
)";

// TURNKEY-FPGA3030 as add_layout.py writes it: "DSP_COLS": "6,19" and
// "BRAM_COLS": "12,25" become startx 7/20 and 13/26, one <region> each.
std::string fpga3030Layout(const std::string& name) {
  return "  <fixed_layout name=\"" + name + R"(" width="34" height="34">
    <row type="io_top" starty="H-2" priority="100"/>
    <col type="io_left" startx="1" priority="100"/>
    <col type="io_right" startx="W-2" priority="100"/>
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

TEST(TilesCfgParser, CountsBlockColumnsOfTheNamedLayout) {
  const auto path = writeArch("one", fpga3030Layout("FPGA3030"));
  TilesCfgResult result = parseTilesCfg(path, "FPGA3030");

  EXPECT_TRUE(result.error.empty()) << result.error;
  EXPECT_EQ(result.columnsOf("bram"), 2);
  EXPECT_EQ(result.columnsOf("dsp"), 2);
  // what the power calculator does with them, and the number resources.json used
  // to yield for this device
  EXPECT_EQ(34 - 2 - 2 - result.columnsOf("bram") - result.columnsOf("dsp"), 26);

  // the io_* regions sharing those columns are counted under their own type, not
  // folded into bram/dsp
  EXPECT_EQ(result.columnsOf("io_dsp_top"), 1);

  // the tile sizes the parser already returned are unaffected
  EXPECT_EQ(result.tiles_cfg["bram"].second, 6);
  EXPECT_EQ(result.tiles_cfg["dsp"].second, 3);
}

TEST(TilesCfgParser, AbsentBlockTypeIsNoColumns) {
  const auto path = writeArch("one", fpga3030Layout("FPGA3030"));
  EXPECT_EQ(parseTilesCfg(path, "FPGA3030").columnsOf("nonesuch"), 0);
}

TEST(TilesCfgParser, FallsBackToTheOnlyLayoutWhenTheNameDoesNotMatch) {
  // a device Aurora generated carries the layout name it was given, which need not
  // be the one the device target was discovered under
  const auto path = writeArch("one", fpga3030Layout("FPGA3030"));
  TilesCfgResult result = parseTilesCfg(path, "SOMETHING_ELSE");

  EXPECT_TRUE(result.error.empty()) << result.error;
  EXPECT_EQ(result.columnsOf("bram"), 2);
}

TEST(TilesCfgParser, PicksTheNamedLayoutOutOfSeveral) {
  const auto path =
      writeArch("multi", fpga3030Layout("FPGA3030") + fpga3030Layout("FPGA1616"));
  TilesCfgResult result = parseTilesCfg(path, "FPGA1616");

  EXPECT_TRUE(result.error.empty()) << result.error;
  EXPECT_EQ(result.columnsOf("bram"), 2);
}

TEST(TilesCfgParser, RefusesToGuessAmongSeveralUnnamedLayouts) {
  const auto path =
      writeArch("multi", fpga3030Layout("FPGA3030") + fpga3030Layout("FPGA1616"));
  TilesCfgResult result = parseTilesCfg(path, "NOPE");

  EXPECT_FALSE(result.error.empty());
  EXPECT_NE(result.error.find("NOPE"), std::string::npos) << result.error;
}

TEST(TilesCfgParser, ReportsAnUncountableColumnRatherThanNone) {
  // a <col> with repeatx spans a number of columns only the layout's own W/H
  // expressions give, which this parser does not evaluate. -1 so the caller can
  // refuse; 0 would have understated the fabric silently.
  const char* const layout = R"(  <fixed_layout name="REPEAT" width="34" height="34">
    <col type="bram" startx="3" repeatx="10" priority="30"/>
    <region type="dsp" startx="7" endx="7" starty="2" endy="H-3" priority="30"/>
  </fixed_layout>
)";
  const auto path = writeArch("repeat", layout);
  TilesCfgResult result = parseTilesCfg(path, "REPEAT");

  EXPECT_TRUE(result.error.empty()) << result.error;
  EXPECT_EQ(result.columnsOf("bram"), -1);
  EXPECT_EQ(result.columnsOf("dsp"), 1);
}

TEST(TilesCfgParser, CountsEveryColumnOfASpanningRegion) {
  const char* const layout = R"(  <fixed_layout name="SPAN" width="34" height="34">
    <region type="bram" startx="10" endx="12" starty="2" endy="H-3" priority="30"/>
  </fixed_layout>
)";
  const auto path = writeArch("span", layout);

  EXPECT_EQ(parseTilesCfg(path, "SPAN").columnsOf("bram"), 3);
}

TEST(TilesCfgParser, ReportsAnArchitectureWithNoLayout) {
  const auto dir = std::filesystem::temp_directory_path() / "foedag_tilescfg_test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "nolayout.xml";
  {
    std::ofstream out(path.string());
    out << "<architecture>\n" << TILES << "</architecture>\n";
  }

  EXPECT_FALSE(parseTilesCfg(path, "FPGA3030").error.empty());
}
