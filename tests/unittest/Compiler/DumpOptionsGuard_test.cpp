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
#include <string>
#include <vector>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "Compiler/DumpOptions.h"
#include "NewProject/ProjectManager/project.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "Utils/FileUtils.h"
#include "gtest/gtest.h"

using namespace FOEDAG;
namespace fs = std::filesystem;
using Tokens = std::vector<std::string>;

namespace {

class GuardCompiler : public CompilerOpenFPGA_ql {
 public:
  GuardCompiler() { m_projManager = new ProjectManager(); }
  void ErrorMessage(const std::string& message, bool = true) const override {
    errors.push_back(message);
  }
  int run(const std::string& command) { return ExecuteAndMonitorSystemCommand(command); }
  // Running a command needs a full session; the decision to run it does not.
  bool refuses(const std::string& command) { return RefuseDumpingCommand(command); }
  bool encrypted = false;
  mutable std::vector<std::string> errors;

 protected:
  bool DeviceIsEncrypted() override { return encrypted; }
};

// CompilerOpenFPGA_ql connects to Project::Instance() without a context object
// and never disconnects, so the instance has to outlive every later test.
GuardCompiler& guardCompiler() {
  static GuardCompiler* compiler = new GuardCompiler;
  return *compiler;
}

class DumpOptionsGuardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    m_root = fs::temp_directory_path() / ("dump_options_guard_test_" + name);
    fs::remove_all(m_root);
    m_project = m_root / "proj";
    fs::create_directories(m_project);
    fs::create_directories(m_root / "bin");
    m_previousProjectPath = Project::Instance()->projectPath();
    Project::Instance()->setProjectPath(QString::fromStdString(m_project.string()));
    m_ran = m_root / "ran";
    m_vpr = fakeTool("vpr");
    m_openfpga = fakeTool("openfpga");
    compiler().encrypted = false;
    compiler().errors.clear();
  }
  void TearDown() override {
    Project::Instance()->setProjectPath(m_previousProjectPath);
    fs::remove_all(m_root);
  }

  GuardCompiler& compiler() { return guardCompiler(); }

  // A stand-in tool that only leaves a marker, so a test can tell it started.
  fs::path fakeTool(const std::string& name) {
    const fs::path tool = m_root / "bin" / name;
    FileUtils::WriteToFile(tool, "#!/bin/sh\ntouch " + m_ran.string() + "\n");
    fs::permissions(tool, fs::perms::owner_all);
    return tool;
  }

  int run(const std::string& command) { return compiler().run(command); }
  bool refuses(const std::string& command) { return compiler().refuses(command); }

  fs::path m_root;
  fs::path m_project;
  fs::path m_ran;
  fs::path m_vpr;
  fs::path m_openfpga;
  QString m_previousProjectPath;
};

}  // namespace

namespace {

const std::string kAuthoringPlace =
    "/opt/bin/vpr arch.xml top.blif --net_file top.net --place --echo_file on "
    "--write_flat_place top_rpm_author.fplace --flat_place_verbosity 2";

Tokens refused(const std::string& command, bool authoring = false,
               const fs::path& workDir = {}) {
  return findRefusedDumpOptions(command, workDir, authoring);
}

}  // namespace

TEST(RefusedDumpOptions, VprArgumentsAreScanned) {
  EXPECT_EQ(refused("/opt/bin/vpr a.xml b.blif --write_rr_graph rr.bin"),
            Tokens{"--write_rr_graph"});
  EXPECT_EQ(refused("vpr.exe a.xml b.blif --echo_file=on"), Tokens{"--echo_file on"});
}

TEST(RefusedDumpOptions, OtherProgramsAreNotScanned) {
  EXPECT_TRUE(refused("python3 x.py --write_rr_graph rr.bin").empty());
  EXPECT_TRUE(refused("/opt/bin/vpr_helper --echo_file on").empty());
  EXPECT_TRUE(refused("").empty());
}

TEST(RefusedDumpOptions, AuthoringPlaceKeepsItsEcho) {
  EXPECT_TRUE(refused(kAuthoringPlace, true).empty());
  EXPECT_TRUE(refused("vpr a.xml b.blif --analytical_place --echo_file=on", true).empty());
}

TEST(RefusedDumpOptions, AuthoringPlaceRefusesEveryOtherDumpOption) {
  EXPECT_EQ(refused(kAuthoringPlace + " --write_rr_graph rr.bin", true),
            Tokens{"--write_rr_graph"});
  EXPECT_EQ(refused("vpr a.xml b.blif --place --echo_file 1", true), Tokens{"--echo_file 1"});
}

TEST(RefusedDumpOptions, EchoIsRefusedOutsideTheAuthoringPlace) {
  // Authoring not active.
  EXPECT_EQ(refused(kAuthoringPlace, false), Tokens{"--echo_file on"});
  // Authoring active, but not a place-only run.
  for (const std::string stage : {"--pack", "--route", "--analysis"}) {
    EXPECT_EQ(refused(kAuthoringPlace + " " + stage, true), Tokens{"--echo_file on"}) << stage;
  }
  EXPECT_EQ(refused("vpr a.xml b.blif --echo_file on", true), Tokens{"--echo_file on"});
}

TEST(RefusedDumpOptions, OpenFpgaScriptVprLinesAreScanned) {
  const fs::path dir = fs::temp_directory_path() / "refused_dump_options_script";
  fs::remove_all(dir);
  fs::create_directories(dir);
  FileUtils::WriteToFile(dir / "x.openfpga",
                         "# vpr --write_rr_graph in a comment\n"
                         "read_openfpga_arch -f arch.xml --echo_file on\n"
                         "  vpr arch.xml design.blif --echo_file on --route\n"
                         "vpr arch.xml design.blif --write_router_lookahead=l.bin\n");
  const Tokens expected{"--echo_file on", "--write_router_lookahead"};
  EXPECT_EQ(refused("openfpga -batch -f x.openfpga", false, dir), expected);
  EXPECT_EQ(refused("openfpga --file " + (dir / "x.openfpga").string()), expected);
  EXPECT_EQ(refused("openfpga --file=x.openfpga", false, dir), expected);
  EXPECT_TRUE(refused("openfpga -batch -f missing.openfpga", false, dir).empty());
  EXPECT_TRUE(refused("openfpga -batch", false, dir).empty());
  fs::remove_all(dir);
}

TEST(RefusedDumpOptions, OpenFpgaScriptEchoIsRefusedEvenWhileAuthoring) {
  const fs::path dir = fs::temp_directory_path() / "refused_dump_options_script_authoring";
  fs::remove_all(dir);
  fs::create_directories(dir);
  FileUtils::WriteToFile(dir / "x.openfpga", "vpr arch.xml top.blif --place --echo_file on\n");
  EXPECT_EQ(refused("openfpga -batch -f x.openfpga", true, dir), Tokens{"--echo_file on"});
  fs::remove_all(dir);
}

TEST_F(DumpOptionsGuardTest, EncryptedDeviceRefusesVprDumpOptionWithoutStartingIt) {
  compiler().encrypted = true;
  const int status = run(m_vpr.string() + " a.xml b.blif --write_rr_graph rr.bin --route");
  EXPECT_NE(status, 0);
  EXPECT_FALSE(fs::exists(m_ran));
  ASSERT_EQ(compiler().errors.size(), 1u);
  EXPECT_NE(compiler().errors.front().find("--write_rr_graph"), std::string::npos);
}

TEST_F(DumpOptionsGuardTest, UnencryptedDeviceAllowsVprDumpOption) {
  EXPECT_FALSE(refuses(m_vpr.string() + " a.xml b.blif --write_rr_graph rr.bin --route"));
  EXPECT_TRUE(compiler().errors.empty());
}

TEST_F(DumpOptionsGuardTest, EncryptedDeviceAllowsVprWithoutDumpOptions) {
  compiler().encrypted = true;
  EXPECT_FALSE(refuses(m_vpr.string() + " a.xml b.blif --echo_file off --route"));
  EXPECT_TRUE(compiler().errors.empty());
}

TEST_F(DumpOptionsGuardTest, EncryptedDeviceRefusesNonAuthoringPlaceEcho) {
  // No RPM-IP authoring project is open, so the place run gets no exemption.
  compiler().encrypted = true;
  EXPECT_TRUE(refuses(m_vpr.string() + " a.xml b.blif --place --echo_file on"));
  ASSERT_EQ(compiler().errors.size(), 1u);
  EXPECT_NE(compiler().errors.front().find("--echo_file on"), std::string::npos);
}

TEST_F(DumpOptionsGuardTest, EncryptedDeviceRefusesOpenFpgaScriptWithDumpOption) {
  compiler().encrypted = true;
  FileUtils::WriteToFile(m_project / "top.openfpga",
                         "vpr arch.xml top.blif --echo_file on --analysis\nexit\n");
  const int status = run(m_openfpga.string() + " -batch -f top.openfpga");
  EXPECT_NE(status, 0);
  EXPECT_FALSE(fs::exists(m_ran));
  ASSERT_EQ(compiler().errors.size(), 1u);
  EXPECT_NE(compiler().errors.front().find("--echo_file on"), std::string::npos);
}

TEST_F(DumpOptionsGuardTest, UnencryptedDeviceAllowsOpenFpgaScriptWithDumpOption) {
  FileUtils::WriteToFile(m_project / "top.openfpga",
                         "vpr arch.xml top.blif --echo_file on --analysis\nexit\n");
  EXPECT_FALSE(refuses(m_openfpga.string() + " -batch -f top.openfpga"));
}

TEST_F(DumpOptionsGuardTest, EncryptedDeviceAllowsCleanOpenFpgaScript) {
  compiler().encrypted = true;
  FileUtils::WriteToFile(m_project / "top.openfpga",
                         "vpr arch.xml top.blif --echo_file off --analysis\nexit\n");
  EXPECT_FALSE(refuses(m_openfpga.string() + " -batch -f top.openfpga"));
}
