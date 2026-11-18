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
#include <string>
#include <vector>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "gtest/gtest.h"

using namespace FOEDAG;
namespace fs = std::filesystem;

namespace {

using Job = CompilerOpenFPGA_ql::ClockBindingJob;
using Outcome = CompilerOpenFPGA_ql::ClockBindingOutcome;
using Plan = CompilerOpenFPGA_ql::ClockBindingPlan;
using Probe = CompilerOpenFPGA_ql::ClockDriverProbe;

const std::string kDriver = "clock-region-place";

class ClockBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    m_root = fs::temp_directory_path() /
             ("ClockBinding_test_" + std::string(::testing::UnitTest::GetInstance()
                                                     ->current_test_info()
                                                     ->name()));
    fs::remove_all(m_root);
    fs::create_directories(m_root);
    m_result = m_root / "binding_result.json";
  }
  void TearDown() override { fs::remove_all(m_root); }

  void result(const std::string& text) { std::ofstream(m_result, std::ios::binary) << text; }

  // An executable shell script with `body` after the interpreter line.
  fs::path script(const std::string& name, const std::string& body) {
    const fs::path file = m_root / name;
    std::ofstream(file) << "#!/bin/sh\n" << body;
    fs::permissions(file, fs::perms::owner_all);
    return file;
  }

  Job job() const {
    Job job;
    job.workdir = "/p/clock_regions";
    job.netFile = "/p/d_post_synth.net";
    job.placeFile = "/p/d_post_synth.place";
    job.clockPads = "/p/d_clock_pads.json";
    job.regionFile = "/dev/aurora/clock_regions.xml";
    job.repack = "/p/clock_regions/repack_design_constraint.xml";
    job.selectorBits = "/p/clock_regions/clock_selector_bits.xml";
    return job;
  }

  fs::path m_root, m_result;
};

// Never asked when the result file holds a verdict.
Probe notAsked() {
  ADD_FAILURE() << "the driver was probed although the step left a verdict";
  return {};
}

Probe probed(Probe::Verdict verdict, int status = 0, const std::string& firstLine = {}) {
  Probe probe;
  probe.verdict = verdict;
  probe.status = status;
  probe.firstLine = firstLine;
  return probe;
}

// ---- the binding the bitstream stage computes ------------------------------------------

std::filesystem::path noPrivateFile() {
  ADD_FAILURE() << "a private file was asked for a plaintext region file";
  return {};
}

TEST_F(ClockBindingTest, a_device_without_clock_regions_has_no_binding) {
  const Plan plan = CompilerOpenFPGA_ql::clockBindingPlan("/p", "d", {}, {}, false, noPrivateFile);
  EXPECT_FALSE(plan.job);
  EXPECT_TRUE(plan.error.empty());
}

// A device whose config.json names a region file that is not installed, or a config.json
// that does not parse: the stage fails, never runs as a device without regions (whose
// bitstream would keep every selector at its default under a chip-wide template).
TEST_F(ClockBindingTest, a_region_file_that_cannot_be_resolved_fails_the_stage) {
  const std::string lookup =
      "device D: config.json CLOCK_REGIONS names aurora/clock_regions.xml, which is not in "
      "/dev (plaintext or .en)";
  const Plan plan = CompilerOpenFPGA_ql::clockBindingPlan("/p", "d", {}, lookup, false, noPrivateFile);
  EXPECT_FALSE(plan.job);
  EXPECT_EQ(plan.error.find(lookup), 0u) << plan.error;
  EXPECT_NE(plan.error.find("cannot be bound"), std::string::npos) << plan.error;
}

TEST_F(ClockBindingTest, a_plaintext_region_file_binds_into_the_clock_regions_directory) {
  const Plan plan = CompilerOpenFPGA_ql::clockBindingPlan(
      "/p", "d", "/dev/aurora/clock_regions.xml", {}, false, noPrivateFile);
  ASSERT_TRUE(plan.job) << plan.error;
  EXPECT_TRUE(plan.error.empty());
  EXPECT_EQ(plan.job->workdir, fs::path("/p/clock_regions"));
  EXPECT_EQ(plan.job->clockPads, fs::path("/p/d_clock_pads.json"));
  EXPECT_EQ(plan.job->regionFile, fs::path("/dev/aurora/clock_regions.xml"));
  EXPECT_FALSE(plan.job->regionEncrypted);
  EXPECT_EQ(plan.job->repack, fs::path("/p/clock_regions/repack_design_constraint.xml"));
  EXPECT_EQ(plan.job->selectorBits, fs::path("/p/clock_regions/clock_selector_bits.xml"));
}

// Both files locate the selectors: with an encrypted region file they are private.
TEST_F(ClockBindingTest, an_encrypted_region_file_binds_into_private_files) {
  int asked = 0;
  const auto privateFile = [&asked] {
    return fs::path("/tmp/private/file" + std::to_string(++asked));
  };
  const Plan plan = CompilerOpenFPGA_ql::clockBindingPlan(
      "/p", "d", "/dev/aurora/clock_regions.xml.en", {}, true, privateFile);
  ASSERT_TRUE(plan.job) << plan.error;
  EXPECT_TRUE(plan.job->regionEncrypted);
  EXPECT_EQ(plan.job->regionFile, fs::path("/dev/aurora/clock_regions.xml.en"));
  EXPECT_EQ(plan.job->repack, fs::path("/tmp/private/file1"));
  EXPECT_EQ(plan.job->selectorBits, fs::path("/tmp/private/file2"));
  const Plan none = CompilerOpenFPGA_ql::clockBindingPlan(
      "/p", "d", "/dev/aurora/clock_regions.xml.en", {}, true, [] { return fs::path(); });
  EXPECT_FALSE(none.job);
  EXPECT_FALSE(none.error.empty());
}

// ---- the record of the placement and Placement's skip check -----------------------------

// The files of a placement and the record the driver leaves of it, as
// aurora2 scripts/clock_region/binding_step.py writes it.
class ClockBindingRecordTest : public ClockBindingTest {
 protected:
  void SetUp() override {
    ClockBindingTest::SetUp();
    m_job.workdir = m_root / "clock_regions";
    m_job.placeFile = m_root / "d_post_synth.place";
    m_job.netFile = m_root / "d_post_synth.net";
    m_job.clockPads = m_root / "d_clock_pads.json";
    m_job.regionFile = m_root / "clock_regions.xml.en";
    fs::create_directories(m_job.workdir);
    write(m_job.placeFile, "placed\n");
    write(m_job.netFile, "packed\n");
    write(m_job.clockPads, "pads\n");
    write(m_job.regionFile, "regions\n");
    m_record = json::parse(R"({
      "schema": 1,
      "fabric_origin": 1,
      "clock_port": "clk",
      "clock_config_pb_type": "clock_config_phy",
      "inputs": {
        "place_file": "SHA256:bc59dec10c6d3c0f39d0345f8cc0e9dd7771a8c750660cee3250ff996dc5d1bd",
        "net_file": "SHA256:d535a32ae4ccd17fb41897c4ba5077d9e1aaa9ae82e6ad990e2c93b0dd10821d",
        "clock_pads_file": "SHA256:685073ed561f309daefcc88fc6950d368b0378838cb66f6f37c5679dbce08195",
        "region_file": "SHA256:074b0849cfd7dd05a9493a4aca77007bc0250d406ebf80ff45c1bf3b54b5533c"
      }
    })");
    writeRecord(m_record);
  }

  static void write(const fs::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
  }
  void writeRecord(const json& record) const {
    write(m_job.workdir / "clock_binding_state.json", record.dump(1) + "\n");
  }

  Job m_job;
  json m_record;
};

TEST_F(ClockBindingRecordTest, the_record_of_these_files_matches) {
  EXPECT_TRUE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job));
}

// A record of another placement: Placement must run, as the binding step would refuse
// to bind (an earlier Aurora placed the project again and left this record in place).
TEST_F(ClockBindingRecordTest, a_record_of_other_files_does_not_match) {
  for (const fs::path& file : {m_job.placeFile, m_job.netFile, m_job.clockPads, m_job.regionFile}) {
    SetUp();
    write(file, "other\n");
    EXPECT_FALSE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job)) << file;
  }
  SetUp();
  fs::remove(m_job.clockPads);
  EXPECT_FALSE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job));
}

TEST_F(ClockBindingRecordTest, a_record_the_binding_step_cannot_use_does_not_match) {
  std::vector<json> unusable;
  for (const char* change :
       {R"({"schema": 2})", R"({"fabric_origin": "1"})", R"({"fabric_origin": -1})",
        R"({"fabric_origin": true})", R"({"clock_port": ""})",
        R"({"clock_config_pb_type": null})", R"({"inputs": []})"}) {
    json record = m_record;
    record.update(json::parse(change));
    unusable.push_back(record);
  }
  json without = m_record;
  without["inputs"].erase("region_file");
  unusable.push_back(without);
  for (const json& record : unusable) {
    writeRecord(record);
    EXPECT_FALSE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job)) << record.dump();
  }
  write(m_job.workdir / "clock_binding_state.json", "not json");
  EXPECT_FALSE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job));
  fs::remove(m_job.workdir / "clock_binding_state.json");
  EXPECT_FALSE(CompilerOpenFPGA_ql::clockBindingRecordMatches(m_job));
}

// ---- the binding step's command line ----------------------------------------------------

// The command is the contract with the driver's `bind` (aurora2
// scripts/clock_region_place.py): every file it reads and writes, by these options.
TEST_F(ClockBindingTest, command_names_every_file_of_the_binding_step) {
  const std::vector<std::string> expected{
      "/a/bin/clock-region-place", "bind",
      "--workdir", "/p/clock_regions",
      "--net", "/p/d_post_synth.net",
      "--place", "/p/d_post_synth.place",
      "--clock-pads", "/p/d_clock_pads.json",
      "--regions", "/dev/aurora/clock_regions.xml",
      "--regions-installed", "/dev/aurora/clock_regions.xml",
      "--repack-constraints-out", "/p/clock_regions/repack_design_constraint.xml",
      "--selector-bits-out", "/p/clock_regions/clock_selector_bits.xml"};
  EXPECT_EQ(CompilerOpenFPGA_ql::clockBindingCommand("/a/bin/clock-region-place", job(),
                                                     "/dev/aurora/clock_regions.xml"),
            expected);
}

// With an encrypted region file the driver reads the decrypted copy, knows the file by
// its installed `.en` name (as the record does), writes where it is told (the private
// files) and is told to quote nothing of the file.
TEST_F(ClockBindingTest, command_of_an_encrypted_region_file) {
  Job encrypted = job();
  encrypted.regionFile = "/dev/aurora/clock_regions.xml.en";
  encrypted.regionEncrypted = true;
  encrypted.repack = "/tmp/private/a";
  encrypted.selectorBits = "/tmp/private/b";
  const std::vector<std::string> argv = CompilerOpenFPGA_ql::clockBindingCommand(
      "/a/bin/clock-region-place", encrypted, "/tmp/private/regions");
  ASSERT_EQ(argv.size(), 19u);
  EXPECT_EQ(argv.back(), "--regions-encrypted");
  EXPECT_EQ(argv[10], "--regions");
  EXPECT_EQ(argv[11], "/tmp/private/regions");
  EXPECT_EQ(argv[12], "--regions-installed");
  EXPECT_EQ(argv[13], "/dev/aurora/clock_regions.xml.en");
  EXPECT_EQ(argv[15], "/tmp/private/a");
  EXPECT_EQ(argv[17], "/tmp/private/b");
}

// ---- the verdict of the binding step ------------------------------------------------------

TEST_F(ClockBindingTest, bound_when_the_step_says_so_and_exits_zero) {
  result(R"({"schema": 1, "bound": true, "exit": 0, "reason": null, "regions": 4})");
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(m_result, 0, kDriver, notAsked);
  EXPECT_TRUE(outcome.bound);
  EXPECT_TRUE(outcome.reason.empty());
}

// A refusal is the driver's own sentence, which says to run Placement again.
TEST_F(ClockBindingTest, a_refusal_carries_the_drivers_reason) {
  result(R"({"schema": 1, "bound": false, "exit": 2,
             "reason": "the clock pads p.json changed since Placement; run Placement again"})");
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(m_result, 2, kDriver, notAsked);
  EXPECT_FALSE(outcome.bound);
  EXPECT_EQ(outcome.reason, "the clock pads p.json changed since Placement; run Placement again");
}

// A driver older than the binding step exits on its unknown command and leaves no
// result: the message names the cause instead of an exit status.
TEST_F(ClockBindingTest, no_result_from_a_driver_without_the_step_says_so) {
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(
      m_result, 2, kDriver, [] { return probed(Probe::NoBindStep, 2); });
  EXPECT_FALSE(outcome.bound);
  EXPECT_EQ(outcome.reason, CompilerOpenFPGA_ql::clockBindingStepMissing(kDriver));
  EXPECT_NE(outcome.reason.find("older than this Aurora"), std::string::npos);
  EXPECT_NE(outcome.reason.find(kDriver), std::string::npos);
}

// A driver that cannot start at all (a missing library, nowhere to unpack) is not
// called older than this Aurora: the message gives what it said.
TEST_F(ClockBindingTest, no_result_from_a_driver_that_cannot_run_says_why) {
  const std::string loader =
      "clock-region-place: error while loading shared libraries: libz.so.1: cannot open "
      "shared object file: No such file or directory";
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(
      m_result, 127, kDriver, [&loader] { return probed(Probe::CannotRun, 127, loader); });
  EXPECT_FALSE(outcome.bound);
  EXPECT_EQ(outcome.reason, "clock-region-place cannot be run (status 127): " + loader);
  EXPECT_EQ(CompilerOpenFPGA_ql::clockDriverNoVerdict(kDriver, 1, "plain",
                                                      [] { return probed(Probe::CannotRun, -1); }),
            "clock-region-place cannot be run (status -1)");
}

TEST_F(ClockBindingTest, no_result_from_a_driver_with_the_step_gives_the_status) {
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(
      m_result, 3, kDriver, [] { return probed(Probe::Binds); });
  EXPECT_FALSE(outcome.bound);
  EXPECT_EQ(outcome.reason, "clock-region-place bind ended with status 3 and gave no reason");
}

// A step that was stopped or crashed has a negative status: that is no sign of an
// older driver, and the driver is not started again to ask.
TEST_F(ClockBindingTest, a_stopped_step_gives_the_status_without_asking_the_driver) {
  const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(m_result, -1, kDriver, notAsked);
  EXPECT_FALSE(outcome.bound);
  EXPECT_EQ(outcome.reason, "clock-region-place bind ended with status -1 and gave no reason");
}

// Nothing but a verdict of this run passes: not a result that is no JSON object, not
// one that claims success under a failing exit status, not one without a reason.
TEST_F(ClockBindingTest, a_result_that_is_no_verdict_binds_nothing) {
  for (const char* text : {"not json", "[]", R"({"bound": "yes"})", R"({"bound": false})",
                           R"({"bound": false, "reason": 3})"}) {
    result(text);
    const Outcome outcome = CompilerOpenFPGA_ql::clockBindingOutcome(
        m_result, 0, kDriver, [] { return probed(Probe::Binds); });
    EXPECT_FALSE(outcome.bound) << text;
    EXPECT_EQ(outcome.reason, "clock-region-place bind ended with status 0 and gave no reason")
        << text;
  }
  result(R"({"bound": true, "reason": null})");
  const Outcome failed = CompilerOpenFPGA_ql::clockBindingOutcome(
      m_result, 3, kDriver, [] { return probed(Probe::Binds); });
  EXPECT_FALSE(failed.bound);
  EXPECT_EQ(failed.reason, "clock-region-place bind ended with status 3 and gave no reason");
}

#ifndef _WIN32
// ---- the probe: `<driver> bind --help` ---------------------------------------------------

// A driver with the step prints its usage and exits 0; this one does so only when it is
// asked exactly `bind --help`.
TEST_F(ClockBindingTest, the_probe_tells_a_driver_with_the_step) {
  const fs::path driver = script(
      "with_step",
      "if [ $# -eq 2 ] && [ \"$1\" = bind ] && [ \"$2\" = --help ]; then\n"
      "  echo 'usage: clock_region_place.py bind [-h] --workdir WORKDIR'\n  exit 0\nfi\nexit 1\n");
  const Probe probe = CompilerOpenFPGA_ql::probeClockRegionDriver(driver);
  EXPECT_EQ(probe.verdict, Probe::Binds);
  EXPECT_EQ(probe.status, 0);
}

// What the driver before the binding step prints (argparse, on stderr).
TEST_F(ClockBindingTest, the_probe_tells_a_driver_without_the_step) {
  const fs::path driver = script(
      "without_step",
      "echo 'usage: clock_region_place.py [-h] {run,check,plan,extract,bench,aurora,example} "
      "...' >&2\n"
      "echo \"clock_region_place.py: error: argument command: invalid choice: 'bind' (choose "
      "from 'run', 'check', 'plan', 'extract', 'bench', 'aurora', 'example')\" >&2\nexit 2\n");
  const Probe probe = CompilerOpenFPGA_ql::probeClockRegionDriver(driver);
  EXPECT_EQ(probe.verdict, Probe::NoBindStep);
  EXPECT_EQ(probe.status, 2);
}

TEST_F(ClockBindingTest, the_probe_tells_a_driver_that_cannot_run) {
  const std::string loader =
      "clock-region-place: error while loading shared libraries: libz.so.1: cannot open "
      "shared object file: No such file or directory";
  const Probe broken =
      CompilerOpenFPGA_ql::probeClockRegionDriver(script("broken", "echo '" + loader + "' >&2\nexit 127\n"));
  EXPECT_EQ(broken.verdict, Probe::CannotRun);
  EXPECT_EQ(broken.status, 127);
  EXPECT_EQ(broken.firstLine, loader);
  // a usage error that is not about the command is no sign of an older driver
  const Probe other = CompilerOpenFPGA_ql::probeClockRegionDriver(
      script("other_usage_error", "echo 'error: unrecognized arguments: --help' >&2\nexit 2\n"));
  EXPECT_EQ(other.verdict, Probe::CannotRun);
  EXPECT_EQ(other.status, 2);
  const Probe missing = CompilerOpenFPGA_ql::probeClockRegionDriver(m_root / "not_installed");
  EXPECT_EQ(missing.verdict, Probe::CannotRun);
  EXPECT_LT(missing.status, 0);
  EXPECT_FALSE(missing.firstLine.empty());
}
#endif

}  // namespace
