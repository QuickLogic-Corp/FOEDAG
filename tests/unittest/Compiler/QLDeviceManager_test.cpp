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

// ---- config.json geometry helpers (issue #2257) ---------------------------

TEST(QLDeviceManager, ParseWholeNumberAcceptsSurroundingWhitespace) {
  int value = -1;

  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("42", value));
  EXPECT_EQ(value, 42);

  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("  42", value));
  EXPECT_EQ(value, 42);

  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("42  ", value));
  EXPECT_EQ(value, 42);

  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("\t42 \t", value));
  EXPECT_EQ(value, 42);
}

TEST(QLDeviceManager, ParseWholeNumberRejectsTrailingJunk) {
  int value = -1;

  // the case the helper exists for: a half-parsed value would mis-size a device
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("12abc", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("12 34", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("0x10", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("1.5", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("abc", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("", value));
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("   ", value));

  EXPECT_EQ(value, -1);  // outputs left alone, not half-written
}

TEST(QLDeviceManager, ParseWholeNumberRejectsOverflow) {
  int value = -1;

  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("99999999999999999999", value));
  EXPECT_EQ(value, -1);
}

TEST(QLDeviceManager, ParseWholeNumberAcceptsSign) {
  int value = -1;

  // documents current behaviour, which is why every caller that needs a
  // non-negative value checks the sign itself
  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("-5", value));
  EXPECT_EQ(value, -5);

  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("+5", value));
  EXPECT_EQ(value, 5);
}

TEST(QLDeviceManager, ParseDeviceGeometrySplitsOnEitherCase) {
  int x = -1;
  int y = -1;

  EXPECT_TRUE(QLDeviceManager::parseDeviceGeometry("78x78", x, y));
  EXPECT_EQ(x, 78);
  EXPECT_EQ(y, 78);

  EXPECT_TRUE(QLDeviceManager::parseDeviceGeometry("1X6", x, y));
  EXPECT_EQ(x, 1);
  EXPECT_EQ(y, 6);

  EXPECT_TRUE(QLDeviceManager::parseDeviceGeometry(" 1 x 3 ", x, y));
  EXPECT_EQ(x, 1);
  EXPECT_EQ(y, 3);
}

TEST(QLDeviceManager, ParseDeviceGeometryRejectsMalformed) {
  int x = 0;
  int y = 0;

  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("78", x, y));    // no separator
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("x5", x, y));    // no width
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("78x", x, y));   // no height
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("3x4x5", x, y));  // splits on the first x
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("", x, y));
}

TEST(QLDeviceManager, CountDeviceColumnsCountsEntries) {
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("3,10,17"), 3);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("10"), 1);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(" 3 , 10 "), 2);
}

TEST(QLDeviceManager, CountDeviceColumnsIgnoresBlankEntries) {
  // an empty list is a device with none of that column, which is legal
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(""), 0);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(" "), 0);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(","), 0);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(" , , "), 0);

  // a whitespace-only entry is a column short of nothing, so it does not count
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("3, ,10"), 2);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("3,\t,10"), 2);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("3,,10"), 2);

  // leading and trailing separators do not invent entries
  EXPECT_EQ(QLDeviceManager::countDeviceColumns(",3,10"), 2);
  EXPECT_EQ(QLDeviceManager::countDeviceColumns("3,10,"), 2);
}

TEST(QLDeviceManager, ConfigJSONTextReadsBothSpellings) {
  // the inconsistency this helper exists for: packages spell the same key as a
  // JSON string or a JSON number
  const json config = {{"IO_CAPACITY_STR", "20"}, {"IO_CAPACITY_NUM", 20}};
  std::string text;

  EXPECT_TRUE(QLDeviceManager::configJSONText(config, "IO_CAPACITY_STR", text));
  EXPECT_EQ(text, "20");

  text.clear();
  EXPECT_TRUE(QLDeviceManager::configJSONText(config, "IO_CAPACITY_NUM", text));
  EXPECT_EQ(text, "20");
}

TEST(QLDeviceManager, ConfigJSONTextRejectsMissingAndUnusableValues) {
  const json config = {{"REAL", 1.5}, {"LIST", {1, 2}}, {"NESTED", {{"a", 1}}}};
  std::string text{"untouched"};

  EXPECT_FALSE(QLDeviceManager::configJSONText(config, "ABSENT", text));
  EXPECT_FALSE(QLDeviceManager::configJSONText(config, "REAL", text));
  EXPECT_FALSE(QLDeviceManager::configJSONText(config, "LIST", text));
  EXPECT_FALSE(QLDeviceManager::configJSONText(config, "NESTED", text));

  EXPECT_EQ(text, "untouched");
}
