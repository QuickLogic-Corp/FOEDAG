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

#include <optional>

#include "Compiler/QLDeviceLayoutInfo.h"
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

// ---- derived counts vs. the shipped resources.json ------------------------
//
// The counts derived from a resolved layout must equal the ones vpr wrote into
// the resources.json these packages shipped before it was retired. Both are
// inlined rather than read off disk, so the check needs no device package and
// cannot go stale against a moving checkout.
//
// The geometry itself is resolved via QLDeviceLayoutInfo::parseDeviceConfig() -
// already covered by QLDeviceLayoutInfo_test.cpp - so these cases only need to
// carry a resolvable config.json; what they check is the arithmetic and the
// failure modes deriveResourceCounts() itself owns.
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

int countOf(const std::vector<std::tuple<std::string, std::optional<int>>>& resources,
            const std::string& name) {
  for (const auto& [resource_name, resource_count] : resources) {
    if (resource_name == name) return resource_count.value_or(-1);
  }
  return -1;
}

// Resolve config.json the same way the flow does, then wrap the result the way
// QLDeviceLayoutInfo's own tests do - the constructor exists for exactly this.
QLDeviceLayoutInfo resolve(const char* config_json_text, const std::string& layout_mode = "") {
  QLDeviceLayout layout;
  QLDeviceLayoutInfo::parseDeviceConfig(json::parse(config_json_text), layout_mode, layout);
  return QLDeviceLayoutInfo(layout);
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
    const QLDeviceLayoutInfo layout_info = resolve(test_case.config_json);
    ASSERT_TRUE(layout_info.resolved()) << test_case.device;
    ASSERT_EQ(layout_info.width(), test_case.layout_width) << test_case.device;
    ASSERT_EQ(layout_info.height(), test_case.layout_height) << test_case.device;

    std::string error;
    const auto resources = QLDeviceManager::deriveResourceCounts(layout_info, &error);

    ASSERT_FALSE(resources.empty()) << test_case.device << ": " << error;
    EXPECT_EQ(countOf(resources, "clb"), test_case.clb) << test_case.device;
    EXPECT_EQ(countOf(resources, "bram"), test_case.bram) << test_case.device;
    EXPECT_EQ(countOf(resources, "dsp"), test_case.dsp) << test_case.device;
    EXPECT_EQ(countOf(resources, "io"), test_case.io) << test_case.device;
  }
}

TEST(QLDeviceManager, DeriveResourceCountsRejectsUnresolvedLayout) {
  // an unresolved layout reaching deriveResourceCounts() is always a genuine
  // problem by the time it gets here - the expected "not yet known" case
  // (AUTO/RESOURCES before Packing()) is filtered out earlier, by
  // deriveDeviceResourceInformation().
  std::string error;
  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(QLDeviceLayoutInfo(QLDeviceLayout{}), &error)
                  .empty());
  EXPECT_NE(error.find("not resolved"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsRejectsIncompleteConfig) {
  // a package predating the CRR keys: DEVICE_SIZE alone resolves the layout,
  // but BRAM_SIZE is still absent, and a wrong count is worse than none
  const QLDeviceLayoutInfo layout_info = resolve(R"({"DEVICE_SIZE": "8x6"})");
  ASSERT_TRUE(layout_info.resolved());

  std::string error;
  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(layout_info, &error).empty());
  EXPECT_NE(error.find("BRAM_SIZE"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsRefusesABlockWiderThanOneTile) {
  // the clb count subtracts one array column per BRAM_COLS/DSP_COLS entry, so a
  // 2xN block would leave it too high. No package ships one; decline rather than
  // report a number computed on an assumption the package contradicts.
  const char* const wide_bram = R"({
    "DEVICE_SIZE": "8x6", "BRAM_SIZE": "2x6", "DSP_SIZE": "1x3",
    "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": "20"
  })";
  std::string error;
  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(resolve(wide_bram), &error).empty());
  EXPECT_NE(error.find("BRAM_SIZE"), std::string::npos) << error;
  EXPECT_NE(error.find("tiles wide"), std::string::npos) << error;

  const char* const wide_dsp = R"({
    "DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "3x3",
    "BRAM_COLS": "3", "DSP_COLS": "6", "IO_CAPACITY": "20"
  })";
  error.clear();
  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(resolve(wide_dsp), &error).empty());
  EXPECT_NE(error.find("DSP_SIZE"), std::string::npos) << error;
}

TEST(QLDeviceManager, DeriveResourceCountsRequiresIOCapacity) {
  // it used to default to 20 per tile when absent, which silently halved or
  // doubled the io count for any device not built that way
  const char* const without =
      R"({"DEVICE_SIZE": "8x6", "BRAM_SIZE": "1x6", "DSP_SIZE": "1x3",
          "BRAM_COLS": "3", "DSP_COLS": "6"})";
  std::string error;

  EXPECT_TRUE(QLDeviceManager::deriveResourceCounts(resolve(without), &error).empty());
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
  EXPECT_EQ(countOf(QLDeviceManager::deriveResourceCounts(resolve(as_number)), "io"), 280);
  EXPECT_EQ(countOf(QLDeviceManager::deriveResourceCounts(resolve(as_string)), "io"), 280);
}
