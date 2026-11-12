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

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <qlcrypt/qlcrypt.hpp>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "gtest/gtest.h"

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

const std::string kPlain = "<repack_design_constraints><pin_constraint net=\"clk\"/>";

std::string read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

void write(const fs::path& file, const std::string& text) {
  std::ofstream(file, std::ios::binary) << text;
}

class EncryptedCopyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    m_root = fs::temp_directory_path() /
             ("EncryptedCopy_test_" + std::string(::testing::UnitTest::GetInstance()
                                                      ->current_test_info()
                                                      ->name()));
    fs::remove_all(m_root);
    fs::create_directories(m_root);
    m_keyDb = makeKeyDb("DEV_Supp.db");
    m_plain = m_root / "private.xml";
    m_stored = m_root / "proj_repack_design_constraint.xml.en";
    write(m_plain, kPlain);
  }
  void TearDown() override { fs::remove_all(m_root); }

  fs::path makeKeyDb(const std::string& name) {
    qlcrypt::KeyDB keys;
    EXPECT_TRUE(qlcrypt::ok(qlcrypt::KeyDB::create(keys)));
    const fs::path db = m_root / name;
    EXPECT_TRUE(qlcrypt::ok(keys.save(db.string())));
    return db;
  }

  fs::path m_root, m_keyDb, m_plain, m_stored;
};

// F1: what a later process restores is exactly what floorplan wrote, and the
// project only ever holds ciphertext.
TEST_F(EncryptedCopyTest, round_trip_keeps_only_ciphertext_in_the_project) {
  ASSERT_TRUE(CompilerOpenFPGA_ql::storeEncryptedCopy(m_plain, m_stored, m_keyDb));
  EXPECT_EQ(read(m_stored).find("pin_constraint"), std::string::npos);

  const fs::path restored = m_root / "restored.xml";
  ASSERT_TRUE(CompilerOpenFPGA_ql::restoreEncryptedCopy(m_stored, restored, m_keyDb));
  EXPECT_EQ(read(restored), kPlain);
#ifndef _WIN32
  EXPECT_EQ(fs::status(restored).permissions() & (fs::perms::group_all | fs::perms::others_all),
            fs::perms::none);
#endif
}

// A failed store must not leave an old or partial copy that a later process
// would take for this run's output.
TEST_F(EncryptedCopyTest, failed_store_leaves_no_copy) {
  write(m_stored, "old copy");
  EXPECT_FALSE(CompilerOpenFPGA_ql::storeEncryptedCopy(m_plain, m_stored, m_root / "none.db"));
  EXPECT_FALSE(fs::exists(m_stored));

  write(m_stored, "old copy");
  EXPECT_FALSE(
      CompilerOpenFPGA_ql::storeEncryptedCopy(m_root / "missing.xml", m_stored, m_keyDb));
  EXPECT_FALSE(fs::exists(m_stored));
}

// A copy made with another device's key fails to restore and leaves nothing
// behind for the bitstream lookup to pick up.
TEST_F(EncryptedCopyTest, restore_with_another_key_leaves_nothing) {
  ASSERT_TRUE(CompilerOpenFPGA_ql::storeEncryptedCopy(m_plain, m_stored, m_keyDb));
  const fs::path restored = m_root / "restored.xml";
  write(restored, "stale");
  EXPECT_FALSE(
      CompilerOpenFPGA_ql::restoreEncryptedCopy(m_stored, restored, makeKeyDb("OTHER_Supp.db")));
  EXPECT_FALSE(fs::exists(restored));
  EXPECT_FALSE(
      CompilerOpenFPGA_ql::restoreEncryptedCopy(m_root / "absent.en", restored, m_keyDb));
  EXPECT_FALSE(fs::exists(restored));
}

}  // namespace
