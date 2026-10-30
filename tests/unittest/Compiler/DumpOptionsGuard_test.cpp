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

namespace {

// A scratch directory per test, removed on scope exit.
class ScratchDir {
 public:
  explicit ScratchDir(const std::string& name)
      : m_path(fs::temp_directory_path() / ("refused_dump_options_" + name)) {
    fs::remove_all(m_path);
    fs::create_directories(m_path);
  }
  ~ScratchDir() { fs::remove_all(m_path); }
  const fs::path& path() const { return m_path; }
  fs::path write(const std::string& name, const std::string& content) const {
    FileUtils::WriteToFile(m_path / name, content);
    return m_path / name;
  }

 private:
  fs::path m_path;
};

// The shape every shipped device template uses (ql-device-data EVAL-MSC-CUSTOM),
// after FinishOpenFPGAScript substituted the VPR options.
std::string deviceTemplate(const std::string& standardOpts, const std::string& vprOpts) {
  return "# Run VPR for the design\n"
         "vpr arch.xml top.blif \\\n"
         "    --device FPGA0808 \\\n"
         "    " + standardOpts + " \\\n"
         "    " + vprOpts + "\n"
         "\n"
         "read_openfpga_arch -f openfpga.xml\n"
         "exit\n";
}

}  // namespace

TEST(RefusedDumpOptions, ProgramNamesAreCaseInsensitive) {
  EXPECT_EQ(refused("C:/aurora/bin/VPR.EXE a.xml b.blif --write_rr_graph rr.bin"),
            Tokens{"--write_rr_graph"});
  const ScratchDir dir("case");
  dir.write("x.openfpga", "vpr a.xml b.blif --echo_file on\n");
  EXPECT_EQ(refused("C:/aurora/bin/OpenFPGA.exe -batch -f x.openfpga", false, dir.path()),
            Tokens{"--echo_file on"});
}

TEST(RefusedDumpOptions, ScriptCommandsAreJoinedLikeTheOpenFpgaShell) {
  EXPECT_EQ(openFpgaScriptCommands("# header\n"
                                   "vpr a.xml \\\n"
                                   "  --device X \\   \n"
                                   "\n"
                                   "# a comment inside the command\n"
                                   "  --route # trailing comment\n"
                                   "   exit  \n"
                                   "vpr b.xml\\\n"
                                   "--pack\n"),
            (Tokens{"vpr a.xml   --device X   --route", "exit", "vpr b.xml--pack"}));
  EXPECT_TRUE(openFpgaScriptCommands("").empty());
  EXPECT_TRUE(openFpgaScriptCommands("# only\n   \n").empty());
}

TEST(RefusedDumpOptions, DumpOptionInSubstitutedTemplateOptionsIsFound) {
  const ScratchDir dir("template_substituted");
  dir.write("top.openfpga",
            deviceTemplate("--clock_modeling ideal --write_rr_graph rr.xml --sdc_file top.sdc",
                           "--route_chan_width 160"));
  EXPECT_EQ(refused("openfpga -batch -f top.openfpga", false, dir.path()),
            Tokens{"--write_rr_graph"});
}

TEST(RefusedDumpOptions, DumpOptionOnItsOwnContinuationLineIsFound) {
  const ScratchDir dir("template_continuation");
  dir.write("top.openfpga",
            "vpr arch.xml top.blif \\\n"
            "    --device FPGA0808 \\\n"
            "    --echo_file on \\\n"
            "    --write_router_lookahead=l.bin\n");
  EXPECT_EQ(refused("openfpga -batch -f top.openfpga", false, dir.path()),
            (Tokens{"--echo_file on", "--write_router_lookahead"}));
}

TEST(RefusedDumpOptions, CleanTemplateAndCommentedDumpOptionsFindNothing) {
  const ScratchDir dir("template_clean");
  dir.write("top.openfpga",
            "# vpr arch.xml top.blif --write_rr_graph rr.xml\n" +
                deviceTemplate("--clock_modeling ideal # --write_rr_graph rr.xml",
                               "--echo_file off"));
  EXPECT_TRUE(refused("openfpga -batch -f top.openfpga", false, dir.path()).empty());
}

TEST(RefusedDumpOptions, InlineSourceStreamIsScanned) {
  const ScratchDir dir("source_inline");
  dir.write("top.openfpga",
            "source --command_stream \"read_openfpga_arch -f a.xml;"
            "vpr arch.xml top.blif --write_rr_graph rr.xml ;exit\"\n");
  EXPECT_EQ(refused("openfpga -batch -f top.openfpga", false, dir.path()),
            Tokens{"--write_rr_graph"});
}

TEST(RefusedDumpOptions, SourcedFileIsScannedRelativeToTheWorkDir) {
  const ScratchDir dir("source_file");
  fs::create_directories(dir.path() / "sub");
  dir.write("sub/vpr.openfpga", "vpr arch.xml top.blif \\\n    --echo_file on\n");
  dir.write("top.openfpga", "source --command_stream sub/vpr.openfpga --from_file\nexit\n");
  EXPECT_EQ(refused("openfpga -batch -f top.openfpga", false, dir.path()),
            Tokens{"--echo_file on"});
  // Without --from_file the stream is a command, not a file name.
  dir.write("top.openfpga", "source --command_stream sub/vpr.openfpga\n");
  EXPECT_TRUE(refused("openfpga -batch -f top.openfpga", false, dir.path()).empty());
}

TEST(RefusedDumpOptions, SourceCycleIsScannedOnce) {
  const ScratchDir dir("source_cycle");
  dir.write("a.openfpga", "source --command_stream b.openfpga --from_file\n");
  dir.write("b.openfpga",
            "vpr arch.xml top.blif --write_placement_delay_lookup d.bin\n"
            "source --command_stream " + (dir.path() / "a.openfpga").string() + " --from_file\n");
  EXPECT_EQ(refused("openfpga -batch -f a.openfpga", false, dir.path()),
            Tokens{"--write_placement_delay_lookup"});
}

TEST(RefusedDumpOptions, SourceChainTooDeepIsRefused) {
  const ScratchDir dir("source_deep");
  constexpr int kFiles = 20;
  for (int i = 0; i < kFiles; ++i) {
    dir.write(std::to_string(i) + ".openfpga",
              "source --command_stream " + std::to_string(i + 1) + ".openfpga --from_file\n");
  }
  const Tokens found = refused("openfpga -batch -f 0.openfpga", false, dir.path());
  ASSERT_EQ(found.size(), 1u);
  EXPECT_NE(found.front().find("source nested deeper"), std::string::npos);
}

namespace {

// DeviceIsEncrypted() as shipped; only the installed-arch lookup is replaced.
class ArchCompiler : public CompilerOpenFPGA_ql {
 public:
  ArchCompiler() { m_projManager = new ProjectManager(); }
  bool encrypted(bool autoMode, bool customMode, const fs::path& arch) {
    m_autoLayoutGenerationMode = autoMode;
    m_customLayoutGenerationMode = customMode;
    m_arch = arch;
    const bool result = DeviceIsEncrypted();
    m_autoLayoutGenerationMode = m_customLayoutGenerationMode = false;
    return result;
  }

 protected:
  std::filesystem::path InstalledVPRArchitectureFile() override { return m_arch; }

 private:
  fs::path m_arch;
};

ArchCompiler& archCompiler() {
  static ArchCompiler* compiler = new ArchCompiler;  // see guardCompiler()
  return *compiler;
}

}  // namespace

TEST(DeviceIsEncrypted, GeneratedLayoutModesAreEncrypted) {
  const ScratchDir dir("arch_modes");
  const fs::path plain = dir.write("vpr.xml", "<architecture/>\n");
  EXPECT_TRUE(archCompiler().encrypted(true, false, plain));
  EXPECT_TRUE(archCompiler().encrypted(false, true, plain));
}

TEST(DeviceIsEncrypted, MissingArchitectureFailsClosed) {
  EXPECT_TRUE(archCompiler().encrypted(false, false, {}));
}

TEST(DeviceIsEncrypted, EncryptedArchitectureIsEncrypted) {
  const ScratchDir dir("arch_encrypted");
  EXPECT_TRUE(archCompiler().encrypted(false, false, dir.write("vpr.xml.en", "anything")));
  EXPECT_TRUE(archCompiler().encrypted(false, false, dir.write("vpr.xml", "QLEN\x01\x02")));
}

TEST(DeviceIsEncrypted, PlainArchitectureIsNotEncrypted) {
  const ScratchDir dir("arch_plain");
  EXPECT_FALSE(archCompiler().encrypted(false, false, dir.write("vpr.xml", "<architecture/>\n")));
}
