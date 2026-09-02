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

#include "Compiler/QLDeviceManager.h"

#include "gtest/gtest.h"
using namespace FOEDAG;

TEST(QLDeviceManager, NormalizeVersionStringSpellings) {
  // spellings device data has used. deviceDSPVersion() still has its own copy of
  // this parsing, so the last case covers leading letters, not DSP behaviour.
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("v2.4"), "2.4");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("V2.4"), "2.4");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("2.4"), "2.4");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("2_4"), "2.4");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("V2_4"), "2.4");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("DSPV2"), "2.0");
}

TEST(QLDeviceManager, NormalizeVersionStringSuppliesMissingMinor) {
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("v2"), "2.0");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("3"), "3.0");
}

TEST(QLDeviceManager, NormalizeVersionStringRejectsValuesWithoutDigits) {
  EXPECT_EQ(QLDeviceManager::normalizeVersionString(""), "");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("none"), "");
  EXPECT_EQ(QLDeviceManager::normalizeVersionString("v"), "");
}

TEST(QLDeviceManager, ParseVersionStringSplitsNormalisedForm) {
  int major = -1;
  int minor = -1;

  ASSERT_TRUE(QLDeviceManager::parseVersionString("2.4", major, minor));
  EXPECT_EQ(major, 2);
  EXPECT_EQ(minor, 4);

  ASSERT_TRUE(QLDeviceManager::parseVersionString("10.11", major, minor));
  EXPECT_EQ(major, 10);
  EXPECT_EQ(minor, 11);
}

TEST(QLDeviceManager, ParseVersionStringRejectsAnythingElse) {
  int major = -1;
  int minor = -1;

  // an unset CRR_VERSION arrives as an empty string
  EXPECT_FALSE(QLDeviceManager::parseVersionString("", major, minor));
  // deviceCRRVersion() always supplies a minor, so a bare major is a caller error
  EXPECT_FALSE(QLDeviceManager::parseVersionString("2", major, minor));
  EXPECT_FALSE(QLDeviceManager::parseVersionString("v2.4", major, minor));
  EXPECT_FALSE(QLDeviceManager::parseVersionString("2.4.1", major, minor));
  EXPECT_FALSE(QLDeviceManager::parseVersionString("2.x", major, minor));

  // a rejected parse leaves the caller's values untouched
  EXPECT_EQ(major, -1);
  EXPECT_EQ(minor, -1);
}

TEST(QLDeviceManager, ParseVersionStringRejectsOverflow) {
  int major = -1;
  int minor = -1;

  // major overflows
  EXPECT_FALSE(
      QLDeviceManager::parseVersionString("99999999999999999999.0", major, minor));
  EXPECT_EQ(major, -1);
  EXPECT_EQ(minor, -1);

  // minor overflows: outputs still left alone, not half-written
  EXPECT_FALSE(
      QLDeviceManager::parseVersionString("2.99999999999999999999", major, minor));
  EXPECT_EQ(major, -1);
  EXPECT_EQ(minor, -1);
}
