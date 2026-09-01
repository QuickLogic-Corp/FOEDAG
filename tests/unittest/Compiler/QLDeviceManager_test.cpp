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

TEST(QLDeviceManager, ParseWholeNumberRejectsNegative) {
  int value = -1;

  // stoi would take the sign; nothing config.json spells this way has a
  // meaningful negative value
  EXPECT_FALSE(QLDeviceManager::parseWholeNumber("-5", value));
  EXPECT_EQ(value, -1);  // left alone, not half-written

  // an explicit plus is still a whole number
  EXPECT_TRUE(QLDeviceManager::parseWholeNumber("+5", value));
  EXPECT_EQ(value, 5);
}

TEST(QLDeviceManager, ParseDeviceGeometryRejectsNegativeDimensions) {
  int x = 0;
  int y = 0;

  // "-30x-20" used to parse into a negative width and height
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("-30x-20", x, y));
  EXPECT_FALSE(QLDeviceManager::parseDeviceGeometry("30x-20", x, y));
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

// ---- derived counts vs. the shipped resources.json ------------------------
//
// The counts derived from config.json must equal the ones vpr wrote into the
// resources.json these packages shipped before it was retired. Both are inlined
// rather than read off disk, so the check needs no device package and cannot go
// stale against a moving checkout.
//
// The devices below are every QLF_K6N10 package that shipped both files with the
// full geometry key set. Their config.json is verbatim except for IO_CAPACITY,
// which none of them declare and which device_data still has to be backfilled
// with; 20 is the value their vpr.xml used and the one that reproduces the io
// counts below. Packages missing the other geometry keys are covered by
// DeriveResourceCountsRejectsIncompleteConfig instead.

namespace {

struct DerivedResourcesCase {
  const char* device;       // device_data path, for the failure message
  const char* config_json;  // verbatim from that package's config.json
  int layout_width;         // its vpr layout, DEVICE_SIZE plus the io/empty rings
  int layout_height;
  int clb;                  // verbatim from that package's resources.json
  int bram;
  int dsp;
  int io;
};

int countOf(const std::vector<std::tuple<std::string, int>>& resources,
            const std::string& name) {
  for (const auto& [resource_name, resource_count] : resources) {
    if (resource_name == name) return resource_count;
  }
  return -1;
}

}  // namespace

TEST(QLDeviceManager, DeriveResourceCountsMatchesShippedResourcesJSON) {
  const DerivedResourcesCase cases[] = {
      {"QLF_K6N10/GF/12nm/IDAHO-FPGA0806_WLBL",
       R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
           "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": "20"})",
       12, 10, 36, 1, 2, 560},

      {"QLF_K6N10/GF/12nm/TURNKEY-FPGA3030",
       R"({"DEVICE_SIZE": "30x30", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
           "BRAM_COLS": "12,25", "DSP_COLS": "6,19", "IO_CAPACITY": "20"})",
       34, 34, 780, 10, 20, 2400},

      {"QLF_K6N10/GF/12nm/TURNKEY-FPGA7878",
       R"({"DEVICE_SIZE": "78x78", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
           "BRAM_COLS": "12,24,36,49,61,73", "DSP_COLS": "6,18,30,43,55,67",
           "IO_CAPACITY": "20"})",
       82, 82, 5148, 78, 156, 6240},

      {"QLF_K6N10/GF/12nm/TURNKEY-FPGA126126",
       R"({"DEVICE_SIZE": "126x126", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
           "BRAM_COLS": "12,24,36,48,60,73,85,97,109,121",
           "DSP_COLS": "6,18,30,42,54,67,79,91,103,115",
           "IO_CAPACITY": "20"})",
       130, 130, 13356, 210, 420, 10080},

      {"QLF_K6N10/GF/12nm/TURNKEY-FPGA258258",
       R"({"DEVICE_SIZE": "258x258", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
           "BRAM_COLS": "12,24,36,48,60,72,84,96,108,120,133,145,157,169,181,193,205,217,229,241,253",
           "DSP_COLS": "6,18,30,42,54,66,78,90,102,114,126,139,151,163,175,187,199,211,223,235,247",
           "IO_CAPACITY": "20"})",
       262, 262, 55728, 903, 1806, 20640},
  };

  for (const auto& test_case : cases) {
    std::string error;
    const json config = json::parse(test_case.config_json);
    const auto resources = QLDeviceManager::deriveResourceCounts(
        config, test_case.layout_width, test_case.layout_height, &error);

    ASSERT_FALSE(resources.empty()) << test_case.device << ": " << error;
    EXPECT_EQ(countOf(resources, "clb"), test_case.clb) << test_case.device;
    EXPECT_EQ(countOf(resources, "bram"), test_case.bram) << test_case.device;
    EXPECT_EQ(countOf(resources, "dsp"), test_case.dsp) << test_case.device;
    EXPECT_EQ(countOf(resources, "io"), test_case.io) << test_case.device;
  }
}

TEST(QLDeviceManager, DeriveResourceCountsRejectsIncompleteConfig) {
  // a package predating the CRR keys: DEVICE_SIZE alone cannot answer, and a
  // wrong count is worse than none
  const json config = json::parse(R"({"DEVICE_SIZE": "8x6"})");
  std::string error;

  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(config, 12, 10, &error).empty());
  EXPECT_NE(error.find("BRAM_SIZE"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsRejectsMismatchedLayout) {
  // a layout DEVICE_SIZE does not describe - a legacy multi-layout package -
  // where only resources.json can answer
  const json config = json::parse(
      R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
          "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": "20"})");
  std::string error;

  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(config, 99, 99, &error).empty());
  EXPECT_NE(error.find("does not describe"), std::string::npos) << error;

  // the matching layout still derives
  EXPECT_FALSE(QLDeviceManager::deriveResourceCounts(config, 12, 10).empty());
}

// ---- the per-layout DEVICE_TYPE_SETTINGS.CUSTOM spelling -------------------
//
// config.json spells the layout geometry flat ("DEVICE_SIZE": "8x6") and/or under
// DEVICE_TYPE_SETTINGS.CUSTOM ("ARRAY_X": "8", "ARRAY_Y": "6"). A package shipping
// one config per layout carries only the one that describes its own layout, so
// which spelling answers is decided by matching the layout's size, not by
// precedence. RESOURCES sits in the same block and is deliberately NOT read: it is
// the resource REQUEST add_layout.py multiplies by MARGIN to size the array, not
// the array's capacity.

namespace {

// EVAL-CCFF-CUSTOM's shape: an 8x6 array with one bram and one dsp column.
const char* const CUSTOM_SPELLING = R"({
  "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3", "IO_CAPACITY": "20",
  "DEVICE_TYPE": "CUSTOM",
  "DEVICE_TYPE_SETTINGS": {
    "LAYOUT_MODE": "AUTO",
    "MARGIN": 1.2,
    "CUSTOM": {
      "ARRAY_X": "8", "ARRAY_Y": "6", "BRAM_COLS": "3", "DSP_COLS": "6"
    },
    "RESOURCES": { "clb": 100, "bram": 6, "dsp": 1, "io": 1281 }
  }
})";

}  // namespace

TEST(QLDeviceManager, DeriveResourceCountsReadsTheCustomSpelling) {
  std::string error;
  const auto resources =
      QLDeviceManager::deriveResourceCounts(json::parse(CUSTOM_SPELLING), 12, 10, &error);

  ASSERT_FALSE(resources.empty()) << error;
  EXPECT_EQ(countOf(resources, "clb"), 36);   // (8 - 1 - 1) * 6
  EXPECT_EQ(countOf(resources, "bram"), 1);   // 1 column * (6 / 6)
  EXPECT_EQ(countOf(resources, "dsp"), 2);    // 1 column * (6 / 3)
  EXPECT_EQ(countOf(resources, "io"), 560);   // 2 * (8 + 6) * 20
}

TEST(QLDeviceManager, DeriveResourceCountsIgnoresTheResourcesRequest) {
  // RESOURCES says clb 100 / bram 6 / dsp 1 / io 1281. Those are what a design
  // asks for, and add_layout.py sizes the array from them; reading them as the
  // array's capacity would report a number roughly MARGIN-scaled and wrong.
  const auto resources =
      QLDeviceManager::deriveResourceCounts(json::parse(CUSTOM_SPELLING), 12, 10);

  ASSERT_FALSE(resources.empty());
  EXPECT_NE(countOf(resources, "clb"), 100);
  EXPECT_NE(countOf(resources, "io"), 1281);
}

TEST(QLDeviceManager, DeriveResourceCountsPicksTheSpellingMatchingTheLayout) {
  // both spellings present and disagreeing - a multi-layout package, or a
  // DEVICE_SIZE left over from the wrong device. Neither wins by precedence: the
  // one describing the layout asked about does.
  const char* const both = R"({
    "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3", "IO_CAPACITY": "20",
    "DEVICE_SIZE": "30x30", "BRAM_COLS": "12,25", "DSP_COLS": "6,19",
    "DEVICE_TYPE_SETTINGS": {
      "CUSTOM": { "ARRAY_X": "8", "ARRAY_Y": "6", "BRAM_COLS": "3", "DSP_COLS": "6" }
    }
  })";
  const json config = json::parse(both);

  // 12x10 is the 8x6 array plus its rings -> the CUSTOM block answers
  const auto small = QLDeviceManager::deriveResourceCounts(config, 12, 10);
  ASSERT_FALSE(small.empty());
  EXPECT_EQ(countOf(small, "clb"), 36);

  // 34x34 is the 30x30 array plus its rings -> DEVICE_SIZE answers
  const auto large = QLDeviceManager::deriveResourceCounts(config, 34, 34);
  ASSERT_FALSE(large.empty());
  EXPECT_EQ(countOf(large, "clb"), 780);

  // and a layout neither describes is declined, naming both
  std::string error;
  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(config, 99, 99, &error).empty());
  EXPECT_NE(error.find("DEVICE_SIZE"), std::string::npos) << error;
  EXPECT_NE(error.find("CUSTOM"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsFallsBackToTheFlatColumnLists) {
  // add_layout.py lets each CUSTOM key fall back to the flat one it does not
  // restate, so a CUSTOM block giving only the array size still gets the columns
  const char* const partial = R"({
    "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3", "IO_CAPACITY": "20",
    "BRAM_COLS": "3", "DSP_COLS": "6",
    "DEVICE_TYPE_SETTINGS": { "CUSTOM": { "ARRAY_X": "8", "ARRAY_Y": "6" } }
  })";
  const auto resources =
      QLDeviceManager::deriveResourceCounts(json::parse(partial), 12, 10);

  ASSERT_FALSE(resources.empty());
  EXPECT_EQ(countOf(resources, "clb"), 36);
  EXPECT_EQ(countOf(resources, "bram"), 1);
}

TEST(QLDeviceManager, DeriveResourceCountsNeedsSomeLayoutGeometry) {
  // the block heights and IO_CAPACITY alone cannot size a layout
  const char* const no_geometry =
      R"({"BRAM_SIZE": "1x6", "DSP_SIZE": "1x3", "IO_CAPACITY": "20"})";
  std::string error;

  EXPECT_TRUE(
      QLDeviceManager::deriveResourceCounts(json::parse(no_geometry), 12, 10, &error).empty());
  EXPECT_NE(error.find("DEVICE_SIZE"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsRequiresIOCapacity) {
  // it used to default to 20 per tile when absent, which silently halved or
  // doubled the io count for any device not built that way
  const char* const without =
      R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
          "BRAM_COLS": "3", "DSP_COLS": "6"})";
  std::string error;

  EXPECT_TRUE(
      QLDeviceManager::deriveResourceCounts(json::parse(without), 12, 10, &error).empty());
  EXPECT_NE(error.find("IO_CAPACITY"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsHonoursIOCapacity) {
  // both spellings of the key are honoured
  const char* const as_number =
      R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
          "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": 10})";
  const char* const as_string =
      R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
          "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": "10"})";
  EXPECT_EQ(countOf(QLDeviceManager::deriveResourceCounts(json::parse(as_number), 12, 10), "io"),
            280);
  EXPECT_EQ(countOf(QLDeviceManager::deriveResourceCounts(json::parse(as_string), 12, 10), "io"),
            280);
}
