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
#include <utility>
#include <vector>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

using LongestDspChain = CompilerOpenFPGA_ql::LongestDspChain;
using MaxChainLengthDecision = CompilerOpenFPGA_ql::MaxChainLengthDecision;

const std::string synth_log_name = "top_synth.log";

// Excerpts of real synthesis logs and of add_layout.py, and real package config.json files.
std::filesystem::path testDataPath(const std::string& file_name) {
  return std::filesystem::path{FOEDAG_TEST_DATA_DIR} / "unittest" / "Compiler" /
         "DspChainLength" / file_name;
}

LongestDspChain findInText(const std::string& text) {
  std::istringstream synth_log(text);
  return CompilerOpenFPGA_ql::findLongestDspChain(synth_log);
}

LongestDspChain findInFile(const std::string& file_name) {
  std::ifstream synth_log(testDataPath(file_name));
  EXPECT_TRUE(synth_log.is_open()) << file_name;
  return CompilerOpenFPGA_ql::findLongestDspChain(synth_log);
}

bool scriptTextHasOption(const std::string& text) {
  std::istringstream script(text);
  return CompilerOpenFPGA_ql::scriptHasMaxChainLengthOption(script);
}

bool scriptFileHasOption(const std::string& file_name) {
  std::ifstream script(testDataPath(file_name));
  EXPECT_TRUE(script.is_open()) << file_name;
  return CompilerOpenFPGA_ql::scriptHasMaxChainLengthOption(script);
}

LongestDspChain lines(int line_count, int length) {
  LongestDspChain longest_chain;
  longest_chain.line_count = line_count;
  longest_chain.length = length;
  return longest_chain;
}

MaxChainLengthDecision decide(const LongestDspChain& longest_chain, bool device_has_dsp_v4,
                              bool auto_layout_mode, bool script_has_option) {
  return CompilerOpenFPGA_ql::decideMaxChainLength(longest_chain, device_has_dsp_v4,
                                                   auto_layout_mode, script_has_option,
                                                   synth_log_name);
}

const LongestDspChain no_line;

const bool dsp_v4_device = true;
const bool device_without_dsp_v4 = false;

// real config.json files of three device packages.
const std::string dsp_v4_config_file = "config_eval_msc_custom_dsp_v4.json";
const std::string dsp_v1_config_file = "config_idaho_3648_dsp_v1.json";
const std::string no_dsp_key_config_file = "config_no_dsp_key.json";

// synthesis log excerpts: the Yosys path (the line once, deleted, or twice) and the Synplify path.
const std::string yosys_log_file = "yosys_path_excerpt.log.sample";
const std::string yosys_log_line_deleted_file = "yosys_path_excerpt_line_deleted.log.sample";
const std::string yosys_log_line_twice_file = "yosys_path_excerpt_line_twice.log.sample";
const std::string synplify_log_file = "synplify_path_excerpt.log.sample";

// add_layout.py excerpts: the option's argparse lines, and the same lines before the option.
const std::string script_with_option_file = "add_layout_with_option_excerpt.py";
const std::string script_without_option_file = "add_layout_without_option_excerpt.py";

json configFromFile(const std::string& file_name) {
  std::ifstream config(testDataPath(file_name));
  EXPECT_TRUE(config.is_open()) << file_name;
  return json::parse(config);
}

// the four helpers together, as Packing() calls them, on real files.
MaxChainLengthDecision decideFromFiles(const std::string& log_file_name,
                                       const std::string& config_file_name,
                                       bool auto_layout_mode,
                                       const std::string& script_file_name) {
  std::ifstream synth_log(testDataPath(log_file_name));
  std::ifstream add_layout_script(testDataPath(script_file_name));
  return CompilerOpenFPGA_ql::decideMaxChainLength(
      CompilerOpenFPGA_ql::findLongestDspChain(synth_log),
      CompilerOpenFPGA_ql::deviceHasDspV4(configFromFile(config_file_name)), auto_layout_mode,
      CompilerOpenFPGA_ql::scriptHasMaxChainLengthOption(add_layout_script), synth_log_name);
}

const std::string old_line_only =
    "ql_dspv4: cascade: 2 PCIN link(s) in 1 chain(s), longest 5 (limit 64)\n";

}  // namespace

// ---- findLongestDspChain ----

TEST(FindLongestDspChain, ReadsTheLineWhenItAppearsOnce) {
  // a real Yosys-path log: the old line says longest 2 (limit 2) above the new line.
  LongestDspChain found = findInFile(yosys_log_file);
  EXPECT_EQ(found.line_count, 1);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 2);

  // a real Synplify-path log: only the new line.
  found = findInFile(synplify_log_file);
  EXPECT_EQ(found.line_count, 1);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 2);

  // the line among other text, with the old line saying a different number.
  found = findInText("Executing QL_DSP4_CHECK pass.\n" + old_line_only +
                     "ql_dsp4_chain_report: longest DSP chain 3 (2 link(s) in 1 chain(s))\n"
                     "End of script.\n");
  EXPECT_EQ(found.line_count, 1);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 3);

  // a log written with Windows line ends.
  found = findInText("ql_dsp4_chain_report: longest DSP chain 64 (63 link(s) in 1 chain(s))\r\n");
  EXPECT_EQ(found.line_count, 1);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 64);
}

TEST(FindLongestDspChain, ReadsALengthOfZeroAsFoundNotAsMissing) {
  const LongestDspChain found =
      findInText("ql_dsp4_chain_report: longest DSP chain 0 (0 link(s) in 0 chain(s))\n");
  EXPECT_EQ(found.line_count, 1);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 0);
}

TEST(FindLongestDspChain, UsesTheLastLineWhenItAppearsMoreThanOnce) {
  // the real log with a second line, 7, appended.
  LongestDspChain found = findInFile(yosys_log_line_twice_file);
  EXPECT_EQ(found.line_count, 2);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 7);

  // a smaller number last: the last line wins, not the largest.
  found = findInText("ql_dsp4_chain_report: longest DSP chain 5 (4 link(s) in 1 chain(s))\n"
                     "ql_dsp4_chain_report: longest DSP chain 3 (2 link(s) in 1 chain(s))\n"
                     "ql_dsp4_chain_report: longest DSP chain 2 (1 link(s) in 1 chain(s))\n");
  EXPECT_EQ(found.line_count, 3);
  ASSERT_TRUE(found.length.has_value());
  EXPECT_EQ(*found.length, 2);
}

TEST(FindLongestDspChain, FindsNoLineWhenThereIsNone) {
  const std::vector<std::pair<std::string, std::string>> logs_without_the_line = {
      {"empty log", ""},
      {"only the older ql_dspv4 cascade line", old_line_only},
      {"the plugin's loop warning",
       "Warning: ql_dsp4_chain_report: top.d0 is in a PCIN loop; not counted.\n"},
      {"the line not at the start of a line",
       "  ql_dsp4_chain_report: longest DSP chain 3 (2 link(s) in 1 chain(s))\n"},
      {"the prefix with no number", "ql_dsp4_chain_report: longest DSP chain \n"},
      {"the prefix with a negative number",
       "ql_dsp4_chain_report: longest DSP chain -3 (2 link(s) in 1 chain(s))\n"},
      {"the prefix with a ten-digit number",
       "ql_dsp4_chain_report: longest DSP chain 9999999999 (1 link(s) in 1 chain(s))\n"},
  };
  for (const auto& [name, text] : logs_without_the_line) {
    const LongestDspChain found = findInText(text);
    EXPECT_EQ(found.line_count, 0) << name;
    EXPECT_FALSE(found.length.has_value()) << name;
  }

  // the real Yosys-path log with the new line deleted: its old line still says longest 2.
  const LongestDspChain found = findInFile(yosys_log_line_deleted_file);
  EXPECT_EQ(found.line_count, 0);
  EXPECT_FALSE(found.length.has_value());

  // a log file that does not exist.
  std::ifstream missing_log(testDataPath("no_such_synth.log"));
  const LongestDspChain found_in_missing_log = CompilerOpenFPGA_ql::findLongestDspChain(missing_log);
  EXPECT_EQ(found_in_missing_log.line_count, 0);
  EXPECT_FALSE(found_in_missing_log.length.has_value());
}

// ---- scriptHasMaxChainLengthOption ----

TEST(ScriptHasMaxChainLengthOption, FindsTheOption) {
  EXPECT_TRUE(scriptFileHasOption(script_with_option_file));
  EXPECT_TRUE(scriptTextHasOption(
      "    parser.add_argument('--max_chain_length', type=int, default=0,\n"));
}

TEST(ScriptHasMaxChainLengthOption, DoesNotFindTheOptionWhenItIsMissing) {
  EXPECT_FALSE(scriptFileHasOption(script_without_option_file));
  EXPECT_FALSE(scriptTextHasOption(""));
  // the bare parameter name is not the command-line option.
  EXPECT_FALSE(scriptTextHasOption("def build_layout(clb_count, max_chain_length=0):\n"));
  std::ifstream missing_script(testDataPath("no_such_add_layout.py"));
  EXPECT_FALSE(CompilerOpenFPGA_ql::scriptHasMaxChainLengthOption(missing_script));
}

// ---- deviceHasDspV4 ----

TEST(DeviceHasDspV4, FindsDspV4) {
  const std::vector<std::pair<std::string, std::string>> configs_with_dsp_v4 = {
      {"DSP_VERSION v4.0", R"({"DSP_VERSION": "v4.0"})"},
      {"DSP_VERSION V4.1, a later minor version", R"({"DSP_VERSION": "V4.1"})"},
      {"only the older DSP_TYPE", R"({"DSP_TYPE": "DSPV4"})"},
      {"DSP_VERSION wins over DSP_TYPE", R"({"DSP_VERSION": "v4.0", "DSP_TYPE": "DSPV2"})"},
  };
  for (const auto& [name, text] : configs_with_dsp_v4) {
    EXPECT_TRUE(CompilerOpenFPGA_ql::deviceHasDspV4(json::parse(text))) << name;
  }
  EXPECT_TRUE(CompilerOpenFPGA_ql::deviceHasDspV4(configFromFile(dsp_v4_config_file)));
}

TEST(DeviceHasDspV4, FindsNoDspV4) {
  const std::vector<std::pair<std::string, std::string>> configs_without_dsp_v4 = {
      {"DSP_VERSION v1.0", R"({"DSP_VERSION": "v1.0"})"},
      {"only the older DSP_TYPE, DSPV2", R"({"DSP_TYPE": "DSPV2"})"},
      {"DSP_VERSION wins over DSP_TYPE", R"({"DSP_VERSION": "v1.0", "DSP_TYPE": "DSPV4"})"},
      {"a DSP_VERSION with no major version", R"({"DSP_VERSION": "v"})"},
      {"neither key", R"({"DEVICE_TYPE": "CUSTOM"})"},
  };
  for (const auto& [name, text] : configs_without_dsp_v4) {
    EXPECT_FALSE(CompilerOpenFPGA_ql::deviceHasDspV4(json::parse(text))) << name;
  }
  // no config.json: loadDeviceConfigJSON() leaves the json empty.
  EXPECT_FALSE(CompilerOpenFPGA_ql::deviceHasDspV4(json()));
  for (const std::string& config_file_name : {dsp_v1_config_file, no_dsp_key_config_file}) {
    EXPECT_FALSE(CompilerOpenFPGA_ql::deviceHasDspV4(configFromFile(config_file_name)))
        << config_file_name;
  }
}

// ---- decideMaxChainLength: passed ----

TEST(DecideMaxChainLength, PassesTheOptionForAChainOfTwoOrMoreOnAnAutoLayoutWhoseScriptHasIt) {
  MaxChainLengthDecision decision = decide(lines(1, 2), dsp_v4_device, true, true);
  EXPECT_EQ(decision.message, "Longest DSP chain: 2. Passing --max_chain_length 2 to add_layout.py.");
  EXPECT_EQ(decision.option, " --max_chain_length 2");
  EXPECT_FALSE(decision.message_is_warning);
  EXPECT_EQ(decision.repeated_lines_message, "");

  decision = decide(lines(1, 3), dsp_v4_device, true, true);
  EXPECT_EQ(decision.message, "Longest DSP chain: 3. Passing --max_chain_length 3 to add_layout.py.");
  EXPECT_EQ(decision.option, " --max_chain_length 3");
  EXPECT_FALSE(decision.message_is_warning);
  EXPECT_EQ(decision.repeated_lines_message, "");

  decision = decide(lines(1, 64), dsp_v4_device, true, true);
  EXPECT_EQ(decision.message,
            "Longest DSP chain: 64. Passing --max_chain_length 64 to add_layout.py.");
  EXPECT_EQ(decision.option, " --max_chain_length 64");
  EXPECT_FALSE(decision.message_is_warning);
  EXPECT_EQ(decision.repeated_lines_message, "");
}

TEST(DecideMaxChainLength, SaysWhenTheLineRepeatsAndPassesTheOption) {
  const MaxChainLengthDecision decision = decide(lines(2, 5), dsp_v4_device, true, true);
  EXPECT_EQ(decision.repeated_lines_message,
            "Found 2 \"longest DSP chain\" lines in top_synth.log. Using the last.");
  EXPECT_EQ(decision.message, "Longest DSP chain: 5. Passing --max_chain_length 5 to add_layout.py.");
  EXPECT_EQ(decision.option, " --max_chain_length 5");
}

// ---- decideMaxChainLength: not passed ----

TEST(DecideMaxChainLength, SaysWhenTheLineRepeatsAndDoesNotPassTheOption) {
  const MaxChainLengthDecision decision = decide(lines(3, 0), dsp_v4_device, false, false);
  EXPECT_EQ(decision.repeated_lines_message,
            "Found 3 \"longest DSP chain\" lines in top_synth.log. Using the last.");
  EXPECT_EQ(decision.message, "Longest DSP chain: 0. Below 2: --max_chain_length not passed.");
  EXPECT_EQ(decision.option, "");
}

TEST(DecideMaxChainLength, DoesNotPassTheOptionWithoutALineOnADspV4Device) {
  for (const bool auto_layout_mode : {true, false}) {
    for (const bool script_has_option : {true, false}) {
      const MaxChainLengthDecision decision =
          decide(no_line, dsp_v4_device, auto_layout_mode, script_has_option);
      EXPECT_EQ(decision.message,
                "No \"longest DSP chain\" line in top_synth.log: --max_chain_length not passed.");
      EXPECT_EQ(decision.option, "");
      EXPECT_FALSE(decision.message_is_warning);
      EXPECT_EQ(decision.repeated_lines_message, "");
    }
  }
}

TEST(DecideMaxChainLength, SaysNothingAndPassesNothingWithoutALineOnADeviceWithoutDspV4) {
  for (const bool auto_layout_mode : {true, false}) {
    for (const bool script_has_option : {true, false}) {
      const MaxChainLengthDecision decision =
          decide(no_line, device_without_dsp_v4, auto_layout_mode, script_has_option);
      EXPECT_EQ(decision.message, "");
      EXPECT_EQ(decision.option, "");
      EXPECT_FALSE(decision.message_is_warning);
      EXPECT_EQ(decision.repeated_lines_message, "");
    }
  }

  // as Packing() calls it: the real log without the line, real packages without DSP-V4.
  for (const std::string& config_file_name : {dsp_v1_config_file, no_dsp_key_config_file}) {
    const MaxChainLengthDecision decision = decideFromFiles(
        yosys_log_line_deleted_file, config_file_name, true, script_with_option_file);
    EXPECT_EQ(decision.message, "") << config_file_name;
    EXPECT_EQ(decision.option, "") << config_file_name;
    EXPECT_FALSE(decision.message_is_warning) << config_file_name;
    EXPECT_EQ(decision.repeated_lines_message, "") << config_file_name;
  }
}

TEST(DecideMaxChainLength, DoesNotPassTheOptionBelowTwo) {
  // asked before the layout mode: the same message on an AUTO layout and on any other.
  for (const bool auto_layout_mode : {true, false}) {
    MaxChainLengthDecision decision = decide(lines(1, 0), dsp_v4_device, auto_layout_mode, true);
    EXPECT_EQ(decision.message, "Longest DSP chain: 0. Below 2: --max_chain_length not passed.");
    EXPECT_EQ(decision.option, "");
    EXPECT_FALSE(decision.message_is_warning);

    decision = decide(lines(1, 1), dsp_v4_device, auto_layout_mode, true);
    EXPECT_EQ(decision.message, "Longest DSP chain: 1. Below 2: --max_chain_length not passed.");
    EXPECT_EQ(decision.option, "");
    EXPECT_FALSE(decision.message_is_warning);
  }
}

TEST(DecideMaxChainLength, DoesNotPassTheOptionWhenTheLayoutIsNotAuto) {
  // CUSTOM, RESOURCES and FIXED all reach the decision as "not AUTO": FOEDAG maps
  // the device's config.json to that one flag before this point.
  for (const char* layout : {"CUSTOM", "RESOURCES", "FIXED"}) {
    for (const bool script_has_option : {true, false}) {
      MaxChainLengthDecision decision =
          decide(lines(1, 2), dsp_v4_device, false, script_has_option);
      EXPECT_EQ(decision.message,
                "Longest DSP chain: 2. Layout mode is not AUTO: --max_chain_length not passed.")
          << layout;
      EXPECT_EQ(decision.option, "") << layout;
      EXPECT_FALSE(decision.message_is_warning) << layout;

      decision = decide(lines(1, 64), dsp_v4_device, false, script_has_option);
      EXPECT_EQ(decision.message,
                "Longest DSP chain: 64. Layout mode is not AUTO: --max_chain_length not passed.")
          << layout;
      EXPECT_EQ(decision.option, "") << layout;
      EXPECT_FALSE(decision.message_is_warning) << layout;
    }
  }
}

TEST(DecideMaxChainLength, WarnsAndDoesNotPassTheOptionWhenTheScriptLacksIt) {
  MaxChainLengthDecision decision = decide(lines(1, 2), dsp_v4_device, true, false);
  EXPECT_EQ(decision.message,
            "Longest DSP chain: 2. This device package's add_layout.py has no --max_chain_length: "
            "it cannot size for a chain of 2.");
  EXPECT_TRUE(decision.message_is_warning);
  EXPECT_EQ(decision.option, "");

  decision = decide(lines(1, 64), dsp_v4_device, true, false);
  EXPECT_EQ(decision.message,
            "Longest DSP chain: 64. This device package's add_layout.py has no --max_chain_length: "
            "it cannot size for a chain of 64.");
  EXPECT_TRUE(decision.message_is_warning);
  EXPECT_EQ(decision.option, "");
}

// ---- decideMaxChainLength: a line read, on a device with or without DSP-V4 ----

TEST(DecideMaxChainLength, GivesTheSameAnswerWithOrWithoutDspV4WhenTheLineIsRead) {
  struct LineRead {
    std::string name;
    LongestDspChain longest_chain;
    bool auto_layout_mode;
    bool script_has_option;
  };
  const std::vector<LineRead> lines_read = {
      {"message 1", lines(1, 2), true, true},
      {"message 2", lines(1, 0), true, true},
      {"message 3", lines(1, 2), false, true},
      {"message 4", lines(1, 2), true, false},
      {"Found K, then message 1", lines(2, 5), true, true},
  };
  for (const LineRead& line_read : lines_read) {
    const MaxChainLengthDecision with_dsp_v4 =
        decide(line_read.longest_chain, dsp_v4_device, line_read.auto_layout_mode,
               line_read.script_has_option);
    const MaxChainLengthDecision without_dsp_v4 =
        decide(line_read.longest_chain, device_without_dsp_v4, line_read.auto_layout_mode,
               line_read.script_has_option);
    EXPECT_EQ(without_dsp_v4.message, with_dsp_v4.message) << line_read.name;
    EXPECT_EQ(without_dsp_v4.option, with_dsp_v4.option) << line_read.name;
    EXPECT_EQ(without_dsp_v4.message_is_warning, with_dsp_v4.message_is_warning)
        << line_read.name;
    EXPECT_EQ(without_dsp_v4.repeated_lines_message, with_dsp_v4.repeated_lines_message)
        << line_read.name;
  }
}

// ---- the four helpers together, as Packing() calls them, on real files ----

TEST(ScriptCheck, HasOption) {
  for (const std::string& log_file_name : {yosys_log_file, synplify_log_file}) {
    const MaxChainLengthDecision decision =
        decideFromFiles(log_file_name, dsp_v4_config_file, true, script_with_option_file);
    EXPECT_EQ(decision.message, "Longest DSP chain: 2. Passing --max_chain_length 2 to add_layout.py.")
        << log_file_name;
    EXPECT_EQ(decision.option, " --max_chain_length 2") << log_file_name;
    EXPECT_FALSE(decision.message_is_warning) << log_file_name;
  }
  const MaxChainLengthDecision decision = decideFromFiles(
      yosys_log_line_twice_file, dsp_v4_config_file, true, script_with_option_file);
  EXPECT_EQ(decision.repeated_lines_message,
            "Found 2 \"longest DSP chain\" lines in top_synth.log. Using the last.");
  EXPECT_EQ(decision.message, "Longest DSP chain: 7. Passing --max_chain_length 7 to add_layout.py.");
  EXPECT_EQ(decision.option, " --max_chain_length 7");
}

TEST(ScriptCheckRefused, LacksOption) {
  for (const std::string& log_file_name : {yosys_log_file, synplify_log_file}) {
    const MaxChainLengthDecision decision =
        decideFromFiles(log_file_name, dsp_v4_config_file, true, script_without_option_file);
    EXPECT_EQ(decision.message,
              "Longest DSP chain: 2. This device package's add_layout.py has no --max_chain_length: "
              "it cannot size for a chain of 2.")
        << log_file_name;
    EXPECT_TRUE(decision.message_is_warning) << log_file_name;
    EXPECT_EQ(decision.option, "") << log_file_name;
  }
}

TEST(NoChainLengthLine, SaysSoAndPassesNothing) {
  const MaxChainLengthDecision decision = decideFromFiles(
      yosys_log_line_deleted_file, dsp_v4_config_file, true, script_with_option_file);
  EXPECT_EQ(decision.message,
            "No \"longest DSP chain\" line in top_synth.log: --max_chain_length not passed.");
  EXPECT_EQ(decision.option, "");
  EXPECT_FALSE(decision.message_is_warning);
}
