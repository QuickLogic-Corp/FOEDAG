/*
Copyright 2026 The Foedag team

GPL License

Copyright (c) 2026 The Open-Source FPGA Foundation

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

#include "Compiler/WildcardFileFinder.h"

#include <QTemporaryDir>
#include <fstream>

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {
void touch(const std::filesystem::path &path) { std::ofstream{path} << "x"; }
}  // namespace

// The Messages dock shows one log for STA's timing_analysis*.rpt: the plain
// file of a single-corner run, or the default corner's file.
TEST(WildcardFileFinder, DefaultFilePathIsTheFileOneViewShows) {
  QTemporaryDir dir;
  const std::filesystem::path root{dir.path().toStdString()};
  const std::string pattern{"timing_analysis*.rpt"};

  EXPECT_TRUE(WildcardFileFinder(root, pattern).defaultFilePath().empty());

  touch(root / "timing_analysis.rpt");
  EXPECT_EQ(WildcardFileFinder(root, pattern).defaultFilePath(),
            root / "timing_analysis.rpt");

  touch(root / "timing_analysis_LVT_TT_0P80_25C.rpt");
  WildcardFileFinder corners{root, pattern};
  EXPECT_EQ(corners.defaultFilePath(),
            root / ("timing_analysis_" + corners.defaultProfile() + ".rpt"));
}
