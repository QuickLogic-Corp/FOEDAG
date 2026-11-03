/*
Copyright 2026 QuickLogic team

GPL License

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

#include "Compiler/DumpOptions.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

using namespace FOEDAG;
using Tokens = std::vector<std::string>;

namespace {

Tokens vpr(const std::string& options) {
  return splitCommandTokens("vpr arch.xml design.blif " + options);
}

}  // namespace

TEST(DumpOptions, SplitDropsEmptyTokens) {
  EXPECT_EQ(splitCommandTokens("  vpr  a.xml\tb.blif \n"), (Tokens{"vpr", "a.xml", "b.blif"}));
  EXPECT_TRUE(splitCommandTokens("").empty());
}

TEST(DumpOptions, EachAlwaysDumpingOptionSpaceForm) {
  for (const std::string option :
       {"--write_rr_graph", "--write_router_lookahead",
        "--write_intra_cluster_router_lookahead", "--write_placement_delay_lookup"}) {
    EXPECT_EQ(findDumpOptions(vpr(option + " out.bin --pack")), Tokens{option}) << option;
  }
}

TEST(DumpOptions, EachAlwaysDumpingOptionEqualsForm) {
  for (const std::string option :
       {"--write_rr_graph", "--write_router_lookahead",
        "--write_intra_cluster_router_lookahead", "--write_placement_delay_lookup"}) {
    EXPECT_EQ(findDumpOptions(vpr("--pack " + option + "=out.bin")), Tokens{option}) << option;
  }
}

TEST(DumpOptions, EchoFileOnIsFoundInBothForms) {
  EXPECT_EQ(findDumpOptions(vpr("--echo_file on --place")), Tokens{"--echo_file on"});
  EXPECT_EQ(findDumpOptions(vpr("--place --echo_file=on")), Tokens{"--echo_file on"});
}

TEST(DumpOptions, EchoFileOffIsAllowed) {
  EXPECT_TRUE(findDumpOptions(vpr("--echo_file off --place")).empty());
  EXPECT_TRUE(findDumpOptions(vpr("--echo_file=off")).empty());
}

TEST(DumpOptions, EchoFileWithoutOffIsFound) {
  EXPECT_EQ(findDumpOptions(vpr("--echo_file")), Tokens{"--echo_file"});
  EXPECT_EQ(findDumpOptions(vpr("--echo_file=")), Tokens{"--echo_file"});
  EXPECT_EQ(findDumpOptions(vpr("--echo_file ON")), Tokens{"--echo_file ON"});
}

TEST(DumpOptions, NearMissNamesAreNotFound) {
  EXPECT_TRUE(findDumpOptions(vpr("--read_rr_graph rr.bin --read_router_lookahead l.bin "
                                  "--read_placement_delay_lookup d.bin "
                                  "--write_rr_graph_x a --write_rr_graphs b "
                                  "-write_rr_graph c --echo_file_x on --echo on "
                                  "--write_flat_place f.place"))
                  .empty());
}

TEST(DumpOptions, ValuesAreNotMistakenForOptions) {
  // A file named like an option is a value, not the option.
  EXPECT_TRUE(findDumpOptions(vpr("--sdc_file --write_rr_graph.sdc")).empty());
}

TEST(DumpOptions, EmptyInputFindsNothing) {
  EXPECT_TRUE(findDumpOptions({}).empty());
  EXPECT_TRUE(findDumpOptions(vpr("")).empty());
}

TEST(DumpOptions, AllHitsAreReportedInOrder) {
  EXPECT_EQ(findDumpOptions(vpr("--echo_file on --write_rr_graph=rr.xml "
                                "--write_router_lookahead l.bin")),
            (Tokens{"--echo_file on", "--write_rr_graph", "--write_router_lookahead"}));
}
