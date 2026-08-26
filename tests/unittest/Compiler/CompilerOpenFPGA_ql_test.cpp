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

#include "Compiler/CompilerOpenFPGA_ql.h"

#include "gtest/gtest.h"

using namespace FOEDAG;

// The post-synthesis netlist is written with `write_blif -param`, so it carries
// .param lines. VPR fatally rejects those unless it reads the file as extended
// BLIF, so only "eblif" -- or an empty setting, which passes no flag and leaves
// the format the OpenFPGA template substitutes -- is usable.

TEST(VprCircuitFormat, EblifIsAccepted) {
  EXPECT_TRUE(vprCircuitFormatError("eblif").empty());
}

TEST(VprCircuitFormat, EmptyIsAccepted) {
  // No flag is appended, so the template's substitution (eblif) applies.
  EXPECT_TRUE(vprCircuitFormatError("").empty());
}

TEST(VprCircuitFormat, StrictBlifIsRejected) {
  // The value that breaks every design carrying .param -- DSP MODE_BITS today.
  EXPECT_FALSE(vprCircuitFormatError("blif").empty());
}

TEST(VprCircuitFormat, AutoIsRejected) {
  // "auto" is not a safe middle ground: it infers the format from the file
  // extension, and the flow names the netlist .blif, so it resolves to strict
  // BLIF and fails exactly like "blif".
  EXPECT_FALSE(vprCircuitFormatError("auto").empty());
}

TEST(VprCircuitFormat, MatchIsCaseSensitive) {
  // VPR's own option parsing is case-sensitive, so accepting "EBLIF" here
  // would only move the failure into VPR.
  EXPECT_FALSE(vprCircuitFormatError("EBLIF").empty());
}

TEST(VprCircuitFormat, RejectionNamesTheSettingAndTheFix) {
  // The whole point of checking early is a message the user can act on, so
  // assert the message carries the setting path, the offending value and the
  // value to use.
  const std::string message = vprCircuitFormatError("blif");
  EXPECT_NE(message.find("vpr>filename>circuit_format"), std::string::npos);
  EXPECT_NE(message.find("\"blif\""), std::string::npos);
  EXPECT_NE(message.find("eblif"), std::string::npos);
}
