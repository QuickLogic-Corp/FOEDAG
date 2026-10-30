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

#include "Compiler/PackLayout.h"

#include "Compiler/FlowEngineClient.h"
#include "Utils/FileUtils.h"
#include "gtest/gtest.h"

#include <filesystem>

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

class PackLayoutTest : public ::testing::Test {
protected:
  void SetUp() override {
    m_project = fs::temp_directory_path() /
                ("pack_layout_test_" +
                 std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::remove_all(m_project);
    fs::create_directories(m_project);
  }
  void TearDown() override { fs::remove_all(m_project); }

  PackLayout sample() const {
    PackLayout layout;
    layout.autoMode = true;
    layout.layoutName = "AUTO_10x12";
    layout.vprXml = "vpr_generated.xml.en";
    layout.sbMaps = "sb_maps_generated.yml.en";
    layout.files["vpr_generated.xml.en"] = {"sha256:" + std::string(64, 'a'), ""};
    return layout;
  }

  fs::path m_project;
};

TEST_F(PackLayoutTest, round_trips_through_the_project) {
  PackLayout layout = sample();
  ASSERT_TRUE(layout.write(m_project));
  const std::optional<PackLayout> back = PackLayout::read(m_project);
  ASSERT_TRUE(back);
  EXPECT_TRUE(back->autoMode);
  EXPECT_FALSE(back->customMode);
  EXPECT_EQ(back->layoutName, "AUTO_10x12");
  EXPECT_EQ(back->vprXml, fs::path{"vpr_generated.xml.en"});
  EXPECT_EQ(back->sbMaps, fs::path{"sb_maps_generated.yml.en"});
  EXPECT_EQ(back->files.at("vpr_generated.xml.en").plaintext, "sha256:" + std::string(64, 'a'));
  EXPECT_EQ(back->toJson(), layout.toJson());
}

// A new session removed device_layout.json; a skipped pack puts back the same
// bytes the pack that ran left.
TEST_F(PackLayoutTest, a_skipped_pack_restores_device_layout_json) {
  const std::string written = "{\n  \"width\": 12,\n  \"layout_name\": \"AUTO_12x12\"\n}";
  PackLayout layout = sample();
  layout.deviceLayout = written;
  ASSERT_TRUE(layout.write(m_project));

  const fs::path file = m_project / "device_layout.json";
  ASSERT_TRUE(FileUtils::WriteToFile(file, written));  // as pack left it
  const std::string packWrote = FileUtils::GetFileContent(file);
  fs::remove(file);                                    // session start

  const std::optional<PackLayout> back = PackLayout::read(m_project);
  ASSERT_TRUE(back);
  EXPECT_TRUE(back->restoreDeviceLayout(m_project));
  ASSERT_TRUE(fs::exists(file));
  EXPECT_EQ(FileUtils::GetFileContent(file), packWrote);

  PackLayout none = sample();  // a pack_layout.json from before the field
  EXPECT_FALSE(none.restoreDeviceLayout(m_project));
}

TEST_F(PackLayoutTest, anything_unusable_reads_as_none) {
  EXPECT_FALSE(PackLayout::read(m_project));  // no file
  FileUtils::WriteToFile(m_project / PackLayout::FILE_NAME, "not json");
  EXPECT_FALSE(PackLayout::read(m_project));
  nlohmann::json json = sample().toJson();
  json["v"] = 2;
  EXPECT_FALSE(PackLayout::fromJson(json));
  json = sample().toJson();
  json["vpr_xml"] = (m_project / "vpr_generated.xml.en").string();  // never absolute
  EXPECT_FALSE(PackLayout::fromJson(json));
  json = sample().toJson();
  json["mode"] = "fixed";
  EXPECT_FALSE(PackLayout::fromJson(json));
  json = sample().toJson();
  json.erase("files");
  EXPECT_FALSE(PackLayout::fromJson(json));
}

// The plaintext digest stands for the encrypted file only while that file is
// the one pack wrote.
TEST_F(PackLayoutTest, host_digests_only_for_files_as_pack_wrote_them) {
  const fs::path xml = m_project / "vpr_generated.xml.en";
  FileUtils::WriteToFile(xml, "cipher one");
  PackLayout layout = sample();
  layout.files["vpr_generated.xml.en"].written = FlowEngineClient::sha256File(xml);
  layout.files["sb_maps_generated.yml.en"] = {"sha256:" + std::string(64, 'c'), "sha256:x"};

  auto digests = layout.outputHostDigests(m_project);
  EXPECT_EQ(digests.size(), 1u);
  EXPECT_EQ(digests.at("vpr_generated.xml.en"), "sha256:" + std::string(64, 'a'));

  FileUtils::WriteToFile(xml, "cipher two");
  EXPECT_TRUE(layout.outputHostDigests(m_project).empty());
  fs::remove(xml);
  EXPECT_TRUE(layout.outputHostDigests(m_project).empty());
}

}  // namespace
