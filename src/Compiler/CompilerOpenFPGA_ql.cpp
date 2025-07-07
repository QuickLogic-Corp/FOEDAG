/*
   Copyright 2021 The Foedag team

   GPL License

   Copyright (c) 2021 The Open-Source FPGA Foundation

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

// clang-format off

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#include <process.h>
#else
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#endif

#include <QDebug>
#include <QDomDocument>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QDirIterator>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>
#include <regex>
#include <vector>
#include <string>
#include <locale>
#include <fstream>
#include <cmath>
#include <unordered_set>
#include <unordered_map>

#include "Compiler/CompilerOpenFPGA_ql.h"
#include "Compiler/Constraints.h"
#include "Log.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "Utils/FileUtils.h"
#include "Utils/LogUtils.h"
#include "Utils/StringUtils.h"
#include "nlohmann_json/json.hpp"
#include "scope_guard/scope_guard.hpp"
#include "MainWindow/main_window.h"
#include "Main/WidgetFactory.h"
#include "Main/Settings.h"
#include <CRFileCryptProc.hpp>
#include <QWidget>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>

#include "QLDeviceManager.h"
#include "QLSettingsManager.h"
#include "QLMetricsManager.h"

extern const char* foedag_version_number;
extern const char* foedag_build_date;
extern const char* foedag_git_hash;
extern const char* foedag_build_type;

using json = nlohmann::ordered_json;

using namespace FOEDAG;

void CompilerOpenFPGA_ql::Version(std::ostream* out) {
  (*out) << "QuickLogic Aurora"
         << "\n";
  std::string str_foedag_build_date(foedag_build_date);
  std::replace( str_foedag_build_date.begin(), str_foedag_build_date.end(), '_', ' ');
  if (std::string(foedag_version_number) != "${VERSION_NUMBER}")
    (*out) << "Version     : " << foedag_version_number << "\n";
  if (std::string(foedag_git_hash) != "${GIT_HASH}")
    (*out) << "Revision    : " << foedag_git_hash << "\n";
  (*out) << "Date        : " << str_foedag_build_date << "\n";
  (*out) << "Build       : " << foedag_build_type << "\n";
}

CompilerOpenFPGA_ql::~CompilerOpenFPGA_ql() {
  CleanTempFiles();
}

void CompilerOpenFPGA_ql::Help(std::ostream* out) {
  (*out) << "------------------------------------" << std::endl;
  (*out) << "-----  QuickLogic Aurora HELP  -----" << std::endl;
  (*out) << "------------------------------------" << std::endl;
  (*out) << "Options:" << std::endl;
  (*out) << "   --help           : This help" << std::endl;
  (*out) << "   --version        : Version" << std::endl;
  (*out) << "   --batch          : Tcl only, no GUI" << std::endl;
  (*out) << "   --replay <script>: Replay GUI test" << std::endl;
  (*out) << "   --script <script>: Execute a Tcl script" << std::endl;
  (*out) << "   --project <project file>: Open a project" << std::endl;
  (*out) << "   --compiler <name>: Compiler name {openfpga...}, default is "
            "a dummy compiler"
         << std::endl;
  (*out) << "   --mute           : Mutes stdout in batch mode" << std::endl;
  (*out) << "   --verific        : Uses Verific parser" << std::endl;
  (*out) << "Tcl commands:" << std::endl;
  (*out) << "   help                       : This help" << std::endl;
  (*out) << "   copy_files_on_add <on/off> : sets whether to copy all the  "
            "design files into the generated project directory"
         << std::endl;
  (*out) << "   create_design <name> ?-type <project type>? : Creates a design "
            "with <name> name"
         << std::endl;
  (*out) << "   close_design     : Close current design" << std::endl;
  (*out) << "               <project type> : rtl, gate-level, post-map" << std::endl;
  (*out) << "   open_project <file>        : Opens a project in started "
            "upfront GUI"
         << std::endl;
  (*out) << "   run_project <file>         : Opens and immediately runs the "
            "project"
         << std::endl;

  (*out) << "   target_device <name>       : Targets a device with <name> name"
         << std::endl;
  (*out) << "   architecture <vpr_file.xml> ?<openfpga_file.xml>? :"
         << std::endl;
  (*out) << "                                Uses the architecture file and "
            "optional openfpga arch file (For bitstream generation)"
         << std::endl;
  (*out) << "   bitstream_config_files -bitstream <bitstream_setting.xml> "
            "-sim <sim_setting.xml> -repack <repack_setting.xml> -key "
            "<fabric_key.xml>"
         << std::endl;
  (*out) << "                              : Uses alternate bitstream "
            "generation configuration files"
         << std::endl;
  (*out) << "   set_device_size XxY        : Device fabric size selection"
         << std::endl;
  (*out) << "   custom_synth_script <file> : Uses a custom Yosys templatized "
            "script"
         << std::endl;
  (*out) << "   custom_openfpga_script <file> : Uses a custom OpenFPGA "
            "templatized "
            "script"
         << std::endl;
  (*out) << "   set_channel_width <int>    : VPR Routing channel setting"
         << std::endl;
  (*out) << "   add_design_file <file list> ?type?   ?-work <libName>?"
         << std::endl;
  (*out) << "              Each invocation of the command compiles the "
            "file list into a compilation unit "
         << std::endl;
  (*out) << "                       <type> : -VHDL_1987, -VHDL_1993, "
            "-VHDL_2000, -VHDL_2008, -V_1995, "
            "-V_2001, -SV_2005, -SV_2009, -SV_2012, -SV_2017> "
         << std::endl;
  (*out) << "              -work <libName> : Compiles the compilation unit "
            "into library <libName>, default is \"work\""
         << std::endl;
  (*out) << "   add_simulation_file <file list> ?type?   ?-work <libName>?"
         << std::endl;
  (*out) << "              Each invocation of the command compiles the "
            "file list into a compilation unit "
         << std::endl;
  (*out) << "                       <type> : -VHDL_1987, -VHDL_1993, "
            "-VHDL_2000, -VHDL_2008, -V_1995, "
            "-V_2001, -SV_2005, -SV_2009, -SV_2012, -SV_2017, -C, -CPP> "
         << std::endl;
  (*out) << "              -work <libName> : Compiles the compilation unit "
            "into library <libName>, default is \"work\""
         << std::endl;
  (*out) << "   clear_simulation_files     : Remove all simulation files"
         << std::endl;
  (*out) << "   read_netlist <file>        : Read a netlist instead of an RTL "
            "design (Skip Synthesis)"
         << std::endl;
  (*out) << "   add_include_path <path1>...: As in +incdir+" << std::endl;
  (*out) << "   add_library_path <path1>...: As in +libdir+" << std::endl;
  (*out) << "   add_library_ext <.v> <.sv> ...: As in +libext+" << std::endl;
  (*out) << "   set_macro <name>=<value>...: As in -D<macro>=<value>"
         << std::endl;
  (*out) << "   set_top_module <top> ?-work <libName>? : Sets the top module"
         << std::endl;
  (*out) << "   add_constraint_file <file> : Sets SDC + location constraints"
         << std::endl;
  (*out) << "                                Constraints: set_pin_loc, "
            "set_property mode, set_region_loc, all SDC commands"
         << std::endl;
  (*out) << "   script_path                : path of the Tcl script passed "
            "with --script"
         << std::endl;
  (*out) << "   keep <signal list> OR all_signals : Keeps the list of signals "
            "or all signals through Synthesis unchanged (unoptimized in "
            "certain cases)"
         << std::endl;
  (*out) << "   add_litex_ip_catalog <directory> : Browses directory for LiteX "
            "IP generators, adds the IP(s) to the IP Catalog"
         << std::endl;
  (*out) << "   ip_catalog ?<ip_name>?     : Lists all available IPs, and "
            "their parameters if <ip_name> is given "
         << std::endl;
  (*out) << "   configure_ip <ip_name> -mod_name <name> -out_location <path> "
            "-version <ver_name> -P<param>=\"<value>\"..."
         << std::endl;
  (*out) << "                              : Configures an IP <ip_name> and "
            "generates the corresponding file with module name"
         << std::endl;
  (*out) << "   ipgenerate ?clean?         : Generates all IP instances set by "
            "configure_ip"
         << std::endl;
  (*out) << "   remove_ip <name>           : Remove IP by name"
          << std::endl;
  (*out) << "   delete_ip <name>           : Delete IP by name"
          << std::endl;
  (*out) << "   verific_parser <on/off>    : Turns on/off Verific parser"
         << std::endl;
  (*out) << "   message_severity <message_id> <ERROR/WARNING/INFO/IGNORE> : "
            "Upgrade/downgrade RTL compilation message severity"
         << std::endl;
  (*out) << "   synthesis_type Yosys/QL/RS : Selects Synthesis type"
         << std::endl;
  (*out) << "   analyze ?clean?            : Analyzes the RTL design, "
            "generates top-level, pin and hierarchy information"
         << std::endl;
  (*out)
      << "   synthesize <optimization> ?clean? : Optional optimization (area, "
         "delay, mixed, none)"
      << std::endl;
  (*out) << "   pin_loc_assign_method <Method>: "
            "(in_define_order(Default)/random/free)"
         << std::endl;
  (*out) << "   synth_options <option list>: Yosys Options" << std::endl;
  (*out) << "   pnr_options <option list>  : VPR Options" << std::endl;
  (*out)
      << "   pnr_netlist_lang <blif, edif, verilog, vhdl> : Chooses vpr input "
         "netlist format"
      << std::endl;
  (*out) << "   packing ?clean?            : Packing" << std::endl;
  // (*out) << "   global_placement ?clean?   : Analytical placer" << std::endl;
  (*out) << "   place ?clean?              : Detailed placer" << std::endl;
  (*out) << "   route ?clean?              : Router" << std::endl;
  (*out) << "   sta ?clean?                : Statistical Timing Analysis"
         << std::endl;
  (*out) << "   power ?clean?              : Power estimator" << std::endl;
  (*out) << "   bitstream ?clean? ?enable_simulation?  : Bitstream generation"
         << std::endl;
  (*out) << "   simulate <level> ?<simulator>? ?clean? : Simulates the design "
            "and testbench"
         << std::endl;
  (*out) << "             <level>: rtl, gate, pnr, bitstream_bd, bitstream_fd."
         << std::endl;
  (*out) << "                 rtl: RTL simulation," << std::endl;
  (*out) << "                gate: post-synthesis simulation," << std::endl;
  (*out) << "                 pnr: post-pnr simulation," << std::endl;
  (*out) << "        bitstream_bd: Back-door bitstream simulation" << std::endl;
  (*out) << "        bitstream_fd: Front-door bitstream simulation"
         << std::endl;
  (*out) << "        <simulator> : verilator, vcs, questa, icarus, ghdl, "
            "xcelium"
         << std::endl;
  (*out) << "   set_top_testbench <module> : Sets the top-level testbench "
            "module/entity"
         << std::endl;
  (*out) << "   simulation_options <simulator> <phase> ?<level>? <options>"
         << std::endl;
  (*out) << "                                Sets the simulator specific "
            "options for the speicifed phase"
         << std::endl;
  (*out)
      << "                      <phase> : compilation, elaboration, simulation"
      << std::endl;
  (*out) << "----------------------------------" << std::endl;
}

// internal fallback yosys template script, if default template script is not found!
const std::string qlYosysScript = R"( 

# yosys (internal) template script for Aurora

# refer:
# 1. https://yosyshq.readthedocs.io/projects/yosys/en/latest/cmd_ref.html#command-line-reference
# 2. https://github.com/chipsalliance/yosys-f4pga-plugins/blob/main/ql-qlf-plugin/synth_quicklogic.cc (help() function describes the commands)

# load the ql-qlf plugin (don't change this):
${PLUGIN_LOAD}

# read design files:
${READ_DESIGN_FILES}

# synthesize:
${QL_SYNTH_PASS_NAME} -top ${TOP_MODULE} -family ${FAMILY} -blif ${OUTPUT_BLIF} ${YOSYS_OPTIONS}

)";


const std::string qlSynplifyScript = R"( 
#project files
${READ_DESIGN_FILES}

impl -add ${TOP_MODULE} -type fpga

#implementation attributes

set_option -vlog_std sysv
set_option -project_relative_includes 1

#device options
set_option -technology QuickLogic
set_option -part ${FAMILY}
set_option -package ""
set_option -speed_grade ""
set_option -part_companion ""

#compilation/mapping options

set_option -top_module "${TOP_MODULE}"

# hdl_compiler_options
set_option -distributed_compile 1
set_option -scm2hydra 0
set_option -scm2hydra_preserve_rtl_sig 1
set_option -hdl_strict_syntax 0

# mapper_without_write_options
set_option -frequency ${FREQUENCY_VALUE}
set_option -srs_instrumentation 1

# Quicklogic
set_option -no_sequential_opt 0
set_option -write_verilog 1
set_option -maxfan 10000
set_option -rw_check_on_ram 0
set_option -disable_io_insertion 0
set_option -bram_threshold 100000
set_option -pipe 1
set_option -infer_seqShift 1
set_option -retiming ${RETIMING_VALUE}
set_option -update_models_cp 0
set_option -run_prop_extract 1

# common_options
set_option -add_dut_hierarchy 0
set_option -prepare_readback 0

# flow_options
set_option -slr_aware_debug 0

# sequential_optimization_options
set_option -symbolic_fsm_compiler 1

# Compiler Options
set_option -compiler_compatible 0
set_option -resource_sharing 1
set_option -multi_file_compilation_unit 1

# Compiler Options
set_option -auto_infer_blackbox 0

#automatic place and route (vendor) options
set_option -write_apr_constraint 1

#set result format/file last
project -result_file "${TOP_MODULE}.vm"
impl -active "${TOP_MODULE}"

)";

// https://github.com/lnis-uofu/OpenFPGA/blob/master/openfpga_flow/misc/ys_tmpl_yosys_vpr_flow.ys
const std::string basicYosysScript = R"( 
# Yosys synthesis script for ${TOP_MODULE}
# Read source files
${READ_DESIGN_FILES}

# Technology mapping
hierarchy -top ${TOP_MODULE}
proc
${KEEP_NAMES}
techmap -D NO_LUT -map +/adff2dff.v

# Synthesis
flatten
opt_expr
opt_clean
check
opt -nodffe -nosdff
fsm
opt -nodffe -nosdff
wreduce
peepopt
opt_clean
opt -nodffe -nosdff
memory -nomap
opt_clean
opt -fast -full -nodffe -nosdff
memory_map
opt -full -nodffe -nosdff
techmap
opt -fast -nodffe -nosdff
clean

# LUT mapping
abc -lut ${LUT_SIZE}

# Check
synth -run check

# Clean and output blif
opt_clean -purge
write_blif ${OUTPUT_BLIF}
write_verilog -noexpr -nodec -defparam -norename ${OUTPUT_VERILOG}
write_edif ${OUTPUT_EDIF}
  )";

bool CompilerOpenFPGA_ql::RegisterCommands(TclInterpreter* interp,
                                        bool batchMode) {
  Compiler::RegisterCommands(interp, batchMode);
  auto select_architecture_file = [](void* clientData, Tcl_Interp* interp,
                                     int argc, const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    if (!compiler->ProjManager()->HasDesign()) {
      compiler->ErrorMessage("Create a design first: create_design <name>");
      return TCL_ERROR;
    }
    std::string name;
    if (argc < 2) {
      compiler->ErrorMessage("Specify an architecture file");
      return TCL_ERROR;
    }
    for (int i = 1; i < argc; i++) {
      std::string expandedFile = argv[i];
      bool use_orig_path = false;
      if (FileUtils::FileExists(expandedFile)) {
        use_orig_path = true;
      }

      if ((!use_orig_path) &&
          (!compiler->GetSession()->CmdLine()->Script().empty())) {
        std::filesystem::path script =
            compiler->GetSession()->CmdLine()->Script();
        std::filesystem::path scriptPath = script.parent_path();
        std::filesystem::path fullPath = scriptPath;
        fullPath.append(argv[i]);
        expandedFile = fullPath.string();
      }

      std::ifstream stream(expandedFile);
      if (!stream.good()) {
        compiler->ErrorMessage("Cannot find architecture file: " +
                               std::string(expandedFile));
        return TCL_ERROR;
      }
      std::filesystem::path the_path = expandedFile;
      if (!the_path.is_absolute()) {
        const auto& path = std::filesystem::current_path();
        expandedFile = std::filesystem::path(path / expandedFile).string();
      }
      stream.close();
      if (i == 1) {
        compiler->ArchitectureFile(expandedFile);
        compiler->Message("VPR Architecture file: " + expandedFile);
      } else {
        compiler->OpenFpgaArchitectureFile(expandedFile);
        compiler->Message("OpenFPGA Architecture file: " + expandedFile);
      }
    }
    return TCL_OK;
  };
  interp->registerCmd("architecture", select_architecture_file, this, 0);

  auto set_bitstream_config_files = [](void* clientData, Tcl_Interp* interp,
                                       int argc, const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc < 2) {
      compiler->ErrorMessage("Specify a bitstream config file");
      return TCL_ERROR;
    }
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      std::string fileType;
      if (arg == "-bitstream") {
        fileType = "bitstream";
      } else if (arg == "-sim") {
        fileType = "sim";
      } else if (arg == "-repack") {
        fileType = "repack";
      } else if (arg == "-key") {
        fileType = "key";
      } else {
        compiler->ErrorMessage(
            "Not a legal option for bitstream_config_files: " + arg);
        return TCL_ERROR;
      }
      i++;
      std::string expandedFile = argv[i];
      bool use_orig_path = false;
      if (FileUtils::FileExists(expandedFile)) {
        use_orig_path = true;
      }

      if ((!use_orig_path) &&
          (!compiler->GetSession()->CmdLine()->Script().empty())) {
        std::filesystem::path script =
            compiler->GetSession()->CmdLine()->Script();
        std::filesystem::path scriptPath = script.parent_path();
        std::filesystem::path fullPath = scriptPath;
        fullPath.append(argv[i]);
        expandedFile = fullPath.string();
      }

      std::ifstream stream(expandedFile);
      if (!stream.good()) {
        compiler->ErrorMessage("Cannot find bitstream config file: " +
                               std::string(expandedFile));
        return TCL_ERROR;
      }
      std::filesystem::path the_path = expandedFile;
      if (!the_path.is_absolute()) {
        expandedFile =
            std::filesystem::path(std::filesystem::path("..") / expandedFile)
                .string();
      }
      stream.close();
      if (fileType == "bitstream") {
        compiler->OpenFpgaBitstreamSettingFile(expandedFile);
        compiler->Message("OpenFPGA Bitstream Setting file: " + expandedFile);
      } else if (fileType == "sim") {
        compiler->OpenFpgaSimSettingFile(expandedFile);
        compiler->Message("OpenFPGA Simulation Setting file: " + expandedFile);
      } else if (fileType == "repack") {
        compiler->OpenFpgaRepackConstraintsFile(expandedFile);
        compiler->Message("OpenFPGA Repack Constraint file: " + expandedFile);
      } else if (fileType == "key") {
        compiler->OpenFpgaFabricKeyFile(expandedFile);
        compiler->Message("OpenFPGA Fabric Key Constraint file: " +
                          expandedFile);
      }
    }
    return TCL_OK;
  };
  interp->registerCmd("bitstream_config_files", set_bitstream_config_files,
                      this, 0);

  auto custom_openfpga_script = [](void* clientData, Tcl_Interp* interp,
                                   int argc, const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify an OpenFPGA script");
      return TCL_ERROR;
    }

    std::string expandedFile = argv[1];
    bool use_orig_path = false;
    if (FileUtils::FileExists(expandedFile)) {
      use_orig_path = true;
    }

    if ((!use_orig_path) &&
        (!compiler->GetSession()->CmdLine()->Script().empty())) {
      std::filesystem::path script =
          compiler->GetSession()->CmdLine()->Script();
      std::filesystem::path scriptPath = script.parent_path();
      std::filesystem::path fullPath = scriptPath;
      fullPath.append(argv[1]);
      expandedFile = fullPath.string();
    }
    std::ifstream stream(expandedFile);
    if (!stream.good()) {
      compiler->ErrorMessage("Cannot find OpenFPGA script: " +
                             std::string(expandedFile));
      return TCL_ERROR;
    }
    std::string script((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
    stream.close();
    compiler->OpenFPGAScript(script);
    return TCL_OK;
  };
  interp->registerCmd("custom_openfpga_script", custom_openfpga_script, this,
                      0);

  auto custom_synth_script = [](void* clientData, Tcl_Interp* interp, int argc,
                                const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify a Yosys script");
      return TCL_ERROR;
    }

    std::string expandedFile = argv[1];
    bool use_orig_path = false;
    if (FileUtils::FileExists(expandedFile)) {
      use_orig_path = true;
    }

    if ((!use_orig_path) &&
        (!compiler->GetSession()->CmdLine()->Script().empty())) {
      std::filesystem::path script =
          compiler->GetSession()->CmdLine()->Script();
      std::filesystem::path scriptPath = script.parent_path();
      std::filesystem::path fullPath = scriptPath;
      fullPath.append(argv[1]);
      expandedFile = fullPath.string();
    }
    std::ifstream stream(expandedFile);
    if (!stream.good()) {
      compiler->ErrorMessage("Cannot find Yosys script: " +
                             std::string(expandedFile));
      return TCL_ERROR;
    }
    std::string script((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
    stream.close();
    compiler->YosysScript(script);
    return TCL_OK;
  };
  interp->registerCmd("custom_synth_script", custom_synth_script, this, 0);

  auto set_channel_width = [](void* clientData, Tcl_Interp* interp, int argc,
                              const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify a channel width");
      return TCL_ERROR;
    }
    compiler->ChannelWidth(std::strtoul(argv[1], 0, 10));
    return TCL_OK;
  };
  interp->registerCmd("set_channel_width", set_channel_width, this, 0);

  auto message_severity = [](void* clientData, Tcl_Interp* interp, int argc,
                             const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string message;
    std::string severity;
    if (argc < 3) {
      compiler->ErrorMessage(
          "message_severity <message_id> <ERROR/WARNING/INFO/IGNORE>");
    }
    message = argv[1];
    severity = argv[2];
    MsgSeverity sev = MsgSeverity::Ignore;
    if (severity == "INFO") {
      sev = MsgSeverity::Info;
    } else if (severity == "WARNING") {
      sev = MsgSeverity::Warning;
    } else if (severity == "ERROR") {
      sev = MsgSeverity::Error;
    } else if (severity == "IGNORE") {
      sev = MsgSeverity::Ignore;
    }

    compiler->AddMsgSeverity(message, sev);
    return TCL_OK;
  };
  interp->registerCmd("message_severity", message_severity, this, 0);

  auto keep = [](void* clientData, Tcl_Interp* interp, int argc,
                 const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    for (int i = 1; i < argc; i++) {
      name = argv[i];
      if (name == "all_signals") {
        compiler->KeepAllSignals(true);
      } else {
        compiler->getConstraints()->addKeep(name);
      }
    }
    return TCL_OK;
  };
  interp->registerCmd("keep", keep, this, 0);

  auto set_device_size = [](void* clientData, Tcl_Interp* interp, int argc,
                            const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify a device size: xXy");
      return TCL_ERROR;
    }

    const std::string deviceSize{argv[1]};

    if (const auto& [res, errorMsg] = compiler->IsDeviceSizeCorrect(deviceSize);
        !res) {
      compiler->ErrorMessage(errorMsg);
      return TCL_ERROR;
    }

    compiler->DeviceSize(deviceSize);
    return TCL_OK;
  };
  interp->registerCmd("set_device_size", set_device_size, this, 0);

  auto pnr_netlist_lang = [](void* clientData, Tcl_Interp* interp, int argc,
                             const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    if (argc != 2) {
      compiler->ErrorMessage("Specify the netlist type: verilog or blif");
      return TCL_ERROR;
    }
    std::string arg = argv[1];
    if (arg == "verilog") {
      compiler->SetNetlistType(NetlistType::Verilog);
    } else if (arg == "edif") {
      compiler->SetNetlistType(NetlistType::Edif);
    } else if (arg == "blif") {
      compiler->SetNetlistType(NetlistType::Blif);
    } else if (arg == "vhdl") {
      compiler->SetNetlistType(NetlistType::VHDL);
    } else {
      compiler->ErrorMessage(
          "Invalid arg to netlist_type (verilog or blif), was: " + arg);
      return TCL_ERROR;
    }
    return TCL_OK;
  };
  interp->registerCmd("pnr_netlist_lang", pnr_netlist_lang, this, 0);

  auto verific_parser = [](void* clientData, Tcl_Interp* interp, int argc,
                           const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify on/off");
      return TCL_ERROR;
    }
    std::string arg = argv[1];
    compiler->SetUseVerific((arg == "on") ? true : false);
    return TCL_OK;
  };
  interp->registerCmd("verific_parser", verific_parser, this, 0);

#if UPSTREAM_UNUSED
  auto target_device = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    if (!compiler->ProjManager()->HasDesign()) {
      compiler->ErrorMessage("Create a design first: create_design <name>");
      return TCL_ERROR;
    }
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Please select a device");
      return TCL_ERROR;
    }
    std::string arg = argv[1];
    if (compiler->LoadDeviceData(arg)) {
      compiler->ProjManager()->setTargetDevice(arg);
      auto deviceData = compiler->deviceData();
      compiler->ProjManager()->setTargetDeviceData(
          deviceData.family, deviceData.series, deviceData.package);
    } else {
      compiler->ErrorMessage("Invalid target device: " + arg);
      return TCL_ERROR;
    }
    return TCL_OK;
  };
  interp->registerCmd("target_device", target_device, this, 0);
#endif // #if UPSTREAM_UNUSED
  
  auto synthesis_type = [](void* clientData, Tcl_Interp* interp, int argc,
                           const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    std::string name;
    if (argc != 2) {
      compiler->ErrorMessage("Specify type: Yosys/RS/QL");
      return TCL_ERROR;
    }
    // std::string arg = argv[1];
    // if (arg == "Yosys") {
    //   compiler->SynthType(SynthesisType::Yosys);
    // } else if (arg == "RS") {
    //   compiler->SynthType(SynthesisType::RS);
    // } else if (arg == "QL") {
    //   compiler->SynthType(SynthesisType::QL);
    // } else {
    //   compiler->ErrorMessage("Illegal synthesis type: " + arg);
    //   return TCL_ERROR;
    // }
    return TCL_OK;
  };
  interp->registerCmd("synthesis_type", synthesis_type, this, 0);
  
  auto show_settings = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {
                            
    //CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    QLSettingsManager::getInstance()->createSettingsWidget(false)->show();

    return TCL_OK;
  };
  interp->registerCmd("show_settings", show_settings, this, 0);

  auto show_device_selection = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {
                            
    //CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    QWidget* qlDeviceSelectionWidget = 
      QLDeviceManager::getInstance()->createDeviceSelectionWidget(false);
    qlDeviceSelectionWidget->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect( qlDeviceSelectionWidget, &QWidget::destroyed, [](){std::cout << "destroyed()" << std::endl;} );
    qlDeviceSelectionWidget->show();

    return TCL_OK;
  };
  interp->registerCmd("show_device_selection", show_device_selection, this, 0);

  auto add_device = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {

    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;

    // add_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> [force]
    // this will perform the steps:
    // 1. check if the 'device' already exists in the installation
    //      check if the '<INSTALLATION> / device_data / <family> / <foundry> / <node> / <devicename>' dir path
    //        already exists in installation
    //      if it already exists, we will display an error, and stop.
    //      if 'force' has been specified, we will push out a warning, but proceed further.
    // 2. ensure that the structure in the <source_device_data_dir_path> reflects 
    //      required structure, as specified in the document: <TODO>
    //    basically, all the required files should exist, in the right hierarchy,
    //      and missing optional files would output a warning.
    // 3. encrypt all the files in the <source_device_data_dir_path> in place
    // 4. copy over all the encrypted files & cryption db
    //      from: <source_device_data_dir_path>
    //      to: <INSTALLATION> / device_data / <family> / <foundry> / <node> / <devicename>
    //      and clean up all the encrypted files & cryption db from the <source_device_data_dir_path>

    // check args: 6 or 7(if force is specified)
    if (argc != 6 && argc != 7) {
      compiler->ErrorMessage("Please enter command in the format:\n"
                             "    encrypt <family> <foundry> <node> <devicename> <source_device_data_dir_path> [force]");
      return TCL_ERROR;
    }

    // parse args
    std::string family = std::string(argv[1]);
    std::string foundry = std::string(argv[2]);
    std::string node = std::string(argv[3]);
    std::string devicename = std::string(argv[4]);
    std::string source_device_data_dir_path = argv[5];
    bool force = false;
    if(argc == 7) {
      if( compiler->ToLower(std::string(argv[6])).compare("force") == 0 ) {
        force = true;
      }
    }

    int status = QLDeviceManager::getInstance()->addDevice(family, foundry, node, devicename, source_device_data_dir_path, force);

    if(status == 0) {
      compiler->Message("\ndevice added ok: " + family + "," + foundry + "," + node + "," + devicename);
      return TCL_OK;
    }

    compiler->Message("\nadd device failed: " + family + "," + foundry + "," + node + "," + devicename);
    return TCL_ERROR;
  };
  interp->registerCmd("add_device", add_device, this, 0);

  auto encrypt_device = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {

    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;

    // encrypt_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> [target_device_data_dir_path]
    // this will perform the steps:
    // 1. ensure that the structure in the <source_device_data_dir_path> reflects 
    //      required structure, as specified in the document: <TODO>
    //    basically, all the required files should exist, in the right hierarchy,
    //      and missing optional files would output a warning.
    // 2. encrypt all the files in the <source_device_data_dir_path> in place
    // 3. copy over all the encrypted files & cryption db
    //      from: <source_device_data_dir_path>
    //      to: <target_device_data_dir_path>
    //      and clean up all the encrypted files & cryption db from the <source_device_data_dir_path>
    //    if target path is not specified, default is a new dir created at same level as source_device_data_dir_path
    //    with the same name + "_en" added.

    // check args: 6 or 7(if target is specified)
    if (argc != 6 && argc != 7) {
      compiler->ErrorMessage("Please enter command in the format:\n"
                             "    encrypt_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> [target_device_data_dir_path]");
      return TCL_ERROR;
    }

    // parse args
    std::string family = std::string(argv[1]);
    std::string foundry = std::string(argv[2]);
    std::string node = std::string(argv[3]);
    std::string devicename = std::string(argv[4]);
    std::string source_device_data_dir_path = argv[5];
    std::string target_device_data_dir_path;
    if(argc == 7) {
      target_device_data_dir_path = argv[6];
    }

    int status = QLDeviceManager::getInstance()->encryptDevice(family, foundry, node, devicename, source_device_data_dir_path, target_device_data_dir_path);

    if(status == 0) {
      compiler->Message("\ndevice encrypted ok: " + family + "," + foundry + "," + node + "," + devicename);
      return TCL_OK;
    }

    compiler->Message("\nencrypt device failed: " + family + "," + foundry + "," + node + "," + devicename);
    return TCL_ERROR;
  };
  interp->registerCmd("encrypt_device", encrypt_device, this, 0);


  auto list_devices = [](void* clientData, Tcl_Interp* interp, int argc,
                          const char* argv[]) -> int {

  std::vector <QLDeviceType>device_list = QLDeviceManager::getInstance(true)->device_list;

  for (QLDeviceType device: device_list) {
    for (QLDeviceVariant device_variant: device.device_variants) {
      for (QLDeviceVariantLayout device_variant_layout: device_variant.device_variant_layouts) {
        std::cout << device_variant.family << ","
                  << device_variant.foundry << ","
                  << device_variant.node << ","
                  << device_variant.devicename << ","
                  << device_variant.voltage_threshold << ","
                  << device_variant.p_v_t_corner << ","
                  << device_variant_layout.name << std::endl;
      }
    }
  }
  
  return TCL_OK;
  };
  interp->registerCmd("list_devices", list_devices, this, 0);

  // note: we invoke these steps using the base class compiler.
  //       this is so that, the base class status is reflected correctly as well.
  auto route_and_sta = [](void* clientData, Tcl_Interp* interp, int argc,
                  const char* argv[]) -> int {
    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "clean") {
          compiler->RouteOpt(Compiler::RoutingOpt::Clean);
          compiler->TimingAnalysisOpt(Compiler::STAOpt::Clean);
      } else {
          compiler->ErrorMessage("Unknown option: " + arg);
      }
    }
    // route
    bool status = compiler->Compile(Action::Routing);
    // if route was ok, do STA
    if(status == true) {
      status = compiler->Compile(Action::STA);
    }

    if(status == false) {
      return TCL_ERROR;
    }
    
    return TCL_OK;
  };
  interp->registerCmd("route_and_sta", route_and_sta, this, 0);

  auto listdir = [](void* clientData, Tcl_Interp* interp, int argc,
                        const char* argv[]) -> int {

    CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)clientData;
    
    if (argc != 2) {
      compiler->ErrorMessage("provide the dirpath!");
      return TCL_ERROR;
    }

    std::string in_filepath = argv[1];

    compiler->Message("");
    compiler->Message("listing files in: " + in_filepath);

    // using Qt
    compiler->Message("");
    compiler->Message("");
    compiler->Message(" >>> Qt");
    QDirIterator allFilesIterator_xml(QString::fromStdString(in_filepath),
                                      QStringList() << "*.xml",
                                      QDir::Files,
                                      QDirIterator::Subdirectories);
    while (allFilesIterator_xml.hasNext()) {
      compiler->Message(allFilesIterator_xml.next().toStdString());
    }
    compiler->Message("");
    
    QDirIterator allFilesIterator_xmlen(QString::fromStdString(in_filepath),
                                        QStringList() << "*.xml.en",
                                        QDir::Files,
                                        QDirIterator::Subdirectories);
    while (allFilesIterator_xmlen.hasNext()) {
      compiler->Message(allFilesIterator_xmlen.next().toStdString());
    }
    compiler->Message("");

    QDirIterator allFilesIterator_db(QString::fromStdString(in_filepath),
                                     QStringList() << "*.db",
                                     QDir::Files,
                                     QDirIterator::Subdirectories);
    while (allFilesIterator_db.hasNext()) {
      compiler->Message(allFilesIterator_db.next().toStdString());
    }


    // using std:: C++17
    compiler->Message("");
    compiler->Message("");
    compiler->Message(" >>> c++17");
    std::error_code ec;
    std::vector<std::filesystem::path> xml_files;
    std::vector<std::filesystem::path> xml_en_files;
    std::vector<std::filesystem::path> db_files;
    for (const std::filesystem::directory_entry& dir_entry : 
        std::filesystem::recursive_directory_iterator(in_filepath, 
                                                      std::filesystem::directory_options::skip_permission_denied,
                                                      ec))
    {
        if(!ec) {
            // no error, proceed
            if(dir_entry.is_regular_file(ec)) {
              if(!ec) {
                // no error, proceed
                if (std::regex_match(dir_entry.path().filename().string(), std::regex(".+\\.xml", std::regex::icase))) {
                  xml_files.push_back(dir_entry.path().string());
                }
                if (std::regex_match(dir_entry.path().filename().string(), std::regex(".+\\.xml.en", std::regex::icase))) {
                  xml_en_files.push_back(dir_entry.path().string());
                }
                if (std::regex_match(dir_entry.path().filename().string(), std::regex(".+\\.db", std::regex::icase))) {
                  db_files.push_back(dir_entry.path().string());
                }
              }
            }
        }
        else {
          compiler->ErrorMessage(std::string("failed listing contents of ") + in_filepath );
        }
    }

    for (auto file_path:  xml_files) {
      compiler->Message(file_path.string());
    }
    compiler->Message("");

    for (auto file_path:  xml_en_files) {
      compiler->Message(file_path.string());
    }
    compiler->Message("");

    for (auto file_path:  db_files) {
      compiler->Message(file_path.string());
    }

    return TCL_OK;
  };
  interp->registerCmd("listdir", listdir, this, 0);

  return true;
}

std::pair<bool, std::string> CompilerOpenFPGA_ql::IsDeviceSizeCorrect(
    const std::string& size) const {
  if (m_architectureFile.empty())
    return std::make_pair(false,
                          "Please specify target device or architecture file.");
  std::filesystem::path datapath = GetSession()->Context()->DataPath();
  std::filesystem::path devicefile = datapath / "etc" / m_architectureFile;
  QFile file(devicefile.string().c_str());
  if (!file.open(QFile::ReadOnly)) {
    return std::make_pair(false,
                          "Cannot open device file: " + devicefile.string());
  }
  QDomDocument doc;
  if (!doc.setContent(&file)) {
    file.close();
    return std::make_pair(false,
                          "Incorrect device file: " + devicefile.string());
  }
  file.close();
  auto fixedLayout = doc.elementsByTagName("fixed_layout");
  if (fixedLayout.isEmpty())
    return std::make_pair(false, "Architecture file: fixed_layout is missing");
  for (int i = 0; i < fixedLayout.count(); i++) {
    auto node = fixedLayout.at(i).toElement();
    if (node.attribute("name").toStdString() == size)
      return std::make_pair(true, std::string{});
  }
  return std::make_pair(false, std::string{"Device size is not correct"});
}

bool CompilerOpenFPGA_ql::VerifyTargetDevice() const {
  const bool target = Compiler::VerifyTargetDevice();
  const bool archFile = FileUtils::FileExists(m_architectureFile);
  return target || archFile;
}

std::filesystem::path CompilerOpenFPGA_ql::removeLog(
    FOEDAG::ProjectManager* projManager, const std::string& fileName) {

  if (projManager) {
    std::filesystem::path projectPath(projManager->projectPath());
    std::filesystem::path filePath = projectPath / fileName;
    if (FileUtils::FileExists(filePath)) {
      std::filesystem::remove(filePath);
      return filePath;
    }
  }

  return "";
}

std::filesystem::path CompilerOpenFPGA_ql::copyLog(
    FOEDAG::ProjectManager* projManager, const std::string& srcFileName,
    const std::string& destFileName) {
  std::filesystem::path dest{};

  if (projManager) {
    std::filesystem::path projectPath(projManager->projectPath());
    std::filesystem::path src = projectPath / srcFileName;
    if (FileUtils::FileExists(src)) {
      dest = projectPath / destFileName;
      std::filesystem::remove(dest);
      std::filesystem::copy_file(src, dest);
    }
  }

  return dest;
}

bool CompilerOpenFPGA_ql::IPGenerate() {
   if (!m_projManager->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif
  if (!HasIPInstances()) {
    // No instances configured, no-op w/o error
    return true;
  }
  PERF_LOG("IPGenerate has started");
  Message("##################################################");
  Message("IP generation for design: " + ProjManager()->projectName());
  Message("##################################################");
  bool status = GetIPGenerator()->Generate();
  if (status) {
    Message("Design " + m_projManager->projectName() + " IPs are generated");
    m_state = State::IPGenerated;
  } else {
    ErrorMessage("Design " + m_projManager->projectName() +
                 " IPs generation failed");
  }
  return true;
}

bool CompilerOpenFPGA_ql::DesignChanged(
    const std::string& synth_script,
    const std::filesystem::path& synth_scrypt_path,
    const std::filesystem::path& outputFile) {
  bool result = false;
  auto path = std::filesystem::current_path();                  // getting path
  std::filesystem::current_path(ProjManager()->projectPath());  // setting path
  time_t time_netlist = FileUtils::Mtime(outputFile);
  if (time_netlist == -1) {
    result = true;
  }
  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    std::vector<std::string> tokens;
    StringUtils::tokenize(lang_file.second, " ", tokens);
    for (auto file : tokens) {
      file = StringUtils::trim(file);
      if (file.size()) {
        time_t tf = FileUtils::Mtime(file);
        if ((tf > time_netlist) || (tf == -1)) {
          result = true;
          break;
        }
      }
    }
  }
  for (auto path : ProjManager()->includePathList()) {
    std::vector<std::string> tokens;
    StringUtils::tokenize(FileUtils::AdjustPath(path), " ", tokens);
    for (auto file : tokens) {
      file = StringUtils::trim(file);
      if (file.size()) {
        time_t tf = FileUtils::Mtime(file);
        if ((tf > time_netlist) || (tf == -1)) {
          result = true;
          break;
        }
      }
    }
  }
  for (auto path : ProjManager()->libraryPathList()) {
    std::vector<std::string> tokens;
    StringUtils::tokenize(FileUtils::AdjustPath(path), " ", tokens);
    for (auto file : tokens) {
      file = StringUtils::trim(file);
      if (file.size()) {
        time_t tf = FileUtils::Mtime(file);
        if ((tf > time_netlist) || (tf == -1)) {
          result = true;
          break;
        }
      }
    }
  }

  std::ifstream script(synth_scrypt_path);
  if (!script.good()) {
    result = true;
  }
  std::stringstream buffer;
  buffer << script.rdbuf();
  if (synth_script != buffer.str()) {
    result = true;
  }

  // check if there are changes to the settings json
  std::string settings_json_filename = m_projManager->projectName() + ".json";
  std::filesystem::path settings_json_path = 
      std::filesystem::path(ProjManager()->projectPath()) /
      ".." /
      settings_json_filename;
  time_t tf = FileUtils::Mtime(settings_json_path);
  if ((tf > time_netlist) || (tf == -1)) {
      result = true;
  }
  
  std::filesystem::current_path(path);
  return result;
}

std::vector<std::string> CompilerOpenFPGA_ql::GetCleanFiles(
    Action action, const std::string& projectName,
    const std::string& topModule) const {
  std::vector<std::string> files;
  switch (action) {
    case Compiler::Action::Analyze:
      files = {ANALYSIS_LOG, "port_info.json",
               std::string{projectName + "_analyzer.cmd"}};
      break;
    case Compiler::Action::Synthesis:
      files = {
          std::string{projectName + "_post_synth.blif"},
          std::string{projectName + "_post_synth.edif"},
          std::string{projectName + "_post_synth.v"},
          std::string{projectName + "_post_synth.vhd"},
          std::string{projectName + ".ys"},
          std::string{projectName + "_synth.log"},
          SYNTHESIS_LOG,
      };
      break;
    case Compiler::Action::Pack:
      files = {
          std::string{projectName + "_post_synth.net"},
          std::string{projectName + "_pack.cmd"},
          "check_rr_node_warnings.log",
          "packing_pin_util.rpt",
          "pre_pack.report_timing.setup.rpt",
          std::string{projectName + "_openfpga.sdc"},
          std::string{projectName + "_post_synth_ports.json"},
          "vpr_stdout.log",
          PACKING_LOG,
      };
      break;
    case Compiler::Action::Detailed:
      files = {
          "packing_pin_util.rpt",
          std::string{projectName + "_post_place_timing.rpt"},
          std::string{projectName + "_post_synth_ports.json"},
          std::string{projectName + "_place.cmd"},
          std::string{projectName + "_openfpga.pcf"},
          "check_rr_node_warnings.log",
          std::string{projectName + "_post_synth.place"},
          "vpr_stdout.log",
          "post_place_timing.rpt",
          PLACEMENT_LOG,
      };
      break;
    case Compiler::Action::Routing:
      files = {"check_rr_node_warnings.log",
               std::string{topModule + "_post_synthesis.blif"},
               std::string{topModule + "_post_synthesis.sdf"},
               std::string{topModule + "_post_synthesis.v"},
               std::string{projectName + "_post_synth_ports.json"},
               std::string{projectName + "_route.cmd"},
               std::string{projectName + "_post_synth.route"},
               "packing_pin_util.rpt",
               "post_place_timing.rpt",
               "post_route_timing.rpt",
               TA_REPORT_TIMING_HOLD,
               TA_REPORT_TIMING_SETUP,
               "report_unconstrained_timing.hold.rpt",
               "report_unconstrained_timing.setup.rpt",
               ROUTING_LOG,
               "vpr_stdout.log"};
      break;
    case Compiler::Action::STA:
      files = {"check_rr_node_warnings.log",
               std::string{topModule + "_post_synthesis.blif"},
               std::string{topModule + "_post_synthesis.sdf"},
               std::string{topModule + "_post_synthesis.v"},
               std::string{projectName + "*_sta.cmd"},
               std::string{projectName + "_post_synth_ports.json"},
               "packing_pin_util.rpt",
               "post_place_timing.rpt",
               "post_route_timing.rpt",
               TA_TIMING_LOG_PATTERN,
               TA_REPORT_TIMING_HOLD_PATTERN,
               TA_REPORT_TIMING_SETUP_PATTERN,
               "report_unconstrained_timing.hold.rpt",
               "report_unconstrained_timing.setup.rpt",
               TIMING_ANALYSIS_LOG_PATTERN,
               "vpr_stdout.log"};
      break;
    case Compiler::Action::Power:
      files = {"post_place_timing.rpt", "post_route_timing.rpt",
               TA_TIMING_LOG, "vpr_stdout.log", POWER_ANALYSIS_LOG};
      break;
    case Compiler::Action::Bitstream:
      files = {std::string{projectName + ".openfpga"},
               std::string{projectName + "_bitstream.cmd"},
               std::string{projectName + "_post_synth_ports.json"},
               "fabric_bitstream.bit",
               "fabric_independent_bitstream.xml",
               "packing_pin_util.rpt",
               "PinMapping.xml",
               "post_place_timing.rpt",
               "post_route_timing.rpt",
               TA_TIMING_LOG,
               TA_REPORT_TIMING_HOLD,
               TA_REPORT_TIMING_SETUP,
               "report_unconstrained_timing.hold.rpt",
               "report_unconstrained_timing.setup.rpt",
               "vpr_stdout.log",
               BITSTREAM_LOG};
      break;
    default:
      break;
  }
  return files;
}

std::string CompilerOpenFPGA_ql::InitAnalyzeScript() {
  std::string analysisScript;
  if (m_useVerific) {
    // Verific parser
    std::string fileList;
    fileList += "-set-warning VERI-1063\n";
    std::string includes;
    for (auto path : ProjManager()->includePathList()) {
      includes += FileUtils::AdjustPath(path) + " ";
    }
    fileList += "-vlog-incdir " + includes + "\n";

    std::string libraries;
    for (auto path : ProjManager()->libraryPathList()) {
      libraries += FileUtils::AdjustPath(path) + " ";
    }
    fileList += "-vlog-libdir " + libraries + "\n";

    for (auto ext : ProjManager()->libraryExtensionList()) {
      fileList += "-vlog-libext " + ext + "\n";
    }

    std::string macros;
    for (auto& macro_value : ProjManager()->macroList()) {
      macros += macro_value.first + "=" + macro_value.second + " ";
    }
    fileList += "-vlog-define " + macros + "\n";

    std::string importLibs;
    auto commandsLibs = ProjManager()->DesignLibraries();
    size_t filesIndex{0};
    for (const auto& lang_file : ProjManager()->DesignFiles()) {
      std::string lang;
      std::string designLibraries;
      switch (lang_file.first.language) {
        case Design::Language::VHDL_1987:
          lang = "-vhdl87";
          break;
        case Design::Language::VHDL_1993:
          lang = "-vhdl93";
          break;
        case Design::Language::VHDL_2000:
          lang = "-vhdl2k";
          break;
        case Design::Language::VHDL_2008:
          lang = "-vhdl2008";
          break;
        case Design::Language::VHDL_2019:
          lang = "-vhdl2019";
          break;
        case Design::Language::VERILOG_1995:
          lang = "-vlog95";
          break;
        case Design::Language::VERILOG_2001:
          lang = "-vlog2k";
          break;
        case Design::Language::SYSTEMVERILOG_2005:
          lang = "-sv2005";
          break;
        case Design::Language::SYSTEMVERILOG_2009:
          lang = "-sv2009";
          break;
        case Design::Language::SYSTEMVERILOG_2012:
          lang = "-sv2012";
          break;
        case Design::Language::SYSTEMVERILOG_2017:
          lang = "-sv";
          break;
        case Design::Language::VERILOG_NETLIST:
          lang = "";
          break;
        case Design::Language::BLIF:
        case Design::Language::EBLIF:
          lang = "BLIF";
          ErrorMessage("Unsupported file format:" + lang);
          return "";
        case Design::Language::OTHER:
          // don't include it in the compilation process
          continue;
      }
      if (filesIndex < commandsLibs.size()) {
        const auto& filesCommandsLibs = commandsLibs[filesIndex];
        for (size_t i = 0; i < filesCommandsLibs.first.size(); ++i) {
          auto libName = filesCommandsLibs.second[i];
          if (!libName.empty()) {
            auto commandLib = "-work " + libName + " ";
            designLibraries += commandLib;
          }
        }
      }
      ++filesIndex;

      if (designLibraries.empty())
        fileList += lang + " " + lang_file.second + "\n";
      else
        fileList +=
            designLibraries + " " + lang + " " + lang_file.second + "\n";
    }
    if (!ProjManager()->DesignTopModule().empty()) {
      fileList += "-top " + ProjManager()->DesignTopModule() + "\n";
    }
    analysisScript = fileList;
  } else {
    // TODO: develop an analysis step with only Yosys parser (no synthesis)
    // Default Yosys parser
    /*
       std::string macros = "verilog_defines ";
       for (auto& macro_value : ProjManager()->macroList()) {
       macros += "-D" + macro_value.first + "=" + macro_value.second + " ";
       }
       macros += "\n";
       std::string includes;
       for (auto path : ProjManager()->includePathList()) {
       includes += "-I" + path + " ";
       }
       analysisScript = ReplaceAll(analysisScript, "${READ_DESIGN_FILES}",
       macros +
       "read_verilog ${READ_VERILOG_OPTIONS} "
       "${INCLUDE_PATHS} ${VERILOG_FILES}");
       std::string fileList;
       std::string lang;
       for (const auto& lang_file : ProjManager()->DesignFiles()) {
       fileList += lang_file.second + " ";
       switch (lang_file.first) {
       case Design::Language::VHDL_1987:
       case Design::Language::VHDL_1993:
       case Design::Language::VHDL_2000:
       case Design::Language::VHDL_2008:
       ErrorMessage("Unsupported language (Yosys default parser)");
       break;
       case Design::Language::VERILOG_1995:
       case Design::Language::VERILOG_2001:
       case Design::Language::SYSTEMVERILOG_2005:
       break;
       case Design::Language::SYSTEMVERILOG_2009:
       case Design::Language::SYSTEMVERILOG_2012:
       case Design::Language::SYSTEMVERILOG_2017:
       lang = "-sv";
       break;
       case Design::Language::VERILOG_NETLIST:
       case Design::Language::BLIF:
       case Design::Language::EBLIF:
       ErrorMessage("Unsupported language (Yosys default parser)");
       break;
       }
       analysisScript = fileList;
       */
  }
  return analysisScript;
}

std::string CompilerOpenFPGA_ql::FinishAnalyzeScript(const std::string& script) {
  std::string result = script;
  return result;
}

bool CompilerOpenFPGA_ql::Analyze() {

  return true;
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Log generated by ExecuteAndMonitorSystemCommand, we just need to add
    // header info to the log
    std::filesystem::path projectPath(ProjManager()->projectPath());
    std::filesystem::path logPath = projectPath / ANALYSIS_LOG;
    LogUtils::AddHeaderToLog(logPath);
  });

  auto printTopModules = [](const std::filesystem::path& filePath,
                            std::ostream* out) {
    // Check for "topModule" in a given json filePath
    // Assumed json format is [ { "topModule" : "some_value"} ]
    if (out) {
      if (FileUtils::FileExists(filePath)) {
        std::ifstream file(filePath);
        json data = json::parse(file);
        if (data.is_array()) {
          std::vector<std::string> topModules;
          std::transform(data.begin(), data.end(),
                         std::back_inserter(topModules),
                         [](json val) -> std::string {
                           return val.value("topModule", "");
                         });

          (*out) << "Top Modules: " << StringUtils::join(topModules, ", ")
                 << std::endl;
        }
      }
    }
  };

  if (AnalyzeOpt() == DesignAnalysisOpt::Clean) {
    Message("Cleaning analysis results for " + ProjManager()->projectName());
    m_state = State::IPGenerated;
    AnalyzeOpt(DesignAnalysisOpt::None);
    CleanFiles(Action::Analyze);
    return true;
  }
  if (!ProjManager()->HasDesign() && !CreateDesign("noname")) return false;
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED

  PERF_LOG("Analysis has started");
  Message("##################################################");
  Message("Analysis for design: " + ProjManager()->projectName());
  Message("##################################################");

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }


  if( QLSettingsManager::getStringValue("general", "options", "verific") == "checked" && m_projManager->synthesisTool() != Synplify && m_projManager->projectType() != PostMapSynplify) {
    m_useVerific = true;
  }
  else {
    m_useVerific = false;
  }

  std::string analysisScript = InitAnalyzeScript();
  analysisScript = FinishAnalyzeScript(analysisScript);

  std::string script_path = ProjManager()->projectName() + "_analyzer.cmd";
  script_path =
      (std::filesystem::path(ProjManager()->projectPath()) / script_path)
          .string();
  std::filesystem::path output_path =
      std::filesystem::path(ProjManager()->projectPath()) / "port_info.json";
  if (!DesignChanged(analysisScript, script_path, output_path)) {
    Message("Design didn't change: " + ProjManager()->projectName() +
            ", skipping analysis.");
    std::stringstream tempOut{};
    printTopModules(output_path, &tempOut);
    Message(tempOut.str());
    return true;
  }
  // Create Analyser command and execute
  std::ofstream ofs(script_path);
  ofs << analysisScript;
  ofs.close();
  std::string command;
  int status = 0;
  std::filesystem::path analyse_path =
      std::filesystem::path(ProjManager()->projectPath()) / ANALYSIS_LOG;
  if (m_useVerific) {
    if (!FileUtils::FileExists(m_analyzeExecutablePath)) {
      ErrorMessage("Cannot find executable: " +
                   m_analyzeExecutablePath.string());
      return false;
    }
    command = m_analyzeExecutablePath.string() + " -f " + script_path;
    Message("Analyze command: " + command);
    status = ExecuteAndMonitorSystemCommand(command, analyse_path.string());
  }
  Message("");
  std::ifstream raptor_log(analyse_path.string());
  if (raptor_log.good()) {
    std::stringstream buffer;
    buffer << raptor_log.rdbuf();
    const std::string& buf = buffer.str();
    if (buf.find("VERI-1063") != std::string::npos) {
      ErrorMessage("Design " + ProjManager()->projectName() +
                   " has an incomplete hierarchy, unknown module(s) error(s).");
      status = true;
    }
    raptor_log.close();
  }
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() + " analysis failed");
    return false;
  } else {
    m_state = State::Analyzed;
    Message("Design " + ProjManager()->projectName() + " is analyzed");
  }

  std::stringstream tempOut{};
  printTopModules(output_path, &tempOut);
  Message(tempOut.str());
  return true;
}

bool CompilerOpenFPGA_ql::Synthesize() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), ProjManager()->projectName() + "_synth.log",
            SYNTHESIS_LOG);
    QLMetricsManager::getInstance()->parseMetricsForAction(Action::Synthesis);
  });

  if (SynthOpt() == SynthesisOpt::Clean) {
    Message("Cleaning synthesis results for " + ProjManager()->projectName());
    m_state = State::IPGenerated;
    SynthOpt(SynthesisOpt::None);
    CleanFiles(Action::Synthesis);
    return true;
  }
  if (!ProjManager()->HasDesign() && !CreateDesign("noname")) return false;
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED
  PERF_LOG("Synthesize has started");
  Message("##################################################");
  Message("Synthesis for design: " + ProjManager()->projectName());
  Message("##################################################");

#if UPSTREAM_UNUSED
  // update constraints
  const auto& constrFiles = ProjManager()->getConstrFiles();
  m_constraints->reset();
  for (const auto& file : constrFiles) {
    int res{TCL_OK};
    auto status =
        m_interp->evalCmd(std::string("read_sdc {" + file + "}").c_str(), &res);
    if (res != TCL_OK) {
      ErrorMessage(status);
      return false;
    }
  }
#endif // #if UPSTREAM_UNUSED

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }

  if(m_projManager->projectType() == RTL && m_projManager->synthesisTool() == Synplify)
  {
    std::string synplifyScript; 
    m_aurora_template_script_synplify_path = QLDeviceManager::getInstance()->deviceSynplifyScriptFile();
    if(m_aurora_template_script_synplify_path.empty() || fs::is_directory(m_aurora_template_script_synplify_path)) { 
      ErrorMessage("This Device is Not Supported by Synplify.");
      return false;
    }
    synplifyScript = InitSynplifyScript();

    std::string includes;
    for (auto path : ProjManager()->includePathList()) {
      includes += "set_option -include_path " + FileUtils::AdjustPath(path) + "\n";
    }
    if(!includes.empty()) {
      synplifyScript =
        ReplaceAll(synplifyScript, "${INCLUDE_PATHS}", includes);
    }
    else{
      synplifyScript = ReplaceAll(synplifyScript, "${INCLUDE_PATHS}", std::string("# [skipped] as there is no include path"));
    }

    std::string designFiles;
    for (const auto& lang_file : ProjManager()->DesignFiles()) {
      std::string filesScript =
          "add_file ${LANGUAGE_STANDARD} ${FILES}";
      std::string lang;

      auto files = lang_file.second + " ";
      switch (lang_file.first.language) {
        case Design::Language::VHDL_1987:
        case Design::Language::VHDL_1993:
        case Design::Language::VHDL_2000:
        case Design::Language::VHDL_2008:
        case Design::Language::VHDL_2019:
          lang = "-vhdl";
          break;
        case Design::Language::VERILOG_1995:
          lang = "-verilog -vlog_std v95";
          break;
        case Design::Language::VERILOG_2001:
          lang = "-verilog -vlog_std v2001";
          break;
        case Design::Language::SYSTEMVERILOG_2005:
        case Design::Language::SYSTEMVERILOG_2009:
        case Design::Language::SYSTEMVERILOG_2012:
        case Design::Language::SYSTEMVERILOG_2017:
          lang = "-verilog -vlog_std sysv";
          break;
        case Design::Language::VERILOG_NETLIST:
        case Design::Language::BLIF:
        case Design::Language::EBLIF:
          ErrorMessage("Unsupported language (Synplify default parser)");
          break;
        case Design::Language::OTHER:
          // don't include it in the compilation process
          continue;
      }
      filesScript = ReplaceAll(filesScript, "${LANGUAGE_STANDARD}", lang);
      filesScript = ReplaceAll(filesScript, "${FILES}", files);
      designFiles += filesScript + "\n";
    }
#ifdef _WIN32
    designFiles = ReplaceAll(designFiles, "\\", "\\\\"); // without this design files won't be found by synplify
#endif
    synplifyScript =
        ReplaceAll(synplifyScript, "${READ_DESIGN_FILES}", designFiles);

    if (!ProjManager()->DesignTopModule().empty()) {
      synplifyScript = ReplaceAll(synplifyScript, "${TOP_MODULE}",
                                ProjManager()->DesignTopModule());
    } else {
      ErrorMessage("Cannot proceed without the top module specified.");
    }

    std::string synplify_family_name = 
      QLDeviceManager::getInstance()->deviceSynplifyFamilyName();
    if(!synplify_family_name.empty()) {
      synplifyScript = 
            ReplaceAll(synplifyScript, "${FAMILY}", synplify_family_name);
    }
    else {
      ErrorMessage("Synplify Family unknown for: " + QLDeviceManager::getInstance()->convertToDeviceString());
      return false;
    }

    std::string synplify_mode = QLSettingsManager::getInstance()->getStringValue("synplify", "general", "mode");
    if (synplify_mode == "speed")
    {
      synplifyScript = 
            ReplaceAll(synplifyScript, "${RETIMING_VALUE}", "1");
      synplifyScript = 
            ReplaceAll(synplifyScript, "${FREQUENCY_VALUE}", "auto");
    }
    else if (synplify_mode == "area")
    {
      synplifyScript = 
            ReplaceAll(synplifyScript, "${RETIMING_VALUE}", "0");
      synplifyScript = 
            ReplaceAll(synplifyScript, "${FREQUENCY_VALUE}", "1");
    }
    else
    {
      synplifyScript = 
            ReplaceAll(synplifyScript, "${RETIMING_VALUE}", "0");
      synplifyScript = 
            ReplaceAll(synplifyScript, "${FREQUENCY_VALUE}", "1");
    }

    std::filesystem::path synth_sdc_filepath;

    if (synplify_mode == "speed"){
      synth_sdc_filepath = QLSettingsManager::getSDCFilePath();

      // if we have a valid sdc_file_path at this point, pass it on to vpr:
      if(!synth_sdc_filepath.empty()) {
        // std::cout << "synth sdc file available: " << synth_sdc_filepath << std::endl;

        synplifyScript = ReplaceAll(synplifyScript, "${READ_SDC_FILE}", std::string("add_file") +
                                                                  std::string(" -constraint ") + 
                                                                  synth_sdc_filepath.string());
      }
      else {
        //std::cout << "synth sdc file not available." << std::endl;

        synplifyScript = ReplaceAll(synplifyScript, "${READ_SDC_FILE}", std::string("# [skipped] read sdc as there is no synth sdc file"));
      }
    }
    else
    {
       synplifyScript = ReplaceAll(synplifyScript, "${READ_SDC_FILE}", std::string("# [skipped] read sdc as the synplify mode is area."));
    }

    std::string synplify_script_path = ProjManager()->projectName() + ".prj";
    synplify_script_path =
      (std::filesystem::path(ProjManager()->projectPath()) / synplify_script_path)
          .string();
    std::ofstream ofs(synplify_script_path);
    ofs << synplifyScript;
#ifdef _WIN32
    ofs << "\n";
    ofs << "# Run all implementations of the active project.\n";
    ofs << "run -all\n";
    ofs << "\n";
    ofs << "# Immediately terminates the tool session without prompting (fix windows shell awaiting user input).\n";
    ofs << "program_terminate\n";
#endif
    ofs.close();

#ifdef _WIN32
    // it looks like synplify_base for windows is a GUI application, it does not write output to stdout by default,
    // let's use synplify_base_console instead to have proper logging into compiler console.
    const std::string synplifyExecName{"synplify_base_console"};
#else
    const std::string synplifyExecName{"synplify_base"};
#endif

    if (!FileUtils::IsSystemCommandAvailable(synplifyExecName)) {
      ErrorMessage("Synthesis cannot proceed because " + synplifyExecName + " is not found in PATH. Please ensure the Synplify tool is correctly installed, all post-installation steps are completed.");
      return false;
    }

    const std::string synplifyLogFilePath{ProjManager()->projectName() + "_synplify.log"};
#ifdef _WIN32
    // synplify_base_console -licensetype synplifybase_quicklogic $(SYNPLIFY_PRJ_FILE_AREA) -log  $(SYNPLIFY_LOG_FILE)
    std::string command = synplifyExecName + " -licensetype synplifybase_quicklogic " +
    synplify_script_path + " -log " + synplifyLogFilePath;
#else
    // synplify_base -batch -licensetype synplifybase_quicklogic $(SYNPLIFY_PRJ_FILE_AREA) >> $(SYNPLIFY_LOG_FILE) 2>&1;
    std::string command = synplifyExecName + " -batch " + "-licensetype synplifybase_quicklogic " +
    synplify_script_path + " >> " + synplifyLogFilePath;
#endif
    Message("Synthesis command: " + command);
    int status = ExecuteAndMonitorSystemCommand(command);
    CleanTempFiles();
    if (status) {
      ErrorMessage("Design " + ProjManager()->projectName() +
                  " synthesis failed");
      return false;
    } else {
      m_state = State::Synthesized;
      Message("Design " + ProjManager()->projectName() + " is synthesized");
    }
  }
  
  // use the device specific yosys script
  m_aurora_template_script_yosys_path = QLDeviceManager::getInstance()->deviceYosysScriptFile();

  if(m_aurora_template_script_yosys_path.empty()) {

    ErrorMessage("Cannot proceed without Yosys Template Script.");
    return false;
  }


  // init synthesis script from the right location according to the selected device.
  std::string yosysScript = InitSynthesisScript();


  if(QLSettingsManager::getStringValue("general", "options", "verific") == "checked" && m_projManager->synthesisTool() != Synplify && m_projManager->projectType() != PostMapSynplify) {
    m_useVerific = true;
  }
  else {
    m_useVerific = false;
  }

  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF:
        Message("Skipping synthesis, gate-level design.");
        return true;
        break;
      default:
        break;
    }
  }
  
  if(m_projManager->synthesisTool() != Synplify)
  {
    if (m_useVerific) {
      // Verific parser
      std::string fileList;
      std::string includes;

      for (auto msg_sev : MsgSeverityMap()) {
        switch (msg_sev.second) {
          case MsgSeverity::Ignore:
            fileList += "verific -set-ignore " + msg_sev.first + "\n";
            break;
          case MsgSeverity::Info:
            fileList += "verific -set-info " + msg_sev.first + "\n";
            break;
          case MsgSeverity::Warning:
            fileList += "verific -set-warning " + msg_sev.first + "\n";
            break;
          case MsgSeverity::Error:
            fileList += "verific -set-error " + msg_sev.first + "\n";
            break;
        }
      }

      // workaround for enabling usage of '-lib' option, suggested by yosyshq
      // add the following line in the ys script:
      fileList += std::string("verific -cfg veri_create_empty_box 1\n");

      // ProjectManager::addIncludePath(const std::string& includePath)
      for (auto path : ProjManager()->includePathList()) {
        includes += FileUtils::AdjustPath(path) + " ";
      }
      if(!includes.empty()) {
        fileList += "verific -vlog-incdir " + includes + "\n";
      }

      // incdir:always add the project's 'sources' directory 
      //   (works for GUI copy_to_project/ TCL copy_files_on_add cases)
      std::filesystem::path design_sources_dir_path =
          ProjManager()->ProjectFilesPath(ProjManager()->projectPath(),
                                          ProjManager()->projectName(),
                                          ProjManager()->getDesignActiveFileSet().toStdString());
      fileList += "verific -vlog-incdir " + design_sources_dir_path.string() + "\n";
      
      // incdir: if executed via TCL script, and copy_files_on_add is *not* set
      //   add the TCL script directory 
      std::filesystem::path tcl_script_dir_path = 
          QLSettingsManager::getTCLScriptDirPath();
      if(!tcl_script_dir_path.empty()) {
        if(!copyFilesOnAdd()) {
          fileList += "verific -vlog-incdir " + tcl_script_dir_path.string() + "\n";
        }
      }

      std::string libraries;
      // ProjectManager::addLibraryPath(const std::string& libraryPath)
      for (auto path : ProjManager()->libraryPathList()) {
        libraries += FileUtils::AdjustPath(path) + " ";
      }
      if(!libraries.empty()) {
        fileList += "verific -vlog-libdir " + libraries + "\n";
      }

      // -vlog-libdir : currently it does not solve anything, so it is commented out.
      // std::filesystem::path device_yosys_modules_dir_path = 
      //     QLDeviceManager::getInstance()->deviceYosysModulesDirPath() /
      //     QLDeviceManager::getInstance()->deviceYosysFamilyName();
      // fileList += "verific -vlog-libdir " + device_yosys_modules_dir_path + "\n";
      
      // recommendation by: <nak@yosyshq.com>
      // with the -vlog-libdir option, if verific can't find a module named "Foo",
      // it will look in the given directory for a file named "Foo.v".
      // if we want to use the -vlog-libdir option we would have to split
      // the primitive library into one file per module.
      // instead of using -vlog-libdir, we could use the existing files by
      // reading them in with the -lib option like this:
      //      verific -vlog2k -lib /path/to/dsp_sim.v
      // we should do this with all files that contain primitives that
      // the user might want to instantiate manually, such as the BRAM sim files.
      std::vector<std::filesystem::path> yosys_modules_pathlist = 
          QLDeviceManager::getInstance()->deviceYosysModulesPathList();

      for (std::filesystem::path yosys_module_path : yosys_modules_pathlist) {

        std::string sim_verilog_pattern = ".*_sim\\.v";

        if (std::regex_match(yosys_module_path.filename().string(),
                            std::regex(sim_verilog_pattern, std::regex::icase))) {

            fileList += std::string("verific -vlog2k -lib ") + 
                        yosys_module_path.string() +
                        "\n";
        }
      }

      // ProjectManager::addLibraryExtension(const std::string& libraryExt)
      for (auto ext : ProjManager()->libraryExtensionList()) {
        fileList += "verific -vlog-libext " + ext + "\n";
      }

      // ProjectManager::addMacro(const std::string& macroName,
      //                          const std::string& macroValue)
      std::string macros;
      for (auto& macro_value : ProjManager()->macroList()) {
        macros += macro_value.first + "=" + macro_value.second + " ";
      }
      if(!macros.empty()) {
        fileList += "verific -vlog-define " + macros + "\n";
      }

      std::string importLibs;
      auto importDesignFilesLibs = false;

      // this is available only if TCL command has specified a top module library
      // with -work <libname>
      // set_top_module <top> ?-work <libName>?
      auto topModuleLib = ProjManager()->DesignTopModuleLib();

      // this is available only if TCL command has specified a design library
      // with -work <libname>
      // add_design_file <file list> ?type? ?-work <libName>?
      auto commandsLibs = ProjManager()->DesignLibraries();

      size_t filesIndex{0};
      for (const auto& lang_file : ProjManager()->DesignFiles()) {
        std::string lang;
        std::string designLibraries;
        switch (lang_file.first.language) {
          case Design::Language::VHDL_1987:
            lang = "-vhdl87";
            break;
          case Design::Language::VHDL_1993:
            lang = "-vhdl93";
            break;
          case Design::Language::VHDL_2000:
            lang = "-vhdl2k";
            break;
          case Design::Language::VHDL_2008:
            lang = "-vhdl2008";
            break;
          case Design::Language::VHDL_2019:
            lang = "-vhdl2019";
            break;
          case Design::Language::VERILOG_1995:
            lang = "-vlog95";
            break;
          case Design::Language::VERILOG_2001:
            lang = "-vlog2k";
            importDesignFilesLibs = true;
            break;
          case Design::Language::SYSTEMVERILOG_2005:
            lang = "-sv2005";
            importDesignFilesLibs = true;
            break;
          case Design::Language::SYSTEMVERILOG_2009:
            lang = "-sv2009";
            importDesignFilesLibs = true;
            break;
          case Design::Language::SYSTEMVERILOG_2012:
            lang = "-sv2012";
            importDesignFilesLibs = true;
            break;
          case Design::Language::SYSTEMVERILOG_2017:
            lang = "-sv";
            importDesignFilesLibs = true;
            break;
          case Design::Language::VERILOG_NETLIST:
            lang = "";
            break;
          case Design::Language::BLIF:
          case Design::Language::EBLIF:
            lang = "BLIF";
            ErrorMessage("Unsupported file format:" + lang);
            return false;
          case Design::Language::OTHER:
            // don't include it in the compilation process
            continue;
        }
        if (filesIndex < commandsLibs.size()) {
          const auto& filesCommandsLibs = commandsLibs[filesIndex];
          for (size_t i = 0; i < filesCommandsLibs.first.size(); ++i) {
            auto libName = filesCommandsLibs.second[i];
            if (!libName.empty()) {
              auto commandLib = "-work " + libName + " ";
              designLibraries += commandLib;
              if (importDesignFilesLibs && libName != topModuleLib) {
                importLibs += "-L " + libName + " ";
              }
            }
          }
        }
        ++filesIndex;

        if (designLibraries.empty()) {
          fileList += "verific " + lang + " " + lang_file.second + "\n";
        }
        else {
          fileList +=
              "verific " + designLibraries + lang + " " + lang_file.second + "\n";
        }
      }
      auto topModuleLibImport = std::string{};
      if (!topModuleLib.empty())
        topModuleLibImport = "-work " + topModuleLib + " ";
      if (ProjManager()->DesignTopModule().empty()) {
        fileList += "verific -import -all\n";
      } else {
        fileList += "verific " + topModuleLibImport + importLibs + "-import " +
                    ProjManager()->DesignTopModule() + "\n";
      }
      yosysScript = ReplaceAll(yosysScript, "${READ_DESIGN_FILES}", fileList);
    } else {
    // Default Yosys parser

    for (const auto& commandLib : ProjManager()->DesignLibraries()) {
      if (!commandLib.first.empty()) {
        ErrorMessage(
            "Yosys default parser doesn't support '-work' design file "
            "command");
        break;
      }
    }

    std::string macros = "";
	  std::string includes = "";
#if UPSTREAM_UNUSED
    std::string macros = "verilog_defines ";
    for (auto& macro_value : ProjManager()->macroList()) {
      macros += "-D" + macro_value.first + "=" + macro_value.second + " ";
    }
    macros += "\n";
    std::string includes;
    for (auto path : ProjManager()->includePathList()) {
      includes += "-I" + FileUtils::AdjustPath(path) + " ";
    }
#endif // #if UPSTREAM_UNUSED

    std::string designFiles;
    for (const auto& lang_file : ProjManager()->DesignFiles()) {
      std::string filesScript =
          "read_verilog ${READ_VERILOG_OPTIONS} ${INCLUDE_PATHS} "
          "${VERILOG_FILES}";
      std::string lang;

      auto files = lang_file.second + " ";
      switch (lang_file.first.language) {
        case Design::Language::VHDL_1987:
        case Design::Language::VHDL_1993:
        case Design::Language::VHDL_2000:
        case Design::Language::VHDL_2008:
        case Design::Language::VHDL_2019:
          ErrorMessage("Unsupported language (Yosys default parser)");
          break;
        case Design::Language::VERILOG_1995:
        case Design::Language::VERILOG_2001:
        case Design::Language::SYSTEMVERILOG_2005:
          break;
        case Design::Language::SYSTEMVERILOG_2009:
        case Design::Language::SYSTEMVERILOG_2012:
        case Design::Language::SYSTEMVERILOG_2017:
          lang = "-sv";
          break;
        case Design::Language::VERILOG_NETLIST:
        case Design::Language::BLIF:
        case Design::Language::EBLIF:
          ErrorMessage("Unsupported language (Yosys default parser)");
          break;
        case Design::Language::OTHER:
          // don't include it in the compilation process
          continue;
      }
      std::string options = lang;
      options += " -nolatches";
      filesScript = ReplaceAll(filesScript, "${READ_VERILOG_OPTIONS}", options);
      filesScript = ReplaceAll(filesScript, "${INCLUDE_PATHS}", includes);
      filesScript = ReplaceAll(filesScript, "${VERILOG_FILES}", files);

      designFiles += filesScript + "\n";
    }
    yosysScript =
        ReplaceAll(yosysScript, "${READ_DESIGN_FILES}", macros + designFiles);
    }
  }
  else
  {
    #if UPSTREAM_UNUSED
        std::string macros = "verilog_defines ";
        for (auto& macro_value : ProjManager()->macroList()) {
          macros += "-D" + macro_value.first + "=" + macro_value.second + " ";
        }
        macros += "\n";
        std::string includes;
        for (auto path : ProjManager()->includePathList()) {
          includes += "-I" + FileUtils::AdjustPath(path) + " ";
        }
    #endif // #if UPSTREAM_UNUSED
    std::string vm_file_path = ProjManager()->DesignTopModule() + "/" + ProjManager()->DesignTopModule() + ".vm";
    std::string filesScript =
            "read_verilog ${READ_VERILOG_OPTIONS} "
            "${VERILOG_FILES}";
    std::string options = " -nolatches";
    filesScript = ReplaceAll(filesScript, "${READ_VERILOG_OPTIONS}", options);
    filesScript = ReplaceAll(filesScript, "${VERILOG_FILES}", vm_file_path);
    std::string designFiles = filesScript + "\n";
    yosysScript =
        ReplaceAll(yosysScript, "${READ_DESIGN_FILES}", designFiles);
  }
    yosysScript = ReplaceAll(yosysScript, "${PLUGIN_LOAD}", std::string("plugin -i ql-qlf"));

#if defined (AURORA_YOSYS_SYNTH_PASS_NAME)
// https://stackoverflow.com/questions/2751870/how-exactly-does-the-double-stringize-trick-work
#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)
  yosysScript = ReplaceAll(yosysScript, "${QL_SYNTH_PASS_NAME}", std::string(STRINGIZE(AURORA_YOSYS_SYNTH_PASS_NAME)));
#else
  yosysScript = ReplaceAll(yosysScript, "${QL_SYNTH_PASS_NAME}", std::string("synth_quicklogic"));
#endif

  if (!ProjManager()->DesignTopModule().empty()) {
    yosysScript = ReplaceAll(yosysScript, "${TOP_MODULE_DIRECTIVE}",
                             "-top " + ProjManager()->DesignTopModule());
    yosysScript = ReplaceAll(yosysScript, "${TOP_MODULE}",
                             ProjManager()->DesignTopModule());
  } else {
    yosysScript =
        ReplaceAll(yosysScript, "${TOP_MODULE_DIRECTIVE}", "-auto-top");
  }

  std::string yosys_family_name = 
    QLDeviceManager::getInstance()->deviceYosysFamilyName();
  if(!yosys_family_name.empty()) {
    yosysScript = 
          ReplaceAll(yosysScript, "${FAMILY}", yosys_family_name);
  }
  else {
    ErrorMessage("Yosys Family unknown for: " + QLDeviceManager::getInstance()->convertToDeviceString());
    return false;
  }


  
  std::filesystem::path synth_sdc_filepath = FindSynthSDCPaths();
  // if we have a valid sdc_file_path at this point, pass it on to vpr:
  if(!synth_sdc_filepath.empty()) {
    // std::cout << "synth sdc file available: " << synth_sdc_filepath << std::endl;
    
    // we have a valid SDC file
    std::filesystem::path aurora_yosys_import_script_path =
        GetSession()->Context()->DataPath() /
        std::filesystem::path("..") /
        std::filesystem::path("scripts") /
        std::filesystem::path("aurora_yosys_import.tcl");

    yosysScript = ReplaceAll(yosysScript, "${PLUGIN_LOAD_SDC}", std::string("plugin -i sdc"));

    yosysScript = ReplaceAll(yosysScript, "${CALL_TCL_IMPORT_SCRIPT}", std::string("tcl") + 
                                                                       std::string(" ") + 
                                                                       aurora_yosys_import_script_path.string());
    yosysScript = ReplaceAll(yosysScript, "${READ_SDC_FILE}", std::string("read_sdc") +
                                                              std::string(" ") + 
                                                              synth_sdc_filepath.string());
  }
  else {
    //std::cout << "synth sdc file not available." << std::endl;

    yosysScript = ReplaceAll(yosysScript, "${PLUGIN_LOAD_SDC}", std::string("# [skipped] sdc plugin load as there is no synth sdc file"));

    yosysScript = ReplaceAll(yosysScript, "${CALL_TCL_IMPORT_SCRIPT}", std::string("# [skipped] call tcl import script as there is no synth sdc file"));

    yosysScript = ReplaceAll(yosysScript, "${READ_SDC_FILE}", std::string("# [skipped] read sdc as there is no synth sdc file"));
  }
  // ---------------------------------------------------------------- synth_sdc_file --

  yosysScript = ReplaceAll(
      yosysScript, "${OUTPUT_BLIF}",
      std::string(ProjManager()->projectName() + "_post_synth.blif"));


  // use settings to populate yosys_options
  std::string yosys_options;

  if( QLSettingsManager::getStringValue("yosys", "general", "verilog") == "checked" ) {

    yosys_options += " -verilog " + std::string(m_projManager->projectName() + "_post_synth.v");
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_abc_opt") == "checked" ) {

    yosys_options += " -no_abc_opt";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_abc9") == "checked" ) {

    yosys_options += " -no_abc9";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_opt") == "checked" ) {

    yosys_options += " -no_opt";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_adder") == "checked" ) {

    yosys_options += " -no_adder";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_ff_map") == "checked" ) {

    yosys_options += " -no_ff_map";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_dsp") == "checked" ) {

    yosys_options += " -no_dsp";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "no_bram") == "checked" ) {

    yosys_options += " -no_bram";
  }

  // moving towards multi-arch support (v2.6), this setting is arch specific and should be removed
  // from user settings json, so we ignore it, even if set.
  // if( QLSettingsManager::getStringValue("yosys", "general", "no_sdff") == "checked" ) {

  //   yosys_options += " -nosdff";
  // }

  if( QLSettingsManager::getStringValue("yosys", "general", "edif") == "checked" ) {

    yosys_options += " -edif " + std::string(m_projManager->projectName() + ".edif");
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "bram_types") == "checked" ) {

    yosys_options += " -bram_types";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "use_dsp_cfg_params") == "checked" ) {

    yosys_options += " -use_dsp_cfg_params";
  }

  if( QLSettingsManager::getStringValue("yosys", "general", "synplify") == "checked"  || m_projManager->projectType() == PostMapSynplify || (m_projManager->projectType() == RTL && m_projManager->synthesisTool() == Synplify)) {

    yosys_options += " -synplify";
  }

  // pass in the path to the device specific yosys libraries directly.
  std::string yosys_modules_dir_path_string = 
      (QLDeviceManager::getInstance()->deviceYosysModulesDirPath()).string();
  if (yosys_modules_dir_path_string.back() != '/') {
    // tack on a '/' separator if it is missing to be safe:
    yosys_modules_dir_path_string += "/";
  }
  yosys_options += " -lib_path " + 
                   yosys_modules_dir_path_string;

  // TODO: trim yosys_options at the front
  yosysScript = ReplaceAll(yosysScript, "${YOSYS_OPTIONS}", yosys_options);


  yosysScript =
      ReplaceAll(yosysScript, "${OUTPUT_VERILOG}",
                 std::string(ProjManager()->projectName() + "_post_synth.v"));
  yosysScript =
      ReplaceAll(yosysScript, "${OUTPUT_VHDL}",
                 std::string(ProjManager()->projectName() + "_post_synth.vhd"));

  yosysScript = ReplaceAll(
      yosysScript, "${OUTPUT_EDIF}",
      std::string(ProjManager()->projectName() + "_post_synth.edif"));

  yosysScript = FinishSynthesisScript(yosysScript);

  std::string script_path = ProjManager()->projectName() + ".ys";
  std::string output_path;
  switch (GetNetlistType()) {
    case NetlistType::Verilog:
      output_path = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::VHDL:
      // Until we have a VHDL netlist reader in VPR
      output_path = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::Edif:
      output_path = ProjManager()->projectName() + "_post_synth.edif";
      break;
    case NetlistType::Blif:
      output_path = ProjManager()->projectName() + "_post_synth.blif";
      break;
  }

  if (!DesignChanged(yosysScript, script_path, output_path)) {
    Message("Design didn't change: " + ProjManager()->projectName() +
            ", skipping synthesis.");
    return true;
  }
  std::filesystem::remove(
      std::filesystem::path(ProjManager()->projectPath()) /
      std::string(ProjManager()->projectName() + "_post_synth.blif"));
  std::filesystem::remove(
      std::filesystem::path(ProjManager()->projectPath()) /
      std::string(ProjManager()->projectName() + "_post_synth.v"));
  // Create Yosys command and execute
  script_path =
      (std::filesystem::path(ProjManager()->projectPath()) / script_path)
          .string();
  std::ofstream ofs(script_path);
  ofs << yosysScript;
  ofs.close();
#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_yosysExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_yosysExecutablePath.string());
    return false;
  }
#endif // #if UPSTREAM_UNUSED


  std::filesystem::path yosys_executable_path = m_yosysExecutablePath;
#if(AURORA_USE_TABBYCAD == 1)
  if(m_useVerific) {
    yosys_executable_path = GetSession()->Context()->BinaryPath() /
                            ".." /
                            "tabby" /
                            "bin" /
                            "yosys_verific";
  }
#endif // #if(AURORA_USE_TABBYCAD == 1)

  std::string command =
      yosys_executable_path.string() + " -s " +
      std::string(ProjManager()->projectName() + ".ys -l " +
                  ProjManager()->projectName() + "_synth.log");
  Message("Synthesis command: " + command);
  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
    " synthesis failed");
    return false;
  } else {
    m_state = State::Synthesized;
    Message("Design " + ProjManager()->projectName() + " is synthesized");
    return true;
  }
}

std::string CompilerOpenFPGA_ql::InitSynthesisScript() {
  // Default or custom Yosys script
  if (m_yosysScript.empty()) {
#if UPSTREAM_UNUSED
    m_yosysScript = basicYosysScript;
#endif // #if UPSTREAM_UNUSED

    bool use_external_template_yosys = false;
    std::string aurora_template_script_yosys;

    // check if we have the device aurora template script available:
    if(FileUtils::FileExists(m_aurora_template_script_yosys_path)) {
        
      // get it into a ifstream
      std::ifstream stream(m_aurora_template_script_yosys_path.string());
        
      if (stream.good()) {
        aurora_template_script_yosys = 
          std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
          stream.close();
          use_external_template_yosys = true;
          
        }
    }

    if(use_external_template_yosys) {
      Message("Using External Yosys Template Script: " +
                                std::string(m_aurora_template_script_yosys_path.string()));
      m_yosysScript = aurora_template_script_yosys;
    }
    else {
      Message("Cannot load Yosys Template Script: " +
                                std::string(m_aurora_template_script_yosys_path.string()));
      Message("Using Internal Yosys Template Script.");
      m_yosysScript = qlYosysScript;
    }
  }
  return m_yosysScript;
}

std::string CompilerOpenFPGA_ql::InitSynplifyScript() {
  // Default or custom Synplify script
  if (m_synplifyScript.empty()) {
    bool use_external_template_synplify = false;
    std::string aurora_template_script_synplify;

    // check if we have the device aurora template script available:
    if(FileUtils::FileExists(m_aurora_template_script_synplify_path)) {
      
      // get it into a ifstream
      std::ifstream stream(m_aurora_template_script_synplify_path.string());
      
      if (stream.good()) {
        aurora_template_script_synplify = 
          std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
          stream.close();
          use_external_template_synplify = true;
          
        }
    }

    if(use_external_template_synplify) {
      Message("Using External Synplify Template Script: " +
                                std::string(m_aurora_template_script_synplify_path.string()));
      m_synplifyScript = aurora_template_script_synplify;
    }
    else {
      Message("Cannot load Synplify Template Script: " +
                                std::string(m_aurora_template_script_synplify_path.string()));
      Message("Using Internal Synplify Template Script.");
      m_synplifyScript = qlSynplifyScript;
    }
  }
  return m_synplifyScript;
}


std::string CompilerOpenFPGA_ql::FinishSynthesisScript(const std::string& script) {
  std::string result = script;
  // Keeps for Synthesis, preserve nodes used in constraints
  std::string keeps;
  if (m_keepAllSignals) {
    keeps += "setattr -set keep 1 w:\\*\n";
  }
  for (auto keep : m_constraints->GetKeeps()) {
    keep = ReplaceAll(keep, "@", "[");
    keep = ReplaceAll(keep, "%", "]");
    Message("Keep name: " + keep);
    keeps += "setattr -set keep 1 w:\\" + keep + "\n";
  }
  result = ReplaceAll(result, "${KEEP_NAMES}", keeps);
  result = ReplaceAll(result, "${OPTIMIZATION}", SynthMoreOpt());
  result = ReplaceAll(result, "${PLUGIN_LIB}", YosysPluginLibName());
  result = ReplaceAll(result, "${PLUGIN_NAME}", YosysPluginName());
  result = ReplaceAll(result, "${MAP_TO_TECHNOLOGY}", YosysMapTechnology());
  result = ReplaceAll(result, "${LUT_SIZE}", std::to_string(m_lut_size));
  return result;
}

std::filesystem::path CompilerOpenFPGA_ql::FindSynthSDCPaths(){
  // ---------------------------------------------------------------- synth_sdc_file ++
  // SDC file support in yosys using sdc-plugin:
  // 1. if there is a sdc file specified in yosys > sdc_plugin > sdc_file > userValue -> take this
  // 2. if there is an sdc file in the project dir of the name: <project_name>_synth.sdc -> take this
  // 3. if there is an sdc file in the TCL dir of the name: <project_name>_synth.sdc -> take this
  // then we need to process the sdc file using the sdc-plugin.
  std::filesystem::path synth_sdc_filepath;
  
  // 1. check if an sdc file is specified in the json:
  if( !QLSettingsManager::getStringValue("yosys", "sdc_plugin", "sdc_file").empty() ) {
    synth_sdc_filepath = QLSettingsManager::getStringValue("yosys", "sdc_plugin", "sdc_file");
  }
  // else, check for an sdc file with the naming convention (<project_name>_synth.sdc)
  // note that, this path is always a relative path.
  else {
    synth_sdc_filepath = ProjManager()->projectName() + std::string("_synth") + std::string(".sdc");
  }

  // check if the path specified is absolute:
  if (synth_sdc_filepath.is_absolute()) {
    // check if the file exists:
    if (!FileUtils::FileExists(synth_sdc_filepath)) {
      // currently, we ignore it, if the sdc file path is not found.
      synth_sdc_filepath.clear();
    }
  }
  // we have a relative path
  else {
    std::filesystem::path synth_sdc_filepath_absolute;
    
    // 1. check project_path
    // 2. check tcl_script_dir_path (if driven by TCL script)
    // 3. check current_dir_path

    std::filesystem::path project_path = std::filesystem::path(GlobalSession->GetCompiler()->ProjManager()->projectPath());
    synth_sdc_filepath_absolute = project_path / synth_sdc_filepath;
    if(!FileUtils::FileExists(synth_sdc_filepath_absolute)) {
      synth_sdc_filepath_absolute.clear();
    }

    // 2. check tcl_script_dir_path
    if(synth_sdc_filepath_absolute.empty()) {
      std::filesystem::path tcl_script_dir_path = QLSettingsManager::getTCLScriptDirPath();
      if(!tcl_script_dir_path.empty()) {
        synth_sdc_filepath_absolute = tcl_script_dir_path / synth_sdc_filepath;
        if(!FileUtils::FileExists(synth_sdc_filepath_absolute)) {
          synth_sdc_filepath_absolute.clear();
        }
      }
    }

    // 3. check current working dir path
    if(synth_sdc_filepath_absolute.empty()) {
      synth_sdc_filepath_absolute = synth_sdc_filepath;
      if(!FileUtils::FileExists(synth_sdc_filepath_absolute)) {
        synth_sdc_filepath_absolute.clear();
      }
    }

    // final: check if we have a valid sdc file path:
    if(!synth_sdc_filepath_absolute.empty()) {
      // assign the absolute path to the sdc_file_path variable:
      synth_sdc_filepath = synth_sdc_filepath_absolute;
    }
    else {
      // currently, we ignore it, if the sdc file path is not found.
      synth_sdc_filepath.clear();
    }
  }
  // relative file path processing done.
  return synth_sdc_filepath;
}

std::string CompilerOpenFPGA_ql::BaseVprCommand(QLDeviceTarget device_target) {

  // note: at this point, the current_path() is the project 'source' directory.

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return std::string("");
  }

  // if device_target is explicitly specified (STA does this):
  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(device_target) ) {
    device_target = QLDeviceManager::getInstance()->getCurrentDeviceTarget();
  }

  // this check continues as is for the original target device as specified in the JSON.
  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return std::string("");
  }


  std::string vpr_options;

  // parse vpr general options
#if UPSTREAM_UNUSED
  std::string device_size = "";
  if (!m_deviceSize.empty()) {
    device_size = " --device " + m_deviceSize;
  }
#endif // #if UPSTREAM_UNUSED
  if (!m_deviceSize.empty()) {
    vpr_options += std::string(" --device") + 
                    std::string(" ") + 
                    m_deviceSize;
  }
  else if( !QLSettingsManager::getStringValue("general", "device", "layout").empty() ) {
    vpr_options += std::string(" --device") + 
                    std::string(" ") + 
                    QLSettingsManager::getStringValue("general", "device", "layout");
  }
  else {
      std::cout << "Should never be here, we should have a layout specified!" << std::endl;
      return std::string("");
  }

  if( QLSettingsManager::getStringValue("vpr", "general", "timing_analysis") == "checked" ) {
    vpr_options += std::string(" --timing_analysis on");
  }
  else if( QLSettingsManager::getStringValue("vpr", "general", "timing_analysis") == "unchecked" ) {
    vpr_options += std::string(" --timing_analysis off");
  }

  if( !QLSettingsManager::getStringValue("vpr", "general", "constant_net_method").empty() ) {
    vpr_options += std::string(" --constant_net_method") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "general", "constant_net_method");
  }

  if( !QLSettingsManager::getStringValue("vpr", "general", "clock_modeling").empty() ) {
    vpr_options += std::string(" --clock_modeling") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "general", "clock_modeling");
  }


  if( QLSettingsManager::getStringValue("vpr", "general", "exit_before_pack") == "checked" ) {
    vpr_options += std::string(" --exit_before_pack on");
  }
  else if( QLSettingsManager::getStringValue("vpr", "general", "exit_before_pack") == "unchecked" ) {
    vpr_options += std::string(" --exit_before_pack off");
  }

  // parse vpr filename options
  if( !QLSettingsManager::getStringValue("vpr", "filename", "circuit_format").empty() ) {
    vpr_options += std::string(" --circuit_format") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "circuit_format");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "net_file").empty() ) {
    vpr_options += std::string(" --net_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "net_file");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "place_file").empty() ) {
    vpr_options += std::string(" --place_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "place_file");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "route_file").empty() ) {
    vpr_options += std::string(" --route_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "route_file");
  }


  // ---------------------------------------------------------------- sdc_file ++

  std::filesystem::path sdc_file_path = QLSettingsManager::getSDCFilePath();

  // if(QLSettingsManager::getInstance()->sdc_file_path_from_json && sdc_file_path.empty()) {
  //   // this is ideally an error, and should be notified.
  //   // current implementation is to ignore any invalid sdc file path.
  // }

  // if we have a valid sdc_file_path at this point, pass it on to vpr:
  if(!sdc_file_path.empty()) {
    Message(std::string("SDC file found: ") + sdc_file_path.string());
    vpr_options += std::string(" --sdc_file") + 
                   std::string(" ") + 
                   sdc_file_path.string();
  }
  else {
    Message(std::string("SDC file not found, no constraints passed to vpr."));
  }
  // ---------------------------------------------------------------- sdc_file --


  if( !QLSettingsManager::getStringValue("vpr", "filename", "write_rr_graph").empty() ) {
    vpr_options += std::string(" --write_rr_graph") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "write_rr_graph");
  }

  // parse vpr netlist options
  if( QLSettingsManager::getStringValue("vpr", "netlist", "absorb_buffer_luts") == "checked" ) {
    vpr_options += std::string(" --absorb_buffer_luts on");
  }
  else if( QLSettingsManager::getStringValue("vpr", "netlist", "absorb_buffer_luts") == "unchecked" ) {
    vpr_options += std::string(" --absorb_buffer_luts off");
  }

  // parse vpr pack options: nothing here

  // parse vpr place options: nothing here

  // parse vpr route options
  if( !QLSettingsManager::getStringValue("vpr", "route", "route_chan_width").empty() ) {
    vpr_options += std::string(" --route_chan_width") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "route", "route_chan_width");
  }

  if( !QLSettingsManager::getStringValue("vpr", "route", "max_router_iterations").empty() ) {
    vpr_options += std::string(" --max_router_iterations") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "route", "max_router_iterations");
  }

  if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "checked" ) {
    vpr_options += std::string(" --flat_routing on");
    if( QLSettingsManager::getStringValue("vpr", "route", "max_router_iterations").empty() ) {
      // if flat_routing is enabled, and user has not specified the max_router_iterations
      // then, increase maximum router iterations to a good default, to give flat router enough
      // time to converage to a legal routing solution
      vpr_options += std::string(" --max_router_iterations 100");
    }
    // otherwise, user specified max_router_iterations is honored.
  }
  else if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "unchecked" ) {
    vpr_options += std::string(" --flat_routing off");
  }

  // parse vpr analysis options
  if( QLSettingsManager::getStringValue("vpr", "analysis", "gen_post_synthesis_netlist") == "checked" ) {
    vpr_options += std::string(" --gen_post_synthesis_netlist on");
  }
  else if( QLSettingsManager::getStringValue("vpr", "analysis", "gen_post_synthesis_netlist") == "unchecked" ) {
    vpr_options += std::string(" --gen_post_synthesis_netlist off");
  }

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "post_synth_netlist_unconn_inputs").empty() ) {
    vpr_options += std::string(" --post_synth_netlist_unconn_inputs") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "analysis", "post_synth_netlist_unconn_inputs");
  }

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "post_synth_netlist_unconn_outputs").empty() ) {
    vpr_options += std::string(" --post_synth_netlist_unconn_outputs") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "analysis", "post_synth_netlist_unconn_outputs");
  }

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "timing_report_npaths").empty() ) {
    vpr_options += std::string(" --timing_report_npaths") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "analysis", "timing_report_npaths");
  }

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "timing_report_detail").empty() ) {
    vpr_options += std::string(" --timing_report_detail") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "analysis", "timing_report_detail");
  }

  if(m_projManager->synthesisTool() == Synplify || m_projManager->projectType() == PostMapSynplify) {
    if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "checked" ) {
      // using Synplify and flat_routing enabled, we get a crash at sync_netlists_to_routing_flat();
      // to skip this synchronization, we can add '--skip_sync_clustering_and_routing_results on'
      // until Synplify side is fixed to resolve the root cause of the error.
      vpr_options += std::string(" --skip_sync_clustering_and_routing_results on");
    }
  }

  // custom vpr command-line options for *all* stages
  // it is upto the user to ensure that the options are passed in correctly.
  if( !QLSettingsManager::getStringValue("vpr", "custom", "custom_vpr_options_str").empty() ) {
    // first, trim the entire string to eliminate any extra whitespace in the front and the back
    std::string vpr_custom_options_string = QLSettingsManager::getStringValue("vpr", "custom", "custom_vpr_options_str");
    vpr_custom_options_string = StringUtils::trim(vpr_custom_options_string);
    // add the options string to the end of the vpr options with one whitespace separator
    vpr_options += std::string(" ") + vpr_custom_options_string;
  }

  std::string netlistFile;
  switch (GetNetlistType()) {
    case NetlistType::Verilog:
      netlistFile = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::VHDL:
      // Until we have a VHDL netlist reader in VPR
      netlistFile = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::Edif:
      netlistFile = ProjManager()->projectName() + "_post_synth.edif";
      break;
    case NetlistType::Blif:
      netlistFile = ProjManager()->projectName() + "_post_synth.blif";
      break;
  }

  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF: {
        netlistFile = lang_file.second;
        std::filesystem::path the_path = netlistFile;
        if (!the_path.is_absolute()) {
          netlistFile =
              std::filesystem::path(std::filesystem::path("..") / netlistFile)
                  .string();
        }
        break;
      }
      default:
        break;
    }
  }
#if UPSTREAM_UNUSED
  std::string pnrOptions;
  if (!PnROpt().empty()) pnrOptions += " " + PnROpt();
  if (!PerDevicePnROptions().empty()) pnrOptions += " " + PerDevicePnROptions();
#endif // #if UPSTREAM_UNUSED

  // QLDeviceTarget device_target = QLDeviceManager::getInstance()->getCurrentDeviceTarget();

  // use rr_graph and router_lookahead files, if available in the device data:
  std::filesystem::path rr_graph_file_path = 
      QLDeviceManager::getInstance()->deviceVPRRRGraphFile(device_target);

  std::filesystem::path router_lookahead_file_path = 
      QLDeviceManager::getInstance()->deviceVPRRouterLookaheadFile(device_target);

  if(!rr_graph_file_path.empty() && !router_lookahead_file_path.empty()) {
    vpr_options +=  " --read_rr_graph " +
                    rr_graph_file_path.string() +
                    " --read_router_lookahead " +
                    router_lookahead_file_path.string();
  }


  m_architectureFile = 
      QLDeviceManager::getInstance()->deviceVPRArchitectureFile(device_target);
  if(m_architectureFile.empty()) {

    ErrorMessage("Cannot proceed without VPR Architecture file.");
    return std::string("");
  }

  if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_architectureFile)) {
    
    std::filesystem::path vpr_xml_en_path = m_architectureFile;
    m_architectureFile = GenerateTempFilePath();

    m_cryptdbPath = 
        CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath(device_target)).string(),
                                                           QLDeviceManager::getInstance()->convertToDeviceTypeString(device_target));

    if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
      Message("load cryptdb failed!");
      // empty string returned on error.
      return std::string("");
    }

    if (!CRFileCryptProc::getInstance()->decryptFile(vpr_xml_en_path, m_architectureFile)) {
      ErrorMessage("decryption failed!");
      // empty string returned on error.
      return std::string("");
    }
  }

  Message( std::string("Using vpr.xml for: ") + QLDeviceManager::getInstance()->convertToDeviceString(device_target) );

  // add the *internal* option to allow dangling nodes in the logic.
  // ref: https://github.com/verilog-to-routing/vtr-verilog-to-routing/blob/a7f573b7a5432711042ddeb9f2958cd035097a10/vpr/src/timing/timing_graph_builder.cpp#L277
  // this is a workaround, to avoid putting timing arcs for static input ports.
  vpr_options += " --allow_dangling_combinational_nodes on";

  // construct the base vpr command with all the options here.
#if UPSTREAM_UNUSED
  std::string command =
      m_vprExecutablePath.string() + std::string(" ") +
      m_architectureFile.string() + std::string(" ") +
      std::string(netlistFile + std::string(" --sdc_file ") +
                  std::string(ProjManager()->projectName() + "_openfpga.sdc") +
                  std::string(" --clock_modeling ideal --route_chan_width ") +
                  std::to_string(m_channel_width) + device_size + pnrOptions);

  return command;
#endif // #if UPSTREAM_UNUSED

  if (GenerateIOFloorPlanConstraints()){
    std::filesystem::path fp_constraint_filepath = ProjManager()->projectName() + "_constraints.xml";
    std::filesystem::path fp_constraint_filepath_absolute = std::filesystem::path(ProjManager()->projectPath()) / fp_constraint_filepath;
    if (fs::exists(fp_constraint_filepath_absolute)) {
      vpr_options += std::string(" --read_vpr_constraints " +  ProjManager()->projectName() + "_constraints.xml");
    }
  }
  else { //IO floorplanning generation failed, must stop the flow
    return std::string("");
  }

  std::string base_vpr_command =
      m_vprExecutablePath.string() + std::string(" ") +
      m_architectureFile.string() + std::string(" ") +
      std::string(netlistFile) + // NOTE: don't add a " " here as vpr options start with a " "
      vpr_options;
  
  return base_vpr_command;
}

std::string CompilerOpenFPGA_ql::BaseStaCommand() {
  std::string command =
      m_staExecutablePath.string() +
      std::string(
          " -exit ");  // allow open sta exit its tcl shell even there is error
  return command;
}

std::string CompilerOpenFPGA_ql::BaseStaScript(std::string libFileName,
                                            std::string netlistFileName,
                                            std::string sdfFileName,
                                            std::string sdcFileName) {
  std::string script =
      std::string("read_liberty ") + libFileName +
      std::string("\n") +  // add lib for test only, need to research on this
      std::string("read_verilog ") + netlistFileName + std::string("\n") +
      std::string("link_design ") + ProjManager()->projectName() +
      std::string("\n") + std::string("read_sdf ") + sdfFileName +
      std::string("\n") + std::string("read_sdc ") + sdcFileName +
      std::string("\n") +
      std::string("report_checks\n");  // to do: add more check/report flavors
  const std::string openStaFile =
      (std::filesystem::path(ProjManager()->projectPath()) /
       std::string(ProjManager()->projectName() + "_opensta.tcl"))
          .string();
  std::ofstream ofssta(openStaFile);
  ofssta << script << "\n";
  ofssta.close();
  return openStaFile;
}
bool CompilerOpenFPGA_ql::Packing() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), "vpr_stdout.log", PACKING_LOG);
    QLMetricsManager::getInstance()->parseMetricsForAction(Action::Pack);
  });

  if (PackOpt() == PackingOpt::Clean) {
    Message("Cleaning packing results for " + ProjManager()->projectName());
    m_state = State::Synthesized;
    PackOpt(PackingOpt::None);
    CleanFiles(Action::Pack);
    return true;
  }
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED
  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_vprExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_vprExecutablePath.string());
    return false;
  }
#endif // #if UPSTREAM_UNUSED
  PERF_LOG("Packing has started");
  Message("##################################################");
  Message("Packing for design: " + ProjManager()->projectName());
  Message("##################################################");
#if UPSTREAM_UNUSED
  const std::string sdcOut =
      (std::filesystem::path(ProjManager()->projectPath()) /
       std::string(ProjManager()->projectName() + "_openfpga.sdc"))
          .string();
  std::ofstream ofssdc(sdcOut);
  // TODO: Massage the SDC so VPR can understand them
  for (auto constraint : m_constraints->getConstraints()) {
    // Parse RTL and expand the get_ports, get_nets
    // Temporary dirty filtering:
    constraint = ReplaceAll(constraint, "@", "[");
    constraint = ReplaceAll(constraint, "%", "]");
    Message("Constraint: " + constraint);
    std::vector<std::string> tokens;
    StringUtils::tokenize(constraint, " ", tokens);
    constraint = "";
    // VPR does not understand: create_clock -period 2 clk -name <logical_name>
    // Pass the constraint as-is anyway
    for (uint32_t i = 0; i < tokens.size(); i++) {
      const std::string& tok = tokens[i];
      constraint += tok + " ";
    }

    // pin location constraints have to be translated to .place:
    if (constraint.find("set_pin_loc") != std::string::npos) {
      continue;
    }
    if (constraint.find("set_mode") != std::string::npos) {
      continue;
    }
    if (constraint.find("set_property") != std::string::npos) {
      continue;
    }
    ofssdc << constraint << "\n";
  }
  ofssdc.close();
#endif // #if UPSTREAM_UNUSED

  std::filesystem::path io_floor_planningpath = std::filesystem::path(ProjManager()->projectPath()) / 
                std::string(ProjManager()->projectName() + "_constraints.xml");
  if (fs::exists(io_floor_planningpath)) {
    fs::remove(io_floor_planningpath);
    Message("Deleted the previous existing " + ProjManager()->projectName() + "_constraints.xml" + 
        " Before Packing");
  }

#if UPSTREAM_UNUSED
  std::string command = BaseVprCommand() + " --pack";
#endif // #if UPSTREAM_UNUSED
  std::string command = BaseVprCommand();
  if(command.empty()) {
    ErrorMessage("Base VPR Command is empty!");
    return false;
  }

  // custom vpr command-line options for pack stage only
  // it is upto the user to ensure that the options are passed in correctly.
  if( !QLSettingsManager::getStringValue("vpr", "pack", "custom_vpr_options_str").empty() ) {
    // first, trim the entire string to eliminate any extra whitespace in the front and the back
    std::string vpr_custom_options_string = QLSettingsManager::getStringValue("vpr", "pack", "custom_vpr_options_str");
    vpr_custom_options_string = StringUtils::trim(vpr_custom_options_string);
    // add the options string to the end of the vpr options with one whitespace separator
    command += std::string(" ") + vpr_custom_options_string;
  }

  command += std::string(" ") + 
             std::string("--pack");

  std::ofstream ofs((std::filesystem::path(ProjManager()->projectPath()) /
                     std::string(ProjManager()->projectName() + "_pack.cmd"))
                        .string());
  ofs << command << std::endl;
  ofs.close();

#if UPSTREAM_UNUSED
  if (FileUtils::IsUptoDate(
          GetNetlistPath(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.net"))
              .string())) {
    m_state = State::Packed;
    Message("Design " + ProjManager()->projectName() + " packing reused");
    return true;
  }
#endif // #if UPSTREAM_UNUSED

  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() + " packing failed");
    return false;
  }
  m_state = State::Packed;
  Message("Design " + ProjManager()->projectName() + " is packed");
  return true;
}

bool CompilerOpenFPGA_ql::GlobalPlacement() {
  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
  if (GlobPlacementOpt() == GlobalPlacementOpt::Clean) {
    Message("Cleaning global placement results for " +
            ProjManager()->projectName());
    m_state = State::Packed;
    GlobPlacementOpt(GlobalPlacementOpt::None);
    return true;
  }
#if UPSTREAM_UNUSED
  if (m_state != State::Packed && m_state != State::GloballyPlaced &&
      m_state != State::Placed) {
    ErrorMessage("Design needs to be in packed state");
    return false;
  }
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED
  // state check: requires "Packed" to be completed.
  // we should be *atleast* at "Packed" or later state.
  if( (m_state == State::Packed) ||
      (m_state == State::GloballyPlaced) ||
      (m_state == State::Placed) ||
      (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in packed state"));
    return false;
  }

  PERF_LOG("GlobalPlacement has started");
  Message("##################################################");
  Message("Global Placement for design: " + ProjManager()->projectName());
  Message("##################################################");
  // TODO:
  m_state = State::GloballyPlaced;
  Message("Design " + ProjManager()->projectName() + " is globally placed");
  return true;
}

bool CompilerOpenFPGA_ql::Placement() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), "vpr_stdout.log", PLACEMENT_LOG);
    QLMetricsManager::getInstance()->parseMetricsForAction(Action::Detailed);
  });

  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
  if (PlaceOpt() == PlacementOpt::Clean) {
    Message("Cleaning placement results for " + ProjManager()->projectName());
    m_state = State::GloballyPlaced;
    PlaceOpt(PlacementOpt::None);
    CleanFiles(Action::Detailed);
    return true;
  }
#if UPSTREAM_UNUSED
  if (m_state != State::Packed && m_state != State::GloballyPlaced &&
      m_state != State::Placed) {
    ErrorMessage("Design needs to be in packed or globally placed state");
    return false;
  }
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED

  // state check: requires "Packed"/"GloballyPlaced" to be completed.
  // we should be *atleast* at "Packed"/"GloballyPlaced" or later state.
  if( (m_state == State::Packed) ||
      (m_state == State::GloballyPlaced) ||
      (m_state == State::Placed) ||
      (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in packed/globally_placed state"));
    return false;
  }

  PERF_LOG("Placement has started");
  Message("##################################################");
  Message("Placement for design: " + ProjManager()->projectName());
  Message("##################################################");
#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_vprExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_vprExecutablePath.string());
    return false;
  }
#endif // #if UPSTREAM_UNUSED
#if UPSTREAM_UNUSED
  const std::string pcfOut =
      (std::filesystem::path(ProjManager()->projectPath()) /
       std::string(ProjManager()->projectName() + "_openfpga.pcf"))
          .string();

  std::string previousConstraints;
  std::ifstream ifspcf(pcfOut);
  if (ifspcf.good()) {
    std::stringstream buffer;
    buffer << ifspcf.rdbuf();
    previousConstraints = buffer.str();
  }
  ifspcf.close();

  bool userConstraint = false;
  std::vector<std::string> constraints;
  for (auto constraint : m_constraints->getConstraints()) {
    constraint = ReplaceAll(constraint, "@", "[");
    constraint = ReplaceAll(constraint, "%", "]");
    // pin location constraints have to be translated to .place:
    if ((constraint.find("set_pin_loc") != std::string::npos)) {
      userConstraint = true;
      constraint = ReplaceAll(constraint, "set_pin_loc", "set_io");
      constraints.push_back(constraint);
    } else if (constraint.find("set_mode") != std::string::npos) {
      constraints.push_back(constraint);
      userConstraint = true;
    } else if ((constraint.find("set_property") != std::string::npos) &&
               (constraint.find(" mode ") != std::string::npos)) {
      constraint = ReplaceAll(constraint, " mode ", " ");
      constraint = ReplaceAll(constraint, "set_property", "set_mode");
      constraints.push_back(constraint);
      userConstraint = true;
    } else {
      continue;
    }
  }

  // sanity check and convert to pcf format
  if (!ConvertSdcPinConstrainToPcf(constraints)) {
    ErrorMessage("Error in SDC file for placement constraint");
    return false;
  }

  // write to file
  std::ofstream ofspcf(pcfOut);
  for (auto constraint : constraints) {
    ofspcf << constraint << "\n";
  }
  ofspcf.close();

  std::string newConstraints;
  ifspcf.open(pcfOut);
  if (ifspcf.good()) {
    std::stringstream buffer;
    buffer << ifspcf.rdbuf();
    newConstraints = buffer.str();
  }
  ifspcf.close();

  if ((previousConstraints == newConstraints) &&
      FileUtils::IsUptoDate(
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.net"))
              .string(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.place"))
              .string())) {
    m_state = State::Placed;
    Message("Design " + ProjManager()->projectName() + " placement reused");
    return true;
  }

  std::string netlistFile = ProjManager()->projectName() + "_post_synth.blif";

  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF: {
        netlistFile = lang_file.second;
        std::filesystem::path the_path = netlistFile;
        if (!the_path.is_absolute()) {
          netlistFile =
              std::filesystem::path(std::filesystem::path("..") / netlistFile)
                  .string();
        }
        break;
      }
      default:
        break;
    }
  }

  std::string command = BaseVprCommand() + " --place";
  std::string pincommand = m_pinConvExecutablePath.string();
  if (PinConstraintEnabled() && (PinAssignOpts() != PinAssignOpt::Free) &&
      FileUtils::FileExists(pincommand) && (!m_PinMapCSV.empty())) {
    if (!std::filesystem::is_regular_file(m_PinMapCSV)) {
      ErrorMessage(
          "No pin description csv file available for this device, required "
          "for set_pin_loc constraints");
      return false;
    }
    // pin_c executable can work with either xml and csv or csv only file
    if (!m_OpenFpgaPinMapXml.empty() &&
        std::filesystem::is_regular_file(m_OpenFpgaPinMapXml)) {
      pincommand += " --xml " + m_OpenFpgaPinMapXml.string();
    }
    pincommand += " --csv " + m_PinMapCSV.string();

    if (userConstraint) {
      pincommand += " --pcf " +
                    std::string(ProjManager()->projectName() + "_openfpga.pcf");
    }

    if (GetNetlistType() == NetlistType::Verilog ||
        GetNetlistType() == NetlistType::VHDL ||
        GetNetlistType() == NetlistType::Edif) {
      std::filesystem::path p(netlistFile);
      p.replace_extension();
      pincommand += " --port_info ";
      pincommand += p.string() + "_ports.json";
    } else {
      pincommand += " --blif " + netlistFile;
    }

    std::string pin_locFile = ProjManager()->projectName() + "_pin_loc.place";
    pincommand += " --output " + pin_locFile;

    // for design pins that are not explicitly constrained by user,
    // pin_c will assign legal device pins to them
    // this is configured at top level raptor shell/gui through command
    // "pin_loc_assign_method"
    pincommand += " --assign_unconstrained_pins";
    if (PinAssignOpts() == PinAssignOpt::Random) {
      pincommand += " random";
    } else if (PinAssignOpts() == PinAssignOpt::In_Define_Order) {
      pincommand += " in_define_order";
    } else if (PinAssignOpts() == PinAssignOpt::Free) {
      pincommand += " free";
    } else {  // default behavior
      pincommand += " in_define_order";
    }

    std::string pin_loc_constraint_file;

    std::ofstream ofsp(
        (std::filesystem::path(ProjManager()->projectPath()) /
         std::string(ProjManager()->projectName() + "_pin_loc.cmd"))
            .string());
    ofsp << pincommand << std::endl;
    ofsp.close();

    int status = ExecuteAndMonitorSystemCommand(pincommand);

    if (status) {
      ErrorMessage("Design " + ProjManager()->projectName() +
                   " pin conversion failed");
      return false;
    } else {
      pin_loc_constraint_file = pin_locFile;
    }

    if (PinConstraintEnabled() && (!pin_loc_constraint_file.empty())) {
      command += " --fix_clusters " + pin_loc_constraint_file;
    }
  }
#endif // #if UPSTREAM_UNUSED

  // generate pin contraints file or use pre-generated .place file, if required.
  // this string should contain the path of the PinConstraints file, if generated correctly.
  // the "filepath_fpga_fix_pins_place_str" variable will be empty if:
  // - there is no pre-generated .place file AND
  // - there is no pcf file in the project.
  std::string filepath_fpga_fix_pins_place_str;
  if (!GeneratePinConstraints(filepath_fpga_fix_pins_place_str)) return false;

  std::string command = BaseVprCommand();
  if(command.empty()) {
    ErrorMessage("Base VPR Command is empty!");
    return false;
  }

  // custom vpr command-line options for place stage only
  // it is upto the user to ensure that the options are passed in correctly.
  if( !QLSettingsManager::getStringValue("vpr", "place", "custom_vpr_options_str").empty() ) {
    // first, trim the entire string to eliminate any extra whitespace in the front and the back
    std::string vpr_custom_options_string = QLSettingsManager::getStringValue("vpr", "place", "custom_vpr_options_str");
    vpr_custom_options_string = StringUtils::trim(vpr_custom_options_string);
    // add the options string to the end of the vpr options with one whitespace separator
    command += std::string(" ") + vpr_custom_options_string;
  }

  command += std::string(" ") + 
             std::string("--place");

  if (!filepath_fpga_fix_pins_place_str.empty()) {
    command += std::string(" --fix_clusters") + 
               std::string(" ") + 
               filepath_fpga_fix_pins_place_str;
  }
  else
  {
    Message("no pcf file found, skipping PinConstraints usage!");
  }

  std::ofstream ofs((std::filesystem::path(ProjManager()->projectPath()) /
                     std::string(ProjManager()->projectName() + "_place.cmd"))
                        .string());
  ofs << command << std::endl;
  ofs.close();
  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
                 " placement failed");
    return false;
  }
  m_state = State::Placed;
  Message("Design " + ProjManager()->projectName() + " is placed");
  return true;
}

bool CompilerOpenFPGA_ql::ConvertSdcPinConstrainToPcf(
    std::vector<std::string>& constraints) {
  // do some simple sanity check during conversion
  std::vector<std::string> constraint_and_mode;
  std::map<std::string, std::string> pin_mode_map;
  // capture pin and mode map
  for (unsigned int i = 0; i < constraints.size(); i++) {
    if (constraints[i].find("set_mode") != std::string::npos) {
      std::vector<std::string> tokens;
      StringUtils::tokenize(constraints[i], " ", tokens);
      if (tokens.size() != 3) {
        ErrorMessage("Invalid set_mode command: <" + constraints[i] + ">");
        return false;
      }
      pin_mode_map.insert(
          std::pair<std::string, std::string>(tokens[2], tokens[1]));
    }
  }
  for (unsigned int i = 0; i < constraints.size(); i++) {
    if (constraints[i].find("set_io") != std::string::npos) {
      std::vector<std::string> tokens;
      StringUtils::tokenize(constraints[i], " ", tokens);
      if ((tokens.size() != 3) && (tokens.size() != 4)) {
        ErrorMessage("Invalid set_pin_loc command: <" + constraints[i] + ">");
        return false;
      }
      std::string constraint_with_mode = tokens[0] + std::string(" ") +
                                         tokens[1] + std::string(" ") +
                                         tokens[2];
      if (pin_mode_map.find(tokens[2]) != pin_mode_map.end()) {
        constraint_with_mode +=
            std::string(" -mode ") + pin_mode_map[tokens[2]];
      } else {
        constraint_with_mode += std::string(" -mode Mode_GPIO");
      }
      if (tokens.size() == 4) {
        constraint_with_mode += std::string(" -internal_pin ") + tokens[3];
      }
      constraint_and_mode.push_back(constraint_with_mode);
    }
  }
  constraints.clear();
  for (unsigned int i = 0; i < constraint_and_mode.size(); i++) {
    constraints.push_back(constraint_and_mode[i]);
  }
  return true;
}

bool CompilerOpenFPGA_ql::Route() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), "vpr_stdout.log", ROUTING_LOG);
    QLMetricsManager::getInstance()->parseMetricsForAction(Action::Routing);
    QLMetricsManager::getInstance()->parseRoutingReportForDetailedUtilization();
  });

  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
  if (RouteOpt() == RoutingOpt::Clean) {
    Message("Cleaning routing results for " + ProjManager()->projectName());
    m_state = State::Placed;
    RouteOpt(RoutingOpt::None);
    CleanFiles(Action::Routing);
    return true;
  }
#if UPSTREAM_UNUSED
  if (m_state != State::Placed) {
    ErrorMessage("Design needs to be in placed state");
    return false;
  }
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED

  // state check: requires "Placed" to be completed.
  // we should be *atleast* at "Placed" or later state.
  if( (m_state == State::Placed) ||
      (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in placed state"));
    return false;
  }
  
  PERF_LOG("Route has started");
  Message("##################################################");
  Message("Routing for design: " + ProjManager()->projectName());
  Message("##################################################");
#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_vprExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_vprExecutablePath.string());
     return false;
   }
#endif // #if UPSTREAM_UNUSED

#if UPSTREAM_UNUSED
  if (FileUtils::IsUptoDate(
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.place"))
              .string(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.route"))
              .string())) {
    m_state = State::Routed;
    Message("Design " + ProjManager()->projectName() + " routing reused");
    return true;
  }
#endif // #if UPSTREAM_UNUSED

#if UPSTREAM_UNUSED
  std::string command = BaseVprCommand() + " --route";
#endif // #if UPSTREAM_UNUSED
  std::string command = BaseVprCommand();
  if(command.empty()) {
    ErrorMessage("Base VPR Command is empty!");
    return false;
  }

  // custom vpr command-line options for route stage only
  // it is upto the user to ensure that the options are passed in correctly.
  if( !QLSettingsManager::getStringValue("vpr", "route", "custom_vpr_options_str").empty() ) {
    // first, trim the entire string to eliminate any extra whitespace in the front and the back
    std::string vpr_custom_options_string = QLSettingsManager::getStringValue("vpr", "route", "custom_vpr_options_str");
    vpr_custom_options_string = StringUtils::trim(vpr_custom_options_string);
    // add the options string to the end of the vpr options with one whitespace separator
    command += std::string(" ") + vpr_custom_options_string;
  }

  command += std::string(" ") + 
             std::string("--route");

  std::ofstream ofs((std::filesystem::path(ProjManager()->projectPath()) /
                     std::string(ProjManager()->projectName() + "_route.cmd"))
                        .string());
  ofs << command << std::endl;
  ofs.close();
  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() + " routing failed");
    return false;
  }
  m_state = State::Routed;
  Message("Design " + ProjManager()->projectName() + " is routed");

  return true;
}

bool CompilerOpenFPGA_ql::TimingAnalysis() {
  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }

  CleanFiles(Action::STA); // this is required to remove the not actual multi corner reports left from previous run

#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED
  if (TimingAnalysisOpt() == STAOpt::Clean) {
    Message("Cleaning TimingAnalysis results for " +
            ProjManager()->projectName());
    TimingAnalysisOpt(STAOpt::None);
    m_state = State::Routed;
    CleanFiles(Action::STA);
    return true;
  }

  PERF_LOG("TimingAnalysis has started");
  Message("##################################################");
  Message("Timing Analysis for design: " + ProjManager()->projectName());
  Message("##################################################");

#ifdef _WIN32

// under WIN32, running the analysis stage alone causes issues, hence we call the
// route and analysis stages together
// hence, we can also be at Placed state here.
  // state check: requires "Placed"/"Routed" to be completed.
  // we should be *atleast* at "Placed"/"Routed" or later state.
  if( (m_state == State::Placed) ||
      (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in placed/routed state"));
    return false;
  }

#else // #ifdef _WIN32

  // state check: requires "Routed" to be completed.
  // we should be *atleast* at "Routed" or later state.
  if( (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in routed state"));
    return false;
  }

#endif // #ifdef _WIN32

#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_vprExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_vprExecutablePath.string());
    return false;
  }
#endif // #if UPSTREAM_UNUSED

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }

  // Check the STA specified device, and if it is different from the current target device
  // explicitly ask to form the base vpr command using the specific variant instead of the
  // current target device:
  // currently we only expect the p_v_t_corner to be specified in JSON, but the code
  // supports voltage_threshold also, if it is added to the JSON.
  std::map<std::string, QLDeviceTarget> devices;
  QLDeviceTarget current_device = QLDeviceManager::getInstance()->getCurrentDeviceTarget();

  auto staSuffix = [](const QLDeviceTarget& device)->std::string{
    return "_" + device.device_variant.voltage_threshold + "_" + device.device_variant.p_v_t_corner;
  };

  std::set<std::string> device_sta_vt_variants{};
  std::set<std::string> device_sta_p_v_t_corner_variants{};
  std::string sta_vpr_options = "";

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "sta_voltage_threshold").empty() ) {
    std::string sta_vt_variants_str = QLSettingsManager::getStringValue("vpr", "analysis", "sta_voltage_threshold");
    if (!sta_vt_variants_str.empty()) {
      std::vector<std::string> elements = StringUtils::tokenize(sta_vt_variants_str, ",");
      device_sta_vt_variants.insert(
          std::make_move_iterator(elements.begin()),
          std::make_move_iterator(elements.end())
      );
    }
  }

  if( !QLSettingsManager::getStringValue("vpr", "analysis", "sta_p_v_t_corner").empty() ) {
    std::string sta_p_v_t_corner_variants_str = QLSettingsManager::getStringValue("vpr", "analysis", "sta_p_v_t_corner");
    if (!sta_p_v_t_corner_variants_str.empty()) {
      std::vector<std::string> elements = StringUtils::tokenize(sta_p_v_t_corner_variants_str, ",");
      device_sta_p_v_t_corner_variants.insert(
          std::make_move_iterator(elements.begin()),
          std::make_move_iterator(elements.end())
      );
    }
  }

  if (!device_sta_p_v_t_corner_variants.empty() && device_sta_vt_variants.empty()) {
    // if pvt corner is specified but vt corner is not, then use vt configuration from the current device
    device_sta_vt_variants.insert(current_device.device_variant.voltage_threshold);
  }

  for (const std::string& device_sta_vt_variant: device_sta_vt_variants) {
    for (const std::string& device_sta_p_v_t_corner_variant: device_sta_p_v_t_corner_variants) {
      QLDeviceTarget device_sta = QLDeviceManager::getInstance()->convertToDeviceTarget(current_device.device_variant.family,
                                                                current_device.device_variant.foundry,
                                                                current_device.device_variant.node,
                                                                current_device.device_variant.devicename,
                                                                device_sta_vt_variant,
                                                                device_sta_p_v_t_corner_variant,
                                                                current_device.device_variant_layout.name);

      // Verify that the JSON value of the STA corner selection is valid
      if(!QLDeviceManager::getInstance()->isDeviceTargetValid(device_sta)) {
        // TODO: print out the options to the user to set the sta_vt and sta_p_v_t_corner in the JSON.
        // we can update the JSON options automatically too, should we do this, or ask user to do this?
        // it seems better UX to print out the json options and user can edit the JSON file, so it is 
        // not opaque to the user?
        Message("Please ensure that the userValue in Settings JSON is one of the below available\n"
                "for 'vpr > analysis > sta_p_v_t_corner':");
        QLDeviceType devicetype = QLDeviceManager::getInstance()->deviceTypeTreeElement(current_device);
        for (const QLDeviceVariant& device_variant: devicetype.device_variants) {
          Message(device_variant.p_v_t_corner);
        }
        ErrorMessage("STA Corner Device in Settings JSON is invalid!");
        return false;
      }
      devices[staSuffix(device_sta)] = device_sta;
    }
  }

  bool is_multicorner_sta_setup = !devices.empty();
  if (is_multicorner_sta_setup) {
    // As the architecture file for PnR will not match the architecture file for STA in this case,
    // vpr will fail on verifying the file hashes, so explicitly ask vpr to ignore the 
    // file hash checks.
    // example error message:
    // >> Netlist was generated from a different architecture file (loaded architecture ID: SHA256:f73c6dffee1739f500e80ed13797d3bb78fb14ef9904f06368c8c0a407205617, netlist file architecture ID: SHA256:af8742ca39cc2f748b691015adaef1561ea258f433904565b2f84e00954c9e87)
    sta_vpr_options += " --verify_file_digests off";
  } else {
    // run sta with current device
    devices[""] = current_device;
  }

  for (const auto& [sta_suffix, device]: devices) {
    if (!TimingAnalysisHelper(device, sta_vpr_options, sta_suffix)) {
      return false;
    }
  }
  return true;
}

bool CompilerOpenFPGA_ql::TimingAnalysisHelper(const QLDeviceTarget& current_device_sta, const std::string& sta_vpr_options, std::string sta_suffix)
{
  if (sta_vpr_options.empty()) {
    sta_suffix = "";
  }
  
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this, sta_suffix] {
    if (sta_suffix.empty()) {
      // Rename log file
      copyLog(ProjManager(), "vpr_stdout.log", TIMING_ANALYSIS_LOG);
    } else {
      std::string corner_timing_analysis_log = StringUtils::replaceAll(TIMING_ANALYSIS_LOG_PATTERN, "*", sta_suffix);
      copyLog(ProjManager(), "vpr_stdout.log", corner_timing_analysis_log);
      removeLog(ProjManager(), "vpr_stdout.log");

      std::string corner_report_timing_hold = StringUtils::replaceAll(TA_REPORT_TIMING_HOLD_PATTERN, "*", sta_suffix);
      copyLog(ProjManager(), TA_REPORT_TIMING_HOLD, corner_report_timing_hold);
      removeLog(ProjManager(), TA_REPORT_TIMING_HOLD);
      
      std::string corner_report_timing_setup = StringUtils::replaceAll(TA_REPORT_TIMING_SETUP_PATTERN, "*", sta_suffix);
      copyLog(ProjManager(), TA_REPORT_TIMING_SETUP, corner_report_timing_setup);
      removeLog(ProjManager(), TA_REPORT_TIMING_SETUP);
    }
  });

  std::filesystem::path sta_cmd_filepath = std::filesystem::path(ProjManager()->projectPath()) / std::string(ProjManager()->projectName() + sta_suffix + "_sta.cmd");

  if (TimingAnalysisOpt() == STAOpt::View) {

    TimingAnalysisOpt(STAOpt::None);
    
#ifdef _WIN32
    // under WIN32, running the analysis stage alone causes issues, hence we call the
    // route and analysis stages together
    std::string taCommand = BaseVprCommand() + " --route --analysis --disp on";
#else // #ifdef _WIN32
    std::string taCommand = BaseVprCommand(current_device_sta) + " --analysis --disp on";
    // Under non-WIN32(because we always add for WIN32 anyway), if the STA target device variant is different from the target 
    // device variant for PnR, **AND** flat_routing is enabled, then vpr throws an error
    // due to mismatch in switch blocks, which needs to be fixed yet.
    // https://github.com/QL-Proprietary/aurora2/issues/1267
    // Until this is fixed, we need to run the route and analysis stages together.
    if(QLDeviceManager::getInstance()->isDeviceTargetValid(current_device_sta)) {
      if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "checked" ) {
        taCommand += std::string(" --route");
      }
    }
#endif // #ifdef _WIN32

    if(!sta_vpr_options.empty()){
      taCommand += sta_vpr_options;
    }

    const int status = ExecuteAndMonitorSystemCommand(taCommand);
    if (status) {
      ErrorMessage("Design " + ProjManager()->projectName() +
                   " place and route view failed");
      return false;
    }
    return true;
  }

#if UPSTREAM_UNUSED
  if (FileUtils::IsUptoDate(
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.route"))
              .string(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_sta.cmd"))
              .string())) {
    Message("Design " + ProjManager()->projectName() + " timing didn't change");
    return true;
  }
#endif // #if UPSTREAM_UNUSED
  std::string taCommand;
  // use OpenSTA to do the job
  if (TimingAnalysisEngineOpt() == STAEngineOpt::Opensta) {
    // allows SDF to be generated for OpenSTA
    std::string command = BaseVprCommand() + " --gen_post_synthesis_netlist on";
    std::ofstream ofs(sta_cmd_filepath);
    ofs.close();
    int status = ExecuteAndMonitorSystemCommand(command);
    if (status) {
      ErrorMessage("Design " + ProjManager()->projectName() +
                   " timing analysis failed");
      return false;
    }
    // find files
    std::string libFileName =
        (std::filesystem::current_path() /
         std::string(ProjManager()->projectName() + ".lib"))
            .string();  // this is the standard sdc file
    std::string netlistFileName =
        (std::filesystem::path(ProjManager()->projectPath()) /
         std::string(ProjManager()->projectName() + "_post_synthesis.v"))
            .string();
    std::string sdfFileName =
        (std::filesystem::path(ProjManager()->projectPath()) /
         std::string(ProjManager()->projectName() + "_post_synthesis.sdf"))
            .string();
    // std::string sdcFile = ProjManager()->getConstrFiles();
    std::string sdcFileName =
        (std::filesystem::current_path() /
         std::string(ProjManager()->projectName() + ".sdc"))
            .string();  // this is the standard sdc file
    if (std::filesystem::is_regular_file(libFileName) &&
        std::filesystem::is_regular_file(netlistFileName) &&
        std::filesystem::is_regular_file(sdfFileName) &&
        std::filesystem::is_regular_file(sdcFileName)) {
      taCommand =
          BaseStaCommand() + " " +
          BaseStaScript(libFileName, netlistFileName, sdfFileName, sdcFileName);
      std::ofstream ofs(sta_cmd_filepath);
      ofs << taCommand << std::endl;
      ofs.close();
    } else {
      ErrorMessage(
          "No required design info generated for user design, required "
          "for timing analysis");
      return false;
    }
  } 
  else {
    // use vpr/tatum engine

    std::string vpr_options;
    std::string netlistFilePrefix = m_projManager->projectName() + "_post_synth";

    if( !QLSettingsManager::getStringValue("vpr", "filename", "net_file").empty() ) {
        vpr_options += std::string(" --net_file") + 
                    std::string(" ") + 
                    QLSettingsManager::getStringValue("vpr", "filename", "net_file");
    }
    else {
        vpr_options += std::string(" --net_file") + 
                    std::string(" ") + 
                    netlistFilePrefix + std::string(".net");
    }

    if( !QLSettingsManager::getStringValue("vpr", "filename", "place_file").empty() ) {
        vpr_options += std::string(" --place_file") + 
                    std::string(" ") + 
                    QLSettingsManager::getStringValue("vpr", "filename", "place_file");
    }
    else {
        vpr_options += std::string(" --place_file") + 
                    std::string(" ") + 
                    netlistFilePrefix + std::string(".place");
    }

    if( !QLSettingsManager::getStringValue("vpr", "filename", "route_file").empty() ) {
        vpr_options += std::string(" --route_file") + 
                    std::string(" ") + 
                    QLSettingsManager::getStringValue("vpr", "filename", "route_file");
    }
    else {
        vpr_options += std::string(" --route_file") + 
                    std::string(" ") + 
                    netlistFilePrefix + std::string(".route");
    }

    taCommand = BaseVprCommand(current_device_sta);
    if(taCommand.empty()) {
        ErrorMessage("Base VPR Command is empty!");
        return false;
    }

    // custom vpr command-line options for analysis stage
    // it is upto the user to ensure that the options are passed in correctly.
    if( !QLSettingsManager::getStringValue("vpr", "analysis", "custom_vpr_options_str").empty() ) {
      // first, trim the entire string to eliminate any extra whitespace in the front and the back
      std::string vpr_custom_options_string = QLSettingsManager::getStringValue("vpr", "analysis", "custom_vpr_options_str");
      vpr_custom_options_string = StringUtils::trim(vpr_custom_options_string);
      // add the options string to the end of the vpr options with one whitespace separator
      vpr_options += std::string(" ") + vpr_custom_options_string;
    }

    taCommand += vpr_options;

    if(!sta_vpr_options.empty()){
      taCommand += sta_vpr_options;
    }
    
#ifdef _WIN32

    // under WIN32, running the analysis stage along causes issues, hence we call the
    // route and analysis stages together
    taCommand += std::string(" --route");

#else // #ifdef _WIN32

    // Under non-WIN32(because we always add for WIN32 anyway), if the STA target device variant is different from the target 
    // device variant for PnR, **AND** flat_routing is enabled, then vpr throws an error
    // due to mismatch in switch blocks, which needs to be fixed yet.
    // https://github.com/QL-Proprietary/aurora2/issues/1267
    // Until this is fixed, we need to run the route and analysis stages together.
    if(QLDeviceManager::getInstance()->isDeviceTargetValid(current_device_sta)) {
      if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "checked" ) {
        taCommand += std::string(" --route");
      }
    }

#endif // #ifdef _WIN32

    taCommand += std::string(" --analysis");

    std::ofstream ofs(sta_cmd_filepath);
    ofs << taCommand << std::endl;
    ofs.close();
  }

  int status = ExecuteAndMonitorSystemCommand(taCommand);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
                 " timing analysis failed");
    return false;
  }

  Message("Design " + ProjManager()->projectName() + " is timing analysed");

#ifdef _WIN32
// under WIN32, running the analysis stage along causes issues, hence we call the
// route and analysis stages together
// hence, we set the state here, so that just sta can be called instead of route and sta as well.
  m_state = State::Routed;
#endif // #ifdef _WIN32

  return true;
}

bool CompilerOpenFPGA_ql::PowerAnalysis() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  
#if 0 // Disable VPR Power Analysis
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), "vpr_stdout.log", POWER_ANALYSIS_LOG);
  });
#endif // Disable VPR Power Analysis

  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
#endif // #if UPSTREAM_UNUSED
  if (PowerAnalysisOpt() == PowerOpt::Clean) {
    Message("Cleaning PowerAnalysis results for " +
            ProjManager()->projectName());
    PowerAnalysisOpt(PowerOpt::None);
    m_state = State::Routed;
    CleanFiles(Action::Power);
    return true;
  }

  PERF_LOG("PowerAnalysis has started");
#ifdef _WIN32

// under WIN32, running the analysis stage alone causes issues, hence we call the
// route and analysis stages together
// hence, we can also be at Placed state here.
  // state check: requires "Placed"/"Routed" to be completed.
  // we should be *atleast* at "Placed"/"Routed" or later state.
  if( (m_state == State::Placed) ||
      (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in placed/routed state"));
    return false;
  }

#else // #ifdef _WIN32

  // state check: requires "Routed" to be completed.
  // we should be *atleast* at "Routed" or later state.
  if( (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in routed state"));
    return false;
  }

#endif // #ifdef _WIN32
  Message("##################################################");
  Message("Power Analysis for design: " + ProjManager()->projectName());
  Message("##################################################");

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }


#if 0 // Disable VPR Power Analysis

#if UPSTREAM_UNUSED
  if (FileUtils::IsUptoDate(
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.route"))
              .string(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_sta.cmd"))
              .string())) {
    Message("Design " + ProjManager()->projectName() + " power didn't change");
    return true;
  }
#endif // #if UPSTREAM_UNUSED

#if UPSTREAM_UNUSED
  std::string command = BaseVprCommand() + " --analysis";
  if (!FileUtils::FileExists(m_vprExecutablePath)) {
    ErrorMessage("Cannot find executable: " + m_vprExecutablePath.string());
       return false;
     }
#endif // #if UPSTREAM_UNUSED
  
  std::string vpr_options;
  std::string netlistFilePrefix = m_projManager->projectName() + "_post_synth";

  if( !QLSettingsManager::getStringValue("vpr", "filename", "net_file").empty() ) {
      vpr_options += std::string(" --net_file") + 
                  std::string(" ") + 
                  QLSettingsManager::getStringValue("vpr", "filename", "net_file");
  }
  else {
      vpr_options += std::string(" --net_file") + 
                  std::string(" ") + 
                  netlistFilePrefix + std::string(".net");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "place_file").empty() ) {
      vpr_options += std::string(" --place_file") + 
                  std::string(" ") + 
                  QLSettingsManager::getStringValue("vpr", "filename", "place_file");
  }
  else {
      vpr_options += std::string(" --place_file") + 
                  std::string(" ") + 
                  netlistFilePrefix + std::string(".place");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "route_file").empty() ) {
      vpr_options += std::string(" --route_file") + 
                  std::string(" ") + 
                  QLSettingsManager::getStringValue("vpr", "filename", "route_file");
  }
  else {
      vpr_options += std::string(" --route_file") + 
                  std::string(" ") + 
                  netlistFilePrefix + std::string(".route");
  }

  std::string command = BaseVprCommand();
  if(command.empty()) {
    ErrorMessage("Base VPR Command is empty!");
    return false;
  }
  command += vpr_options +
#ifdef _WIN32
// under WIN32, running the analysis stage alone causes issues, hence we call the
// route and analysis stages together
             std::string(" ") + 
             std::string("--route") +
#endif // #ifdef _WIN32
             std::string(" ") + 
             std::string("--analysis");


  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
                 " power analysis failed");
    return false;
  }

#endif // Disable VPR Power Analysis

  long double power_dynamic_mW = PowerEstimator_Dynamic();
  long double power_leakage_mW = PowerEstimator_Leakage();
  long double power_total_mW = power_dynamic_mW + power_leakage_mW;

  if(power_dynamic_mW != 0 && power_leakage_mW != 0 && power_total_mW != 0) {

    // write power analysis to console
    Message("\n# ====== Power Analysis Report ======\n");
    Message(">> Dynamic Power   =   " + std::to_string(power_dynamic_mW) + " mW");
    Message(">> Leakage Power   =   " + std::to_string(power_leakage_mW) + " mW");
    Message(">> Total Power     =   " + std::to_string(power_total_mW) + " mW");
    Message("\n# ===================================\n");

    // write power analysis into file
    std::filesystem::path power_analysis_rpt_filepath = 
      std::filesystem::path(ProjManager()->projectPath()) / POWER_ANALYSIS_LOG;
    std::ofstream power_analysis_rpt;
    power_analysis_rpt.open(power_analysis_rpt_filepath);
    if(!power_analysis_rpt) {
      ErrorMessage("File: " + power_analysis_rpt_filepath.string() + " could not be opened");
      return false;
    }
    power_analysis_rpt << "\n# ====== Power Analysis Report ======\n" << std::endl;
    power_analysis_rpt << "Dynamic Power  =   " << std::to_string(power_dynamic_mW) << " mW" << std::endl;
    power_analysis_rpt << "Leakage Power  =   " << std::to_string(power_leakage_mW) << " mW" << std::endl;
    power_analysis_rpt << "Total Power    =   " << std::to_string(power_total_mW) << " mW" << std::endl;
    power_analysis_rpt << "\n# ===================================\n" << std::endl;
    power_analysis_rpt.close();
  }

  Message("Design " + ProjManager()->projectName() + " is power analysed");
  return true;
}

const std::string basicOpenFPGABitstreamScript = R"( 
vpr ${VPR_ARCH_FILE} ${VPR_TESTBENCH_BLIF} --clock_modeling ideal${OPENFPGA_VPR_DEVICE_LAYOUT} --net_file ${NET_FILE} --place_file ${PLACE_FILE} --route_file ${ROUTE_FILE} --route_chan_width ${OPENFPGA_VPR_ROUTE_CHAN_WIDTH} --sdc_file ${SDC_FILE} --absorb_buffer_luts off --constant_net_method route --skip_sync_clustering_and_routing_results on --circuit_format ${OPENFPGA_VPR_CIRCUIT_FORMAT} --analysis ${PNR_OPTIONS}

# Read OpenFPGA architecture definition
read_openfpga_arch -f ${OPENFPGA_ARCH_FILE}

# Read OpenFPGA simulation settings
read_openfpga_simulation_setting -f ${OPENFPGA_SIM_SETTING_FILE}

read_openfpga_bitstream_setting -f ${OPENFPGA_BITSTREAM_SETTING_FILE}

# Annotate the OpenFPGA architecture to VPR data base
# to debug use --verbose options
link_openfpga_arch --sort_gsb_chan_node_in_edges 

${PB_PIN_FIXUP}

# Apply fix-up to Look-Up Table truth tables based on packing results
lut_truth_table_fixup

# Build the module graph
#  - Enabled compression on routing architecture modules
#  - Enable pin duplication on grid modules
build_fabric --frame_view --compress_routing --duplicate_grid_pin ${OPENFPGA_BUILD_FABRIC_OPTION}

# Repack the netlist to physical pbs
# This must be done before bitstream generator and testbench generation
# Strongly recommend it is done after all the fix-up have been applied
repack --design_constraints ${OPENFPGA_REPACK_CONSTRAINTS}

build_architecture_bitstream

build_fabric_bitstream
write_fabric_bitstream --format plain_text --file fabric_bitstream.bit
write_io_mapping -f PinMapping.xml

# Finish and exit OpenFPGA
exit

)";

const std::string simulationOpenFPGABitstreamScript = R"( 
vpr ${VPR_ARCH_FILE} ${VPR_TESTBENCH_BLIF} --clock_modeling ideal${OPENFPGA_VPR_DEVICE_LAYOUT} --net_file ${NET_FILE} --place_file ${PLACE_FILE} --route_file ${ROUTE_FILE} --route_chan_width ${OPENFPGA_VPR_ROUTE_CHAN_WIDTH} --sdc_file ${SDC_FILE} --absorb_buffer_luts off --constant_net_method route --skip_sync_clustering_and_routing_results on --circuit_format ${OPENFPGA_VPR_CIRCUIT_FORMAT} --analysis ${PNR_OPTIONS}

# Read OpenFPGA architecture definition
read_openfpga_arch -f ${OPENFPGA_ARCH_FILE}

# Read OpenFPGA simulation settings
read_openfpga_simulation_setting -f ${OPENFPGA_SIM_SETTING_FILE}

read_openfpga_bitstream_setting -f ${OPENFPGA_BITSTREAM_SETTING_FILE}

# Annotate the OpenFPGA architecture to VPR data base
# to debug use --verbose options
link_openfpga_arch --sort_gsb_chan_node_in_edges 

${PB_PIN_FIXUP}

# Apply fix-up to Look-Up Table truth tables based on packing results
lut_truth_table_fixup

# Build the module graph
#  - Enabled compression on routing architecture modules
#  - Enable pin duplication on grid modules
build_fabric --frame_view --compress_routing --duplicate_grid_pin ${OPENFPGA_BUILD_FABRIC_OPTION}

# Repack the netlist to physical pbs
# This must be done before bitstream generator and testbench generation
# Strongly recommend it is done after all the fix-up have been applied
repack --design_constraints ${OPENFPGA_REPACK_CONSTRAINTS}

build_architecture_bitstream --verbose \
                             --write_file fabric_independent_bitstream.xml
 
build_fabric_bitstream --verbose 

write_fabric_verilog --file BIT_SIM \
                     --explicit_port_mapping \
                     --include_timing \
                     --print_user_defined_template \
                     --verbose

write_fabric_bitstream --format plain_text --file fabric_bitstream.bit

write_fabric_bitstream --format xml --file fabric_bitstream.xml

write_full_testbench --file BIT_SIM \
                     --bitstream fabric_bitstream.bit 

write_preconfigured_fabric_wrapper --file BIT_SIM --embed_bitstream iverilog

write_preconfigured_testbench --file BIT_SIM

write_io_mapping -f PinMapping.xml

# Finish and exit OpenFPGA
exit

)";

const std::string qlOpenFPGABitstreamScript = R"(
# openfpga (internal) template script for Aurora

# refer:
# 1. https://openfpga.readthedocs.io/en/latest/manual/openfpga_shell/openfpga_script/
# 2. https://openfpga.readthedocs.io/en/latest/manual/openfpga_shell/openfpga_commands/

# we need to run the vpr analysis command before openfpga process can start.
# don't edit this:
${VPR_ANALYSIS_COMMAND}

# Read OpenFPGA architecture definition
read_openfpga_arch -f ${OPENFPGA_ARCH_FILE}

# Read OpenFPGA simulation setting
read_openfpga_simulation_setting -f ${OPENFPGA_SIM_SETTING_FILE}

# Read OpenFPGA bitstream setting
${READ_OPENFPGA_BITSTREAM_SETTING_COMMAND}

# Annotate the OpenFPGA architecture to VPR data base
# to debug add '--verbose'
# to specify activity file, add '--activity_file ${ACTIVITY_FILE}'
link_openfpga_arch --sort_gsb_chan_node_in_edges

# Apply fix-up to Look-Up Table truth tables based on packing results
lut_truth_table_fixup

# Build the module graph
# - Enabled compression on routing architecture modules with '--compress_routing'
# - Enable pin duplication on grid modules with '--duplicate_grid_pin'
# - Create only frame views of the module graph to make it run faster with '--frame_view'
build_fabric --compress_routing --duplicate_grid_pin --frame_view ${OPENFPGA_BUILD_FABRIC_OPTION}

# Dump GSB data
# Necessary for creation of rr graph for SymbiFlow
write_gsb_to_xml --file gsb

# Repack the netlist to physical pbs
# This must be done before bitstream generator and testbench generation
# Strongly recommend it is done after all the fix-up have been applied
# to use repack design contraints, add '--design_constraints ${OPENFPGA_REPACK_CONSTRAINTS}'
repack

# Build bitstream database and save to file
build_architecture_bitstream --write_file fabric_independent_bitstream.xml

# Build fabric bitstream
build_fabric_bitstream

# Write fabric bitstream
write_fabric_bitstream --format plain_text --file fabric_bitstream.bit

# Write fabric bitstream xml format
write_fabric_bitstream --format xml --file fabric_bitstream.xml

# Write io mapping 
write_io_mapping -f PinMapping.xml

${OPENFPGA_WRITE_FABRIC_IO_INFO_COMMAND}

# Write the SDC files for PnR backend
#write_pnr_sdc --time_unit ns --flatten_names --file ./SDC
#write_pnr_sdc --time_unit ns --flatten_names --hierarchical --file ./SDC_leaf

# Finish and exit OpenFPGA
exit
)";

const std::string qlOpenFPGApcf2placeScript = R"(
${OPENFPGA_PCF2PLACE_COMMAND}
# Finish and exit OpenFPGA
exit
)";

std::string CompilerOpenFPGA_ql::InitOpenFPGAScript() {
  // Default or custom OpenFPGA script
  if (m_openFPGAScript.empty()) {
#if UPSTREAM_UNUSED
    if (BitsOpt() == BitstreamOpt::EnableSimulation) {
      m_openFPGAScript = simulationOpenFPGABitstreamScript;
    } else {
    m_openFPGAScript = basicOpenFPGABitstreamScript;
	}
#endif // #if UPSTREAM_UNUSED

    bool use_external_template_openfpga = false;
    std::string aurora_template_script_openfpga;

    // check if we have the device aurora template script available:
    if(FileUtils::FileExists(m_aurora_template_script_openfpga_path)) {
        
      // get it into a ifstream
      std::ifstream stream(m_aurora_template_script_openfpga_path.string());
        
      if (stream.good()) {
        aurora_template_script_openfpga = 
          std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
          stream.close();
          use_external_template_openfpga = true;
        }
    }

    if(use_external_template_openfpga) {
      Message("Using External OpenFPGA Template Script: " +
                                std::string(m_aurora_template_script_openfpga_path.string()));
      m_openFPGAScript = aurora_template_script_openfpga;
    }
    else {
      Message("Cannot load OpenFPGA Template Script: " +
                                std::string(m_aurora_template_script_openfpga_path.string()));
      Message("Using Internal OpenFPGA Template Script.");
      m_openFPGAScript = qlOpenFPGABitstreamScript;
    }
  }
  return m_openFPGAScript;
}

std::string CompilerOpenFPGA_ql::FinishOpenFPGAScript(const std::string& script) {

  std::string result = script;

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return std::string("");
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return std::string("");
  }


  QLDeviceTarget device_target = QLDeviceManager::getInstance()->getCurrentDeviceTarget();

  std::error_code ec;


  // [required] openfpga architecture file
  m_OpenFpgaArchitectureFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGAArchitectureFile();
  if(m_OpenFpgaArchitectureFile.empty()) {

    ErrorMessage("Cannot proceed without OpenFPGA Architecture file.");
    return std::string("");
  }

  if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaArchitectureFile)) {
    
    std::filesystem::path openfpga_xml_en_path = m_OpenFpgaArchitectureFile;
    m_OpenFpgaArchitectureFile = GenerateTempFilePath();

    m_cryptdbPath = 
        CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                           QLDeviceManager::getInstance()->convertToDeviceTypeString());

    if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
      Message("load cryptdb failed!");
      // empty string returned on error.
      return std::string("");
    }

    if (!CRFileCryptProc::getInstance()->decryptFile(openfpga_xml_en_path, m_OpenFpgaArchitectureFile)) {
      ErrorMessage("decryption failed!");
      // empty string returned on error.
      return std::string("");
    }
  }


  // [required] bitstream annotation file
  m_OpenFpgaBitstreamSettingFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGABitstreamAnnotationFile();
  if(m_OpenFpgaBitstreamSettingFile.empty()) {

    ErrorMessage("Cannot proceed without bitstream annotation file.");
    return std::string("");
  }

  if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaBitstreamSettingFile)) {
    
    std::filesystem::path bitstream_annotation_xml_en_path = m_OpenFpgaBitstreamSettingFile;
    m_OpenFpgaBitstreamSettingFile = GenerateTempFilePath();

    m_cryptdbPath = 
        CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                           QLDeviceManager::getInstance()->convertToDeviceTypeString());

    if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
      Message("load cryptdb failed!");
      // empty string returned on error.
      return std::string("");
    }

    if (!CRFileCryptProc::getInstance()->decryptFile(bitstream_annotation_xml_en_path, m_OpenFpgaBitstreamSettingFile)) {
      ErrorMessage("decryption failed!");
      // empty string returned on error.
      return std::string("");
    }
  }


  // [optional] repack design contraint file
  m_OpenFpgaRepackConstraintsFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGARepackDesignConstraintFile();
  if(m_OpenFpgaRepackConstraintsFile.empty()) {

    Message("Proceeding without user provided repack design contraint file.");
  }
  else {

    if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaRepackConstraintsFile)) {
      
      std::filesystem::path repack_design_contraint_xml_en_path = m_OpenFpgaRepackConstraintsFile;
      m_OpenFpgaRepackConstraintsFile = GenerateTempFilePath();

      m_cryptdbPath = 
          CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                            QLDeviceManager::getInstance()->convertToDeviceTypeString());

      if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
        Message("load cryptdb failed!");
        // empty string returned on error.
        return std::string("");
      }

      if (!CRFileCryptProc::getInstance()->decryptFile(repack_design_contraint_xml_en_path, m_OpenFpgaRepackConstraintsFile)) {
        ErrorMessage("decryption failed!");
        // empty string returned on error.
        return std::string("");
      }
    }
  }


  // [required] fixed sim file
  m_OpenFpgaSimSettingFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGAFixedSimFile();
  if(m_OpenFpgaSimSettingFile.empty()) {

    ErrorMessage("Cannot proceed without fixed sim file.");
    return std::string("");
  }

  if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaSimSettingFile)) {
    
    std::filesystem::path fixed_sim_openfpga_xml_en_path = m_OpenFpgaSimSettingFile;
    m_OpenFpgaSimSettingFile = GenerateTempFilePath();

    m_cryptdbPath = 
        CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                           QLDeviceManager::getInstance()->convertToDeviceTypeString());

    if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
      Message("load cryptdb failed!");
      // empty string returned on error.
      return std::string("");
    }

    if (!CRFileCryptProc::getInstance()->decryptFile(fixed_sim_openfpga_xml_en_path, m_OpenFpgaSimSettingFile)) {
      ErrorMessage("decryption failed!");
      // empty string returned on error.
      return std::string("");
    }
  }


  // [optional] fabric key file
  m_OpenFpgaFabricKeyFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGAFabricKeyFile();

  if(!m_OpenFpgaFabricKeyFile.empty()) {

      if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaFabricKeyFile)) {
      
      std::filesystem::path fabric_key_xml_en_path = m_OpenFpgaFabricKeyFile;
      m_OpenFpgaFabricKeyFile = GenerateTempFilePath();

      m_cryptdbPath = 
          CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                            QLDeviceManager::getInstance()->convertToDeviceTypeString());

      if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
        Message("load cryptdb failed!");
        // empty string returned on error.
        return std::string("");
      }

      if (!CRFileCryptProc::getInstance()->decryptFile(fabric_key_xml_en_path, m_OpenFpgaFabricKeyFile)) {
        ErrorMessage("decryption failed!");
        // empty string returned on error.
        return std::string("");
      }
    }
  }


  // [optional] bitstream remapping file
  m_OpenFpgaBitstreamRemappingFile = 
      QLDeviceManager::getInstance()->deviceOpenFPGABitstreamRemappingFile();

  if(!m_OpenFpgaBitstreamRemappingFile.empty()) {

      if(QLDeviceManager::getInstance()->deviceFileIsEncrypted(m_OpenFpgaBitstreamRemappingFile)) {
      
      std::filesystem::path bitstream_remapping_xml_en_path = m_OpenFpgaBitstreamRemappingFile;
      m_OpenFpgaBitstreamRemappingFile = GenerateTempFilePath();

      m_cryptdbPath = 
          CRFileCryptProc::getInstance()->getCryptDBFileName((QLDeviceManager::getInstance()->deviceTypeDirPath()).string(),
                                                            QLDeviceManager::getInstance()->convertToDeviceTypeString());

      if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
        Message("load cryptdb failed!");
        // empty string returned on error.
        return std::string("");
      }

      if (!CRFileCryptProc::getInstance()->decryptFile(bitstream_remapping_xml_en_path, m_OpenFpgaBitstreamRemappingFile)) {
        ErrorMessage("decryption failed!");
        // empty string returned on error.
        return std::string("");
      }
    }
  }


  Message( std::string("Using openfpga.xml for: ") + QLDeviceManager::getInstance()->getCurrentDeviceTargetString() );

  // call vpr to execute analysis
  std::string vpr_options;
  std::string netlistFilePrefix = ProjManager()->projectName() + "_post_synth";

  if( !QLSettingsManager::getStringValue("vpr", "filename", "net_file").empty() ) {
    vpr_options += std::string(" --net_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "net_file");
  }
  else {
    vpr_options += std::string(" --net_file") + 
                   std::string(" ") + 
                   netlistFilePrefix + std::string(".net");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "place_file").empty() ) {
    vpr_options += std::string(" --place_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "place_file");
  }
  else {
    vpr_options += std::string(" --place_file") + 
                   std::string(" ") + 
                   netlistFilePrefix + std::string(".place");
  }

  if( !QLSettingsManager::getStringValue("vpr", "filename", "route_file").empty() ) {
    vpr_options += std::string(" --route_file") + 
                   std::string(" ") + 
                   QLSettingsManager::getStringValue("vpr", "filename", "route_file");
  }
  else {
    vpr_options += std::string(" --route_file") + 
                   std::string(" ") + 
                   netlistFilePrefix + std::string(".route");
  }

  std::string vpr_analysis_command = BaseVprCommand();
  if(vpr_analysis_command.empty()) {
    ErrorMessage("Base VPR Command is empty!");
    // empty string returned on error.
    return std::string("");
  }
  vpr_analysis_command += vpr_options +
#ifdef _WIN32
// under WIN32, running the analysis stage along causes issues, hence we call the
// route and analysis stages together
                          std::string(" ") + 
                          std::string("--route") +
#endif // #ifdef _WIN32
                          std::string(" ") + 
                          std::string("--analysis");

  result = ReplaceAll(result, "${VPR_ANALYSIS_COMMAND}", vpr_analysis_command);

  //std::string netlistFilePrefix = m_projManager->projectName() + "_post_synth";

  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF: {
        std::filesystem::path the_path = lang_file.second;
        std::filesystem::path filename = the_path.filename();
        std::filesystem::path stem = filename.stem();
        netlistFilePrefix = stem.string();
        break;
      }
      default:
        break;
    }
  }

  result = ReplaceAll(result, "${VPR_ARCH_FILE}", m_architectureFile.string());
  result = ReplaceAll(result, "${NET_FILE}", netlistFilePrefix + ".net");
  result = ReplaceAll(result, "${PLACE_FILE}", netlistFilePrefix + ".place");
  result = ReplaceAll(result, "${ROUTE_FILE}", netlistFilePrefix + ".route");
  result = ReplaceAll(result, "${SDC_FILE}",
                      ProjManager()->projectName() + "_openfpga.sdc");

  std::string pnrOptions;
  if (!PnROpt().empty()) pnrOptions += " " + PnROpt();
  if (!PerDevicePnROptions().empty()) pnrOptions += " " + PerDevicePnROptions();
  result = ReplaceAll(result, "${PNR_OPTIONS}", pnrOptions);
  std::string netlistFile;
  switch (GetNetlistType()) {
    case NetlistType::Verilog:
      netlistFile = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::VHDL:
      // Until we have a VHDL netlist reader in VPR
      netlistFile = ProjManager()->projectName() + "_post_synth.v";
      break;
    case NetlistType::Edif:
      netlistFile = ProjManager()->projectName() + "_post_synth.edif";
      break;
    case NetlistType::Blif:
      netlistFile = ProjManager()->projectName() + "_post_synth.blif";
      break;
  }
  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF: {
        netlistFile = lang_file.second;
        std::filesystem::path the_path = netlistFile;
        if (!the_path.is_absolute()) {
          netlistFile =
              std::filesystem::path(std::filesystem::path("..") / netlistFile)
                  .string();
        }
        break;
      }
      default:
        break;
    }
  }
  result = ReplaceAll(result, "${VPR_TESTBENCH_BLIF}", netlistFile);

  std::string netlistFormat;
  switch (GetNetlistType()) {
    case NetlistType::Verilog:
      netlistFormat = "verilog";
      break;
    case NetlistType::VHDL:
      // Until we have a VHDL netlist reader in VPR
      netlistFormat = "verilog";
      break;
    case NetlistType::Edif:
      netlistFormat = "edif";
      break;
    case NetlistType::Blif:
      netlistFormat = "blif";
      break;
  }

  result = ReplaceAll(result, "${OPENFPGA_VPR_CIRCUIT_FORMAT}", netlistFormat);
  if (m_deviceSize.size()) {
    result = ReplaceAll(result, "${OPENFPGA_VPR_DEVICE_LAYOUT}",
                        " --device " + m_deviceSize);
  } else {
    result = ReplaceAll(result, "${OPENFPGA_VPR_DEVICE_LAYOUT}", "");
  }

  result = ReplaceAll(result, "${OPENFPGA_VPR_ROUTE_CHAN_WIDTH}",
                      std::to_string(m_channel_width));

  result = ReplaceAll(result, "${OPENFPGA_ARCH_FILE}",
                      m_OpenFpgaArchitectureFile.string());

  result = ReplaceAll(result, "${OPENFPGA_SIM_SETTING_FILE}",
                      m_OpenFpgaSimSettingFile.string());

  result = ReplaceAll(result, "${PB_PIN_FIXUP}", m_pb_pin_fixup);

  // optional, so only if this file is available, else blank command.
  std::string read_openfpga_bitstream_setting_command = "#skipped";
  if(!m_OpenFpgaBitstreamSettingFile.empty()) {
    // read_openfpga_bitstream_setting -f ${OPENFPGA_BITSTREAM_SETTING_FILE}
    read_openfpga_bitstream_setting_command = 
        std::string("read_openfpga_bitstream_setting -f ") + 
        m_OpenFpgaBitstreamSettingFile.string();
  }
  result = ReplaceAll(result, "${READ_OPENFPGA_BITSTREAM_SETTING_COMMAND}",
                      read_openfpga_bitstream_setting_command);

  // repack constraints
  // 1. pass in the PCF file, if available with '--pcf'
  // 2. pass in the user provided repack design constraint xml if available with '--design_constraints'
  // 3. pass in option '--write_design_constraints' to dump constraints to verify
  std::string openfpga_repack_constraints_command = "repack";
  std::filesystem::path filepath_pcf = QLSettingsManager::getInstance()->getPCFFilePath();
  if(!filepath_pcf.empty()) {
    openfpga_repack_constraints_command += 
        " --pcf " + filepath_pcf.string();
  }
  if(!m_OpenFpgaRepackConstraintsFile.empty()) {
    openfpga_repack_constraints_command += 
        " --design_constraints " + m_OpenFpgaRepackConstraintsFile.string();
  }
  std::string generated_repack_design_constraint_filename =
      "repack_design_constraint_generated.xml";
  openfpga_repack_constraints_command += 
      " --write_design_constraints " + generated_repack_design_constraint_filename;
  result = ReplaceAll(result, "${OPENFPGA_REPACK_CONSTRAINTS_COMMAND}",
                      openfpga_repack_constraints_command);

  // fabric_key is optional
  if (m_OpenFpgaFabricKeyFile.empty()) {
    result = ReplaceAll(result, "${OPENFPGA_BUILD_FABRIC_OPTION}", "");
  } else {
    result =
        ReplaceAll(result, "${OPENFPGA_BUILD_FABRIC_OPTION}",
                   "--load_fabric_key " + m_OpenFpgaFabricKeyFile.string());
  }

  // bitstream_remapping is optional. and if it exists:
  // build_reordered_fabric_bitstream --reorder_map bitstream_remapping.xml --file reordered_bitstream.bin
  // write_fabric_bitstream --reorder --format plain_text --file fabric_bitstream.bit
  // write_fabric_bitstream --reorder --format xml --file fabric_bitstream.xml
  if (m_OpenFpgaBitstreamRemappingFile.empty()) {
    result = ReplaceAll(result, "${OPENFPGA_BUILD_REORDERED_FABRIC_BITSTREAM_COMMAND}", std::string("#skipped"));
    result = ReplaceAll(result, "${OPENFPGA_WRITE_BITSTREAM_PLAINTEXT_COMMAND}",
                                std::string("write_fabric_bitstream --format plain_text --file fabric_bitstream.bit"));
    result = ReplaceAll(result, "${OPENFPGA_WRITE_BITSTREAM_XML_COMMAND}",
                                std::string("write_fabric_bitstream --format xml --file fabric_bitstream.xml"));
  } else {
    result =
        ReplaceAll(result, "${OPENFPGA_BUILD_REORDERED_FABRIC_BITSTREAM_COMMAND}",
                   std::string("build_reordered_fabric_bitstream --reorder_map ") + m_OpenFpgaBitstreamRemappingFile.string() +
                   std::string(" --file reordered_bitstream.bin"));
        result = ReplaceAll(result, "${OPENFPGA_WRITE_BITSTREAM_PLAINTEXT_COMMAND}", 
                                    std::string("write_fabric_bitstream --reorder --format plain_text --file fabric_bitstream.bit"));
        result = ReplaceAll(result, "${OPENFPGA_WRITE_BITSTREAM_XML_COMMAND}", 
                                    std::string("write_fabric_bitstream --reorder --format xml --file fabric_bitstream.xml"));
  }


  // call openfpga to output the fpga_io_map XML file *always*
  // write_fabric_io_info --file ${OPENFPGA_IO_MAP_FILE} --verbose
  std::string openfpga_write_fabric_io_info_command;
  // fpga_io_map
  std::filesystem::path filepath_fpga_io_map_xml;
  // form the file name using the current device: family_foundry_node
  filepath_fpga_io_map_xml = std::string("fpga_io_map") + std::string(".xml");
  // generate the fpga_io_map file in the generated 'working_directory', not in the 'design_directory'
  // so the below part of code is commented out.
  // if (!filepath_fpga_io_map_xml.is_absolute()) {
  //   filepath_fpga_io_map_xml = std::filesystem::path(std::filesystem::path("..") / filepath_fpga_io_map_xml);
  // }
  openfpga_write_fabric_io_info_command = std::string("write_fabric_io_info") +
                                          std::string(" --file") +
                                          std::string(" ") + filepath_fpga_io_map_xml.string();
  openfpga_write_fabric_io_info_command += std::string(" --verbose");
  result = ReplaceAll(result, "${OPENFPGA_WRITE_FABRIC_IO_INFO_COMMAND}", openfpga_write_fabric_io_info_command);

  return result;
}

bool CompilerOpenFPGA_ql::GenerateBitstream() {
  // Using a Scope Guard so this will fire even if we exit mid function
  // This will fire when the containing function goes out of scope
  auto guard = sg::make_scope_guard([this] {
    // Rename log file
    copyLog(ProjManager(), "vpr_stdout.log", BITSTREAM_LOG);
  });

  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
#if UPSTREAM_UNUSED
  if (!HasTargetDevice()) return false;
  const bool openFpgaArch = FileUtils::FileExists(m_OpenFpgaArchitectureFile);
  if (!openFpgaArch) {
    ErrorMessage("Please specify OpenFPGA architecture file");
    return false;
  }
#endif // #if UPSTREAM_UNUSED
  if (BitsOpt() == BitstreamOpt::Clean) {
    Message("Cleaning bitstream results for " + ProjManager()->projectName());
    m_state = State::Routed;
    BitsOpt(BitstreamOpt::DefaultBitsOpt);
    CleanFiles(Action::Bitstream);
    return true;
  }
#if UPSTREAM_UNUSED
  if (!QLDeviceManager::getInstance()->getCurrentDeviceTargetString().empty()) {
    if (!LicenseDevice(QLDeviceManager::getInstance()->getCurrentDeviceTargetString())) {
      ErrorMessage(
          "Device is not licensed: " + QLDeviceManager::getInstance()->getCurrentDeviceTargetString() + "\n");
      return false;
    }
  }
#endif // #if UPSTREAM_UNUSED
  PERF_LOG("GenerateBitstream has started");
  // state check: requires "Routed" to be completed.
  // we should be *atleast* at "Routed" or later state.
  if( (m_state == State::Routed) ||
      (m_state == State::TimingAnalyzed) ||
      (m_state == State::PowerAnalyzed) ||
      (m_state == State::BistreamGenerated) ) {
  }
  else {
    ErrorMessage(std::string(__func__) + std::string("(): Design needs to be *atleast* in routed state"));
    return false;
  }
  Message("##################################################");
  Message("Bitstream generation for design \"" + ProjManager()->projectName() +
          "\" on device \"" + QLDeviceManager::getInstance()->getCurrentDeviceTargetString() + "\"");
  Message("##################################################");

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }

  if(QLSettingsManager::getStringValue("general", "device", "foundry") == "GF" &&
     QLSettingsManager::getStringValue("general", "device", "node") == "12nm") {

    // add an internal check, if we really want to enable the bitstream generation
    // for gf12 devices: if the feature enable file exists, we honor it.
    std::string enable_bitstream_file_name = "bitstream_enable.au";
    std::filesystem::path enable_bitstream_file_path = 
        QLDeviceManager::getInstance()->deviceTypeDirPath() /
        enable_bitstream_file_name;
    if (FileUtils::FileExists(enable_bitstream_file_path)) {
      // continue with the bitstream generation, as internal
      // feature enable file exists.
    }
    else {
      Message("##################################################");
      Message("Skipping Bitstream Generation for GF 12nm devices!");
      Message("##################################################");
      return true;
    }
  }

  if( QLSettingsManager::getStringValue("openfpga", "general", "bitstream_generation") == "checked" ) {
    // bitstream generation is enabled, we can continue.
  }
  else {
    Message("##################################################");
    Message("Skipping Bitstream Generation since it is not enabled!");
    Message("##################################################");
    return true;
  }

  // if flat_routing is enabled in VPR, skip bitstream generation
  // OpenFPGA does not support bitstream generation with flat_routing (fully, yet)
  // ref: https://github.com/verilog-to-routing/vtr-verilog-to-routing/issues/2256#issuecomment-1498007179
  if( QLSettingsManager::getStringValue("vpr", "route", "flat_routing") == "checked" ) {
    // add an internal check, if we really want to enable the bitstream generation
    // with flatrouter: if the feature enable file exists, we honor it.
    std::string enable_bitstream_file_name = "flatrouting_bitstream_enable.au";
    std::filesystem::path enable_bitstream_file_path = 
        QLDeviceManager::getInstance()->deviceTypeDirPath() /
        enable_bitstream_file_name;
    if (FileUtils::FileExists(enable_bitstream_file_path)) {
      // continue with the bitstream generation, as internal
      // feature enable file exists.
    }
    else {
      Message("##################################################");
      Message("Skipping Bitstream Generation since flat_routing is enabled in VPR!");
      Message("##################################################");
      return true;
    }
  }

#if UPSTREAM_UNUSED
  if (BitsOpt() == BitstreamOpt::EnableSimulation) {
    std::filesystem::path bit_path =
        std::filesystem::path(ProjManager()->projectPath()) / "BIT_SIM";
    std::filesystem::create_directory(bit_path);
  }

  if (FileUtils::IsUptoDate(
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string(ProjManager()->projectName() + "_post_synth.route"))
              .string(),
          (std::filesystem::path(ProjManager()->projectPath()) /
           std::string("fabric_bitstream.bit"))
              .string())) {
    Message("Design " + ProjManager()->projectName() +
            " bitstream didn't change");
    m_state = State::BistreamGenerated;
    return true;
  }
#endif // #if UPSTREAM_UNUSED

#if UPSTREAM_UNUSED
  if (BitsOpt() == BitstreamOpt::DefaultBitsOpt) {
#ifdef PRODUCTION_BUILD
    if (BitstreamEnabled() == false) {
      Message("Device " + QLDeviceManager::getInstance()->getCurrentDeviceTargetString() +
              " bitstream is not enabled, skipping");
      return true;
    }
#endif
  } else if (BitsOpt() == BitstreamOpt::Force) {
    // Force bitstream generation
  }
#endif // #if UPSTREAM_UNUSED

  std::string command = m_openFpgaExecutablePath.string() + " -batch -f " +
                        ProjManager()->projectName() + ".openfpga";

  // use the device specific openfpga script
  m_aurora_template_script_openfpga_path = QLDeviceManager::getInstance()->deviceOpenFPGAScriptFile();

  if(m_aurora_template_script_openfpga_path.empty()) {

    ErrorMessage("Cannot proceed without OpenFPGA Template Script.");
    return false;
  }

  std::string script = InitOpenFPGAScript();

  script = FinishOpenFPGAScript(script);
  if(script.empty()) {
    ErrorMessage("OpenFPGA Script is empty!");
    return false;
  }

  std::string script_path = ProjManager()->projectName() + ".openfpga";

  std::filesystem::remove(std::filesystem::path(ProjManager()->projectPath()) /
                          std::string("fabric_bitstream.bit"));
  std::filesystem::remove(std::filesystem::path(ProjManager()->projectPath()) /
                          std::string("fabric_independent_bitstream.xml"));
  // Create OpenFpga command and execute
  script_path =
      (std::filesystem::path(ProjManager()->projectPath()) / script_path)
          .string();
  std::ofstream sofs(script_path);
  sofs << script;
  sofs.close();
#if UPSTREAM_UNUSED
  if (!FileUtils::FileExists(m_openFpgaExecutablePath)) {
    ErrorMessage("Cannot find executable: " +
                 m_openFpgaExecutablePath.string());
    return false;
  }
#endif // #if UPSTREAM_UNUSED

  std::ofstream ofs(
      (std::filesystem::path(ProjManager()->projectPath()) /
       std::string(ProjManager()->projectName() + "_bitstream.cmd"))
          .string());
  ofs << command << std::endl;
  ofs.close();
  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
                 " bitstream generation failed");
    return false;
  }
  m_state = State::BistreamGenerated;

  Message("Design " + ProjManager()->projectName() + " bitstream is generated");
  return true;
}

bool CompilerOpenFPGA_ql::GeneratePinConstraints(std::string& filepath_fpga_fix_pins_place_str) {
  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }
  
  // PinConstraints (.place): if a pre-generated pin constraints file is available, prefer to use that, and
  //   ignore any pcf file for the project, so pcf2place flow is not invoked.
  // if there is a .place file path specified in "openfpga" > "general" > "place" > "default" : use this, else:
  // if there is a .place file in the design directory with the name: <project_name>_fix_pins.place -> use this, else:
  // use the pcf file and pcf2place flow as below:

  // PinConstraints (.pcf): if there is a PCF file available, we need to generate PinConstraints (.place) file
  //  and use it in the VPR placement stage.
  // if there is a pcf file path specified in "openfpga" > "general" > "pcf" > "default" : use this, else:
  // if there is a pcf file in the design directory with the name <project_name>.pcf  : use this, else:
  // no pcf file is found, continue without PinConstraints.

  // either way, if the .place file is available, set the path to it in the 'filepath_fpga_fix_pins_place_str' variable ref passed in.

  // reload QLSettingsManager() to ensure we account for dynamic changes in the settings/power json:
  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }


  ///////////////////////////////////////////////////////////////// PLACE ++
  //QLSettingsManager::getStringValue("general", "device", "family");
  std::filesystem::path filepath_place;
  if( !QLSettingsManager::getStringValue("openfpga", "general", "place").empty() ) {
    filepath_place = QLSettingsManager::getStringValue("openfpga", "general", "place");
  }
  else {
    filepath_place = ProjManager()->projectName() + std::string("_fix_pins") + std::string(".place");
  }
  // we are currently in the 'design_directory' now...
  if (FileUtils::FileExists(filepath_place)) {
    if (!filepath_place.is_absolute()) {
      // if it exists, make path relative to the working directory (used when openfpga is actually run)
      filepath_place = std::filesystem::path(std::filesystem::path("..") / filepath_place);
    }
    // set the PinConstraints file path to be used by the caller.
    filepath_fpga_fix_pins_place_str = filepath_place.string();

    (*m_out) << "Design " << ProjManager()->projectName()
             << " use available PinConstraints file: "
             << filepath_fpga_fix_pins_place_str
             << std::endl;

    return true;
  }
  // else
  // no place file found, so we continue with the PCF flow for PinConstraints below.
  ///////////////////////////////////////////////////////////////// PLACE --

  ///////////////////////////////////////////////////////////////// PCF ++
  std::filesystem::path filepath_pcf = QLSettingsManager::getInstance()->getPCFFilePath();

  if(filepath_pcf.empty()) {
    // no pcf file found, so we continue without PinConstraints defined.
    // This is not an error, so we return true.
    return true;
  }

#if ORIGINAL_PCF_LOGIC
  if( !QLSettingsManager::getStringValue("openfpga", "general", "pcf").empty() ) {
    filepath_pcf = QLSettingsManager::getStringValue("openfpga", "general", "pcf");
  }
  else {
    if (!QLSettingsManager::getInstance()->getTCLScriptDirPath().empty()) {
      filepath_pcf = QLSettingsManager::getInstance()->getTCLScriptDirPath().string() + "/" + ProjManager()->projectName() + "/" + ProjManager()->projectName() + std::string(".pcf");
      if (!FileUtils::FileExists(filepath_pcf)) {
        filepath_pcf = QLSettingsManager::getInstance()->getTCLScriptDirPath().string() + "/" + ProjManager()->projectName() + std::string(".pcf");
      }
    } else {
      filepath_pcf = ProjManager()->projectPath() + "/" + ProjManager()->projectName() + std::string(".pcf");
    }
  }
  // we are currently in the 'design_directory' now...
  if (FileUtils::FileExists(filepath_pcf)) {
    if (!filepath_pcf.is_absolute()) {
      // if it exists, make path relative to the working directory (used when openfpga is actually run)
      filepath_pcf = std::filesystem::path(std::filesystem::path("..") / filepath_pcf);
    }
  }
  else {
    // no pcf file found, so we continue without PinConstraints defined.
    // This is not an error, so we return true.
    return true;
  }

  auto removePathPrefixFn = [](const std::filesystem::path& original_path, const std::filesystem::path& prefix) -> std::filesystem::path {
    if (original_path.string().find(prefix.string()) == 0) {
        return original_path.lexically_relative(prefix);
    } else {
        return original_path;
    }
  };

  // if pcf is located in current project folder, we may convert path to relative, since .openfpga also be called from that directory
  filepath_pcf = removePathPrefixFn(filepath_pcf, std::filesystem::path(ProjManager()->projectPath()));
#endif // #if ORIGINAL_PCF_LOGIC
  ///////////////////////////////////////////////////////////////// PCF --

  ///////////////////////////////////////////////////////////////// NETLIST ++
  std::string netlistFile = ProjManager()->projectName() + "_post_synth.blif";
  for (const auto& lang_file : ProjManager()->DesignFiles()) {
    switch (lang_file.first.language) {
      case Design::Language::VERILOG_NETLIST:
      case Design::Language::BLIF:
      case Design::Language::EBLIF: {
        netlistFile = lang_file.second;
        std::filesystem::path the_path = netlistFile;
        if (!the_path.is_absolute()) {
          netlistFile =
              std::filesystem::path(std::filesystem::path("..") / netlistFile)
                  .string();
        }
        break;
      }
      default:
        break;
    }
  }
  ///////////////////////////////////////////////////////////////// NETLIST --

  // get the required values for the current device:
  QLDeviceTarget device_target = QLDeviceManager::getInstance()->getCurrentDeviceTarget();

  ///////////////////////////////////////////////////////////////// PIN TABLE CSV ++
  auto [filepath_pin_table_csv, error] = findCurrentDevicePinTableCsv();
  if (filepath_pin_table_csv.empty()) {
      // no pin table csv available, we cannot proceed with the pcf flow!
      ErrorMessage(std::string(__func__) + ": pin table csv not found, cannot continue with pcf flow!");
      return false;
  }
  ///////////////////////////////////////////////////////////////// PIN TABLE CSV --

  ///////////////////////////////////////////////////////////////// FPGA IO MAP XML ++
  // holder for final io map xml path
  std::filesystem::path filepath_fpga_io_map_xml;

  filepath_fpga_io_map_xml = QLDeviceManager::getInstance()->deviceOpenFPGAIOMapFile();

  if(filepath_fpga_io_map_xml.empty()) {

    ErrorMessage(std::string(__func__) + ": fpga io map xml not found, cannot continue with pcf flow!");
    return false;
  }
  ///////////////////////////////////////////////////////////////// FPGA IO MAP XML --

  ///////////////////////////////////////////////////////////////// VPR FIX PINS PLACE ++
  // we want OpenFPGA to generate the vpr fix pins place file in the working_directory itself
  // and it will be used from there, so we don't adjust the path below.
  std::filesystem::path filepath_fpga_fix_pins_place;
  filepath_fpga_fix_pins_place = ProjManager()->projectName() + std::string("_fix_pins") + std::string(".place");
  ///////////////////////////////////////////////////////////////// VPR FIX PINS PLACE --

  (*m_out) << "##################################################" << std::endl;
  (*m_out) << "PinConstraints generation for design \""
          << ProjManager()->projectName() << "\" on device \""
          << QLDeviceManager::getInstance()->getCurrentDeviceTargetString() << "\"" << std::endl;
  (*m_out) << "##################################################" << std::endl;

  // call openfpga to generate pin_constraints 'fix_pins.place' if 'pcf2place' is enabled
  // pcf2place --pcf ${OPENFPGA_PCF} --blif ${VPR_TESTBENCH_BLIF} --pin_table ${OPENFPGA_PIN_TABLE} --fpga_io_map ${OPENFPGA_IO_MAP_FILE} --fpga_fix_pins ${OPENFPGA_VPR_FIX_PINS_FILE}
  // replace command: ${OPENFPGA_PCF2PLACE_COMMAND} in the template script

  std::string openfpga_pcf2place_command;
  openfpga_pcf2place_command = std::string("pcf2place") +
                               std::string(" --blif") +
                               std::string(" ") + netlistFile +
                               std::string(" --pcf") +
                               std::string(" ") + filepath_pcf.string() +
                               std::string(" --pin_table") +
                               std::string(" ") + filepath_pin_table_csv.string() +
                               std::string(" --fpga_io_map") +
                               std::string(" ") + filepath_fpga_io_map_xml.string() +
                               std::string(" --fpga_fix_pins") +
                               std::string(" ") + filepath_fpga_fix_pins_place.string() +
                               std::string(" --pin_table_direction_convention") +
                               std::string(" ") + std::string("quicklogic");

  // this does not seem to be supported in OpenFPGA
  // openfpga_pcf2place_command += std::string(" --assign_unconstrained_pins") + 
  //                               std::string(" ") + 
  //                               std::string("in_define_order"); // or "random"

  std::string script = qlOpenFPGApcf2placeScript;
  script = ReplaceAll(script, "${OPENFPGA_PCF2PLACE_COMMAND}", openfpga_pcf2place_command);
  
  std::string pin_constaints_openfpga_script_name = ProjManager()->projectName() + 
                                                    std::string("_pinconstraints") + 
                                                    std::string(".openfpga");
  std::string command = m_openFpgaExecutablePath.string() + 
                        std::string(" -f") +
                        std::string(" ") +
                        pin_constaints_openfpga_script_name;

  // Create OpenFpga command and execute
  std::filesystem::path script_path =
      (std::filesystem::path(ProjManager()->projectPath()) / pin_constaints_openfpga_script_name)
          .string();
  std::ofstream sofs(script_path);
  sofs << script;
  sofs.close();

  std::ofstream ofs(
      (std::filesystem::path(ProjManager()->projectPath()) /
      std::string(ProjManager()->projectName() + "_pinconstraints.cmd"))
          .string());
  ofs << command << std::endl;
  ofs.close();

  int status = ExecuteAndMonitorSystemCommand(command);
  CleanTempFiles();
  if (status) {
    ErrorMessage("Design " + ProjManager()->projectName() +
                " PinConstraints generation failed!");
    return false;
  }

  (*m_out) << "Design " << ProjManager()->projectName()
          << " PinConstraints generated!" << std::endl;
  
  // set the PinConstraints file path to be used by the caller.
  filepath_fpga_fix_pins_place_str = filepath_fpga_fix_pins_place.string();
  return FileUtils::FileExists(ProjManager()->projectPath() / filepath_fpga_fix_pins_place);
}

bool CompilerOpenFPGA_ql::GenerateIOFloorPlanConstraints() {
  std::filesystem::path io_floor_planningpath = std::filesystem::path(ProjManager()->projectPath()) / 
  std::string(ProjManager()->projectName() + "_constraints.xml");
  
  if (fs::exists(io_floor_planningpath)){
    Message(ProjManager()->projectName() + "_constraints.xml" + 
            " Already Exists. Using the Existing Constraint File.");
    return true;
  }

  if (!ProjManager()->HasDesign()) {
    ErrorMessage("No design specified");
    return false;
  }

  QLSettingsManager::reloadJSONSettings();

  // check if settings were loaded correctly before proceeding:
  if((QLSettingsManager::getInstance()->settings_json).empty()) {
    ErrorMessage("Project Settings JSON is missing, please check <project_name> and corresponding <project_name>.json exists: " + ProjManager()->projectName());
    return false;
  }

  if( !QLDeviceManager::getInstance()->isDeviceTargetValid(QLDeviceManager::getInstance()->getCurrentDeviceTarget()) ) {
    ErrorMessage("Invalid Device set in Settings JSON! Please check if the target device is correct/available. ");
    std::string family              = QLSettingsManager::getStringValue("general", "device", "family");
    std::string foundry             = QLSettingsManager::getStringValue("general", "device", "foundry");
    std::string node                = QLSettingsManager::getStringValue("general", "device", "node");
    std::string devicename          = QLSettingsManager::getStringValue("general", "device", "devicename");
    std::string voltage_threshold   = QLSettingsManager::getStringValue("general", "device", "voltage_threshold");
    std::string p_v_t_corner        = QLSettingsManager::getStringValue("general", "device", "p_v_t_corner");
    std::string layout              = QLSettingsManager::getStringValue("general", "device", "layout");
    Message("family: " + family);
    Message("foundry: " + foundry);
    Message("node: " + node);
    Message("devicename: " + devicename);
    Message("voltage_threshold: " + voltage_threshold);
    Message("p_v_t_corner: " + p_v_t_corner);
    Message("layout: " + layout);
    return false;
  }

  std::filesystem::path netlist_path = std::filesystem::path(ProjManager()->projectPath()) / 
                                      std::string(ProjManager()->projectName() + "_post_synth.blif");

  if (!fs::exists(netlist_path)){
    ErrorMessage("Post Synthesis blif Was Not Found!\n");
    ErrorMessage("Design " + ProjManager()->projectName() + " IO Floor Plan Generation Failed!\n");
    return false;
  }
  
  std::filesystem::path floor_planning_constraint_filepath = QLSettingsManager::getInstance()->getQDCFilePath();
  if (!fs::exists(floor_planning_constraint_filepath)){
    Message("qdc Constraint File Does Not Exist. Skipping IO Floor Plan Constraint Generation.\n");
    return true;
  }

  std::string line;
  std::ifstream infile(floor_planning_constraint_filepath);
  std::unordered_set<std::string> leftSet, rightSet, topSet, bottomSet;
  std::unordered_map<std::string, std::unordered_set<std::string>*> sideMap = {
    {"left", &leftSet},
    {"right", &rightSet},
    {"top", &topSet},
    {"bottom", &bottomSet}
  };

  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    std::string token, signalName;
    iss >> token;

    if (token.empty()){
      Message("Empty line found in QDC file. Skipping...\n");
      continue; // Skip empty lines
    }
    
    if (token != "set_io_side"){
      ErrorMessage("Invalid QDC command. Expected 'set_io_side' command.");
      return false;
    }

    iss >> signalName;
    std::string side;
    while (iss >> side) {
        std::transform(side.begin(), side.end(), side.begin(), ::tolower); 
        auto it = sideMap.find(side);
        if (it != sideMap.end()) {
            it->second->insert(signalName); // insert avoids duplicates
        }
    }
  }

  // Convert sets to comma-separated strings
  auto setToString = [](const std::unordered_set<std::string>& set) {
      std::string result;
      for (const auto& sig : set) {
          result += sig + ",";
      }
      if (!result.empty()) {
        result.erase(result.size() - 1); // remove the last ","
      }
      return result;
  };

  std::string leftStr   = setToString(leftSet);
  std::string rightStr  = setToString(rightSet);
  std::string topStr    = setToString(topSet);
  std::string bottomStr = setToString(bottomSet);

  // Output results
  if (!leftStr.empty())
    leftStr = std::string(" left:"   + leftStr);
  if (!rightStr.empty())
    rightStr = std::string(" right:"  + rightStr);
  if (!topStr.empty())
    topStr = std::string(" top:"    + topStr);
  if (!bottomStr.empty())
    bottomStr = std::string(" bottom:" + bottomStr);

  if (leftStr.empty() && rightStr.empty() && topStr.empty() && bottomStr.empty()) {
    ErrorMessage("QDC file either does not contain a valid side or the side is empty\n");
    return false;
  }
  
  std::filesystem::path generate_floorplanning_script_path =
      GetSession()->Context()->DataPath() /
      std::filesystem::path("..") /
      std::filesystem::path("scripts") /
      std::filesystem::path("generate_floorplanning.py");
      
      
  std::string netlistFile = ProjManager()->projectName() + "_post_synth.blif";
  std::string output_path = std::string("--output_path " + ProjManager()->projectName() + "_constraints.xml");
  std::string architectureFile = m_architectureFile.string();
  #ifdef _WIN32
    std::filesystem::path python_exec{"python.exe"};
  #else // _WIN32
    std::filesystem::path python_exec{"python3"};
  #endif // _WIN32

  if (!FileUtils::IsSystemCommandAvailable(python_exec.string())) {
  #ifdef USE_IPGENERATOR_PYTHON_FOR_FLOORPLANNING
    // if we couldn't find system python3 interpreter we use bundled python3 from ipgenerator
    pythonExec = IPCatalog::getPythonPath(GetIPGenerator()->EnvsPath());
  #else // USE_IPGENERATOR_PYTHON_FOR_FLOORPLANNING
    ErrorMessage("System " + python_exec.string() +
                " is not found, Please install " + python_exec.string() + " and make sure it's in the PATH variable."
                " IO Floor Plan Generation Failed!");
    return false;
  #endif // USE_IPGENERATOR_PYTHON_FOR_FLOORPLANNING
  }

  std::string command = std::string(python_exec.string() + " " +
                        generate_floorplanning_script_path.string() + " " +
                        netlistFile + " " + 
                        architectureFile + " " +
                        QLSettingsManager::getStringValue("general", "device", "layout") + 
                        leftStr + rightStr + topStr + bottomStr + " " + 
                        output_path); 

  std::filesystem::path pin_constraint_filepath = QLSettingsManager::getInstance()->getPCFFilePath();
  if (fs::exists(floor_planning_constraint_filepath)) {
    command += std::string(" --pcf_file ") + pin_constraint_filepath.string();
  }

  int status = ExecuteAndMonitorSystemCommand(command);

  if (status == 1) { //Failure
    ErrorMessage("Design " + ProjManager()->projectName() +
                " IO Floor Plan Generation Failed!");
    return false;
  }
  else if (status == 2){ //Skipped
    Message("All of the atoms on the QDC have been overwritten by PCF file; Thus, no partition has been created!");
    return true;
  }
  else { //Success
    return true;
  }
}

bool CompilerOpenFPGA_ql::LoadDeviceData(const std::string& deviceName) {
  bool status = true;
#if UPSTREAM_UNUSED
  std::filesystem::path datapath = GetSession()->Context()->DataPath();
  std::filesystem::path devicefile =
      datapath / std::string("etc") / std::string("device.xml");
  QFile file(devicefile.string().c_str());
  if (!file.open(QFile::ReadOnly)) {
    ErrorMessage("Cannot open device file: " + devicefile.string());
    return false;
  }

  QDomDocument doc;
  if (!doc.setContent(&file)) {
    file.close();
    ErrorMessage("Incorrect device file: " + devicefile.string());
    return false;
  }
  file.close();

  QDomElement docElement = doc.documentElement();
  QDomNode node = docElement.firstChild();
  bool foundDevice = false;
  while (!node.isNull()) {
    if (node.isElement()) {
      QDomElement e = node.toElement();

      std::string name = e.attribute("name").toStdString();
      if (name == deviceName) {
        foundDevice = true;
        QDomNodeList list = e.childNodes();
        for (int i = 0; i < list.count(); i++) {
          QDomNode n = list.at(i);
          if (!n.isNull() && n.isElement()) {
            if (n.nodeName() == "internal") {
              std::string file_type =
                  n.toElement().attribute("type").toStdString();
              std::string file = n.toElement().attribute("file").toStdString();
              std::string name = n.toElement().attribute("name").toStdString();
              std::string num = n.toElement().attribute("num").toStdString();
              std::filesystem::path fullPath;
              if (FileUtils::FileExists(file)) {
                fullPath = file;  // Absolute path
              } else {
                fullPath = datapath / std::string("etc") /
                           std::string("devices") / file;
              }
              if (!FileUtils::FileExists(fullPath.string())) {
                ErrorMessage(
                    "Invalid device config file: " + fullPath.string() + "\n");
                status = false;
              }
              if (file_type == "vpr_arch") {
                ArchitectureFile(fullPath.string());
              } else if (file_type == "openfpga_arch") {
                OpenFpgaArchitectureFile(fullPath.string());
              } else if (file_type == "bitstream_settings") {
                OpenFpgaBitstreamSettingFile(fullPath.string());
              } else if (file_type == "sim_settings") {
                OpenFpgaSimSettingFile(fullPath.string());
              } else if (file_type == "repack_settings") {
                OpenFpgaRepackConstraintsFile(fullPath.string());
              } else if (file_type == "fabric_key") {
                OpenFpgaFabricKeyFile(fullPath.string());
              } else if (file_type == "pinmap_xml") {
                OpenFpgaPinmapXMLFile(fullPath.string());
              } else if (file_type == "pb_pin_fixup") {
                PbPinFixup(name);
              } else if (file_type == "pinmap_csv") {
                PinmapCSVFile(fullPath);
              } else if (file_type == "plugin_lib") {
                YosysPluginLibName(name);
              } else if (file_type == "plugin_func") {
                YosysPluginName(name);
              } else if (file_type == "technology") {
                YosysMapTechnology(name);
              } else if (file_type == "synth_type") {
#if UPSTREAM_UNUSED
                if (name == "QL")
                  SynthType(SynthesisType::QL);
                else if (name == "RS")
                  SynthType(SynthesisType::RS);
                else if (name == "Yosys")
                  SynthType(SynthesisType::Yosys);
                else {
                  ErrorMessage("Invalid synthesis type: " + name + "\n");
                  status = false;
                }
#endif // #if UPSTREAM_UNUSED
              } else if (file_type == "synth_opts") {
                PerDeviceSynthOptions(name);
              } else if (file_type == "vpr_opts") {
                PerDevicePnROptions(name);
              } else if (file_type == "device_size") {
                DeviceSize(name);
              } else if (file_type == "lut_size") {
                LutSize(std::strtoul(num.c_str(), nullptr, 10));
              } else if (file_type == "channel_width") {
                ChannelWidth(std::strtoul(num.c_str(), nullptr, 10));
              } else if (file_type == "bitstream_enabled") {
                if (num == "true") {
                  BitstreamEnabled(true);
                } else if (num == "false") {
                  BitstreamEnabled(false);
                } else {
                  ErrorMessage("Invalid bitstream_enabled num (true, false): " +
                               num + "\n");
                  status = false;
                }
              } else if (file_type == "pin_constraint_enabled") {
                if (num == "true") {
                  PinConstraintEnabled(true);
                } else if (num == "false") {
                  PinConstraintEnabled(false);
                } else {
                  ErrorMessage(
                      "Invalid pin_constraint_enabled num (true, false): " +
                      num + "\n");
                  status = false;
                }
              } else {
                ErrorMessage("Invalid device config type: " + file_type + "\n");
                status = false;
              }
            }
          }
        }
      }
    }

    node = node.nextSibling();
  }
  if (!foundDevice) {
    ErrorMessage("Incorrect device: " + deviceName + "\n");
    status = false;
  }

  if (!LicenseDevice(deviceName)) {
    ErrorMessage("Device is not licensed: " + deviceName + "\n");
    status = false;
  }
#endif // #if UPSTREAM_UNUSED
  return status;
}

bool CompilerOpenFPGA_ql::LicenseDevice(const std::string& deviceName) {
  // No need for licenses
  return true;
}

std::string CompilerOpenFPGA_ql::ToUpper(std::string str) {
        std::string upper;
        // for (size_t i = 0; i < str.size(); ++i) {
        //     char C = ::toupper(str[i]);
        //     upper.push_back(C);
        // }
        // https://stackoverflow.com/a/39927248
        upper = str;
        auto& facet = 
            std::use_facet<std::ctype<char>>(std::locale());
        facet.toupper(upper.data(), upper.data() + upper.size());
        return upper;
    }

std::string CompilerOpenFPGA_ql::ToLower(std::string str) {
    std::string lower;
    // for (size_t i = 0; i < str.size(); ++i) {
    //     char C = ::tolower(str[i]);
    //     lower.push_back(C);
    // }
    // https://stackoverflow.com/a/39927248
    lower = str;
    auto& facet = 
        std::use_facet<std::ctype<char>>(std::locale());
    facet.tolower(lower.data(), lower.data() + lower.size());
    return lower;
}

std::pair<std::filesystem::path, std::string> CompilerOpenFPGA_ql::findCurrentDevicePinTableCsv() const
{
  QLDeviceTarget device_target = QLDeviceManager::getInstance()->getCurrentDeviceTarget();

  // we expect the pin table csv to be named: layoutname_pin_table.csv (legacy)
  // --or-- just pin_table.csv (default) - this is true for all devices with single layout.
  // this would be in the device_data/family/foundry/node/aurora directory
  // optionally, it can also be placed the design_directory
  
  // holder for final pin table csv path
  std::filesystem::path filepath_pin_table_csv;

  filepath_pin_table_csv = QLDeviceManager::getInstance()->deviceOpenFPGAPinTableFile();

  // if no pin table csv is found, we cannot proceed with the pcf flow
  if(filepath_pin_table_csv.empty()) {
    return std::make_pair("", "pin table csv not found!");
  }

  return std::make_pair(filepath_pin_table_csv, "");
}

std::filesystem::path CompilerOpenFPGA_ql::GenerateTempFilePath(bool managedOutside) {

    // remember where we are
    std::filesystem::path current_path = std::filesystem::current_path();

    // get a guaranteed temp directory
    std::filesystem::path temp_dir_path = std::filesystem::temp_directory_path();

    // change to the temp directory before generating a temp file name
    std::filesystem::current_path(temp_dir_path);

    // generate a temp file path in the system temp directory
    std::filesystem::path temp_file_path;
#if defined(_WIN32)
    // in windows, the tmpnam only generates the file name (with a '\' in the front), and we need to append this
    // to the temp_directory_path() to make a complete path.
    std::string temp_file_path_str = std::tmpnam(nullptr);
    // convert the string into a std::filesystem::path
    temp_file_path = temp_file_path_str;
    // tmpnam() returns a filepath that starts with a '\' and is hence an absolute path
    // using the '/' operator with 2 absolute paths, results in replacement, than append!
    // https://stackoverflow.com/questions/55214156/why-does-stdfilesystempathappend-replace-the-current-path-if-p-starts-with
    // hence, we should convert the temp_file_path into a relative path first, and then
    // append it to the temp_dir_path to get the absolute path we need on Windows.
    temp_file_path = temp_file_path.relative_path();
    temp_file_path = temp_dir_path / temp_file_path;
#else // #if defined(_WIN32)
    // in linux, the tmpnam generates the file path including the current dir path
    // so, we can use this as the final path as is.
    std::string temp_file_path_str = std::tmpnam(nullptr);
    temp_file_path = temp_file_path_str;
#endif // #if defined(_WIN32)

    // change back to the original path we came from
    std::filesystem::current_path(current_path);

    // add to our cleanup list
    if (!managedOutside) {
      m_TempFileList.push_back(temp_file_path);
    }
    
    // return the temp file path we obtained
    return temp_file_path;
}


int CompilerOpenFPGA_ql::CleanTempFiles() {

  int count = 0;
  std::error_code ec;
  for(std::filesystem::path tempFile: m_TempFileList) {
    // delete the source encrypted file, as it not needed anymore.
    std::filesystem::remove(tempFile,
                            ec);

    //std::cout << "removing: " << tempFile << std::endl;
    if(ec) {
      // error : ignore it.
      //std::cout << "failed removing: " << tempFile << std::endl;
    }
    count++;
  }

  m_TempFileList.clear();

  return count;
}

void CompilerOpenFPGA_ql::CleanScripts() {
  m_yosysScript = "";
  m_openFPGAScript = "";
}


long double CompilerOpenFPGA_ql::PowerEstimator_Dynamic() {

  // Based on v1.38: https://github.com/QL-Proprietary/eFPGA_PowerCalculator/blob/main/K6N10%20TSMC%2016nm%20Power%20Calculator%20v1.38.xlsx

  // overall, from the spreadsheet:
  // power_dynamic =
  //  (
  //     (
  //     $Calculator.D11*$Dynamic.O6+
  //     $Calculator.D12*$Dynamic.O7+
  //     $Calculator.D21*($Dynamic.O8+$Dynamic.O14)/2+
  //     $Calculator.D14*$Dynamic.O9+
  //     $Calculator.D15*$Dynamic.O10
  //     )+
  //     (
  //     $Calculator.D16*$Dynamic.O12+
  //     $Calculator.D17*$Dynamic.O13+
  //     $Calculator.D20*$Dynamic.O15+
  //     $Calculator.D18*$Dynamic.O16
  //     )+
  //     IF($Calculator.D27<=5,$Calculator.D22*$Dynamic.O35,0)+
  //     IF($Calculator.D27=6,$Calculator.D22*$Dynamic.O37,0)+
  //     MAX($Dynamic.O36-$Dynamic.O35,$Dynamic.O38-$Dynamic.O37)*$Calculator.D26+
  //     ($Calculator.D28*ROUNDUP($Calculator.D26/(80*$'clock network'.N97),0)*$'clock network'.T96)
  //  ) / (0.8*0.8)
  //    *($Calculator.D8*$Calculator.D8)+
  //     D29*$FFB.N3+
  //     D30*$FFB.N4


  // step 0: figure out relationships and where are the constants
  // e.g.: $Calculator.D11*$Dynamic.O6
  // $Calculator.D11 -> user_input
  // $Dynamic.O6 -> $Dynamic.P6 (if > 0) -> (if calc e11 freq > 0) then, if e11 freq <= 10mhz, use dynamiclinear o6, else if e11 freq <= 250mhz, use dynamicpoly o6, else use 0
  //    $DynamicLinear.O6 -> calc with e11 and $DynamicLinear.K6, $DynamicLinear.L6, $DynamicLinear.M6 -> these are constants
  //    $DynamicPoly.O6 -> calc with e11 and $DynamicPoly.K6, $DynamicPoly.L6, $DynamicPoly.M6 -> these are constants

  // step 1:
  // we need all the constants used first saved here.
  // start by adding all constants required: 
  //    DynamicLinear K, L, M columns (rows: 6,7,8,9,10,12,13,14,15,16,35,36,37,38)
  //    DynamicPoly K, L, M columns (rows: 6,7,8,9,10,12,13,14,15,16,35,36,37,38)
  //    FFBLinear K, L, M columns (rows: 3,4)
  //    FFBPoly K, L, M columns (rows: 3,4)

  // step 2:
  // process user inputs

  // step 3:
  // parse design inputs from the logs

  // step 4:
  // calculate all frequencies of components using (2) and (3)

  // step 5:
  // obtain Dynamic and FFB values (uses frequency from Calculator)
  // obtain DynamicLinear O column values (uses frequency from Calculator)
  // obtain DynamicPoly O column values (uses frequency from Calculator)
  // obtain Dynamic P, and O columns (O=P if P>0) values from Poly and Linear (uses frequency from Calculator)
  // obtain FFBLinear N column values
  // obtain FFBPoly N column values
  // obtain FFB N and O columns values (N=O if O>0) values from Poly and Linear (uses frequency from Calculator)

  // step 6:
  // apply all values into power formula


  // final power number
  long double power_dynamic = 0;


  // step 1: define constants from the spreadsheet
  // ===================================================== Constants ++
  // ===================================================== DynamicLinear Constants ++
  long double dynamic_linear_k5       = 0;
  long double dynamic_linear_l5       = 0.0004278670;
  long double dynamic_linear_m5       = 0;

  long double dynamic_linear_k6       = 0;
  long double dynamic_linear_l6       = 0.0000360280;
  long double dynamic_linear_m6       = 0;
    
  long double dynamic_linear_k7       = 0;
  long double dynamic_linear_l7       = 0.0000500160;
  long double dynamic_linear_m7       = 0;
    
  long double dynamic_linear_k8       = 0;
  long double dynamic_linear_l8       = 0.0000136140;
  long double dynamic_linear_m8       = 0;
    
  long double dynamic_linear_k9       = 0;
  long double dynamic_linear_l9       = 0.0001175010;
  long double dynamic_linear_m9       = 0;
    
  long double dynamic_linear_k10      = 0;
  long double dynamic_linear_l10      = 0.0000439390;
  long double dynamic_linear_m10      = 0;
    
  long double dynamic_linear_k12      = 0;
  long double dynamic_linear_l12      = 0.0000021880;
  long double dynamic_linear_m12      = 0;
    
  long double dynamic_linear_k13      = 0;
  long double dynamic_linear_l13      = 0.0000596270;
  long double dynamic_linear_m13      = 0;
    
  long double dynamic_linear_k14      = 0;
  long double dynamic_linear_l14      = 0.0000188840;
  long double dynamic_linear_m14      = 0;
    
  long double dynamic_linear_k15      = 0;
  long double dynamic_linear_l15      = 0.0000056040;
  long double dynamic_linear_m15      = 0;
    
  long double dynamic_linear_k16      = 0;
  long double dynamic_linear_l16      = 0.0001608270;
  long double dynamic_linear_m16      = 0;
    
  long double dynamic_linear_k35      = 0;
  long double dynamic_linear_l35      = 0.0000892080;
  long double dynamic_linear_m35      = 0;
    
  long double dynamic_linear_k36      = 0;
  long double dynamic_linear_l36      = 0.0001714450;
  long double dynamic_linear_m36      = 0;
    
  long double dynamic_linear_k37      = 0;
  long double dynamic_linear_l37      = 0.0001047010;
  long double dynamic_linear_m37      = 0;
    
  long double dynamic_linear_k38      = 0;
  long double dynamic_linear_l38      = 0.0001311860;
  long double dynamic_linear_m38      = 0;
  // ===================================================== DynamicLinear Constants --
  // ===================================================== DynamicPoly Constants ++
  long double dynamic_poly_k5         = 0.0000002;
  long double dynamic_poly_l5         = 0.0011;
  long double dynamic_poly_m5         = -0.0032;

  long double dynamic_poly_k6         = -0.00000007;
  long double dynamic_poly_l6         = 0.00006;
  long double dynamic_poly_m6         = 0.0001;

  // long double dynamic_poly_k7         = 0;              // -->  (not used currently)
  long double dynamic_poly_l7         = 0.0032;
  long double dynamic_poly_m7         = -0.0066;

  long double dynamic_poly_k8         = -0.00000003;
  long double dynamic_poly_l8         = 0.00008;
  long double dynamic_poly_m8         = -0.0005;

  long double dynamic_poly_k9         = 0.0000000000007;
  long double dynamic_poly_l9         = 0.000002;
  long double dynamic_poly_m9         = 0.0065;

  long double dynamic_poly_k10        = -0.00000009;
  long double dynamic_poly_l10        = 0.0001;
  long double dynamic_poly_m10        = 0.0005;

  long double dynamic_poly_k12        = -0.00000001;
  long double dynamic_poly_l12        = 0.00001;
  long double dynamic_poly_m12        = 0.0004;

  long double dynamic_poly_k13        = -0.0000004;
  long double dynamic_poly_l13        = 0.0001;
  long double dynamic_poly_m13        = 0.0008;

  long double dynamic_poly_k14        = -0.00000003;
  long double dynamic_poly_l14        = 0.0001;
  long double dynamic_poly_m14        = -0.0007;

  long double dynamic_poly_k15        = -0.00000001;
  long double dynamic_poly_l15        = 0.00003;
  long double dynamic_poly_m15        = -0.0002;

  long double dynamic_poly_k16        = -0.0000002;
  long double dynamic_poly_l16        = 0.0003;
  long double dynamic_poly_m16        = 0.0004;

  long double dynamic_poly_k35        = -0.0000002;
  long double dynamic_poly_l35        = 0.0002;
  long double dynamic_poly_m35        = -0.0005;

  long double dynamic_poly_k36        = -0.0000003;
  long double dynamic_poly_l36        = 0.0003;
  long double dynamic_poly_m36        = 1.00E-04;

  long double dynamic_poly_k37        = -0.0000003;
  long double dynamic_poly_l37        = 0.0003;
  long double dynamic_poly_m37        = -0.001;

  long double dynamic_poly_k38        = -0.0000004;
  long double dynamic_poly_l38        = 0.0004;
  long double dynamic_poly_m38        = -0.001;
  // ===================================================== DynamicPoly Constants --
  // ===================================================== FFBLinear Constants ++
  long double ffb_linear_k3           = 0;
  long double ffb_linear_l3           = 0.006619534;
  long double ffb_linear_m3           = 0;

  long double ffb_linear_k4           = 0;
  long double ffb_linear_l4           = 0.015547711;
  long double ffb_linear_m4           = 0;
  // ===================================================== FFBLinear Constants --
  // ===================================================== FFBPoly Constants ++
  long double ffb_poly_k3             = 0.000000000002;
  long double ffb_poly_l3             = 0.0074;
  long double ffb_poly_m3             = 0.0052;

  long double ffb_poly_k4             = 0.0000000004;
  long double ffb_poly_l4             = 0.0173;
  long double ffb_poly_m4             = 0.0669;
  // ===================================================== FFBPoly Constants --
  // ===================================================== ClockNetwork Constants ++
  long double clock_network_l90 = 48;     // = num_clock_buffers
  long double clock_network_p95 = 48;     // = clock_buffer_reduction = (4*1) + (2+2) + (1*8) TODO: updated needed clarify
  long double clock_network_n97 = 1;      // = utilization_factor
  // ===================================================== ClockNetwork Constants --
  // ===================================================== Constants --


  // ===================================================== User Inputs ++
  // step 2: user inputs

  // check for user inputs power json:
  if( QLSettingsManager::getJson("power", "power_inputs") == nullptr ) {

    // there are no power_inputs parameters required for power analysis!
    Message("\n>> power_inputs in JSON unavailable, skipping power analysis!");

    return power_dynamic;
  }

  // check if the user has explicitly enabled power estimation:
  if( QLSettingsManager::getStringValue("power", "power_outputs", "dynamic_power") != "checked" ) {

    // user has not enabled power analysis
    Message("\n>> dynamic_power is disabled in JSON, skipping power analysis!");

    return power_dynamic;
  }

  // set everything to be printed in fixed point instead of scientific notation:
  std::cout.setf (std::ios::fixed);

  // enable debug prints if specified in JSON
  bool power_estimation_dbg = false;
  std::ofstream power_analysis_debug_rpt;

  if( QLSettingsManager::getStringValue("power", "power_outputs", "debug") == "checked" ) {

    power_estimation_dbg = true;

    // write power analysis debug prints into file
    std::filesystem::path power_analysis_debug_rpt_filepath = 
      std::filesystem::path(ProjManager()->projectPath()) / std::string("power_analysis_debug.rpt");

    power_analysis_debug_rpt.open(power_analysis_debug_rpt_filepath);

    if(!power_analysis_debug_rpt) {
      ErrorMessage("File: " + power_analysis_debug_rpt_filepath.string() + " could not be opened");
      power_analysis_debug_rpt.close();
      return power_dynamic;
    }
  }

  long double calculator_d8 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "voltage");                          // voltage (internal)

  long double calculator_e9 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "system_frequency_mhz");             // system_frequency_mhz (user)

  long double calculator_f11 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "input_activity_factor");            // input_activity_factor (user)

  long double calculator_f15 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "input_xbar_activity_factor");       // input_xbar_activity_factor (internal)

  long double calculator_f16 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "output_activity_factor");           // output_activity_factor (user)

  // v1.40 : F18 = F16 (removed from JSON, if value changes, we will add it back)
  long double calculator_f18 = calculator_f16;
    // QLSettingsManager::getLongDoubleValue("power", "power_inputs", "output_clb_activity_factor");    // output_clb_activity_factor (internal)

  // v1.40 : F21 = F16 (removed from JSON, if value changes, we will add it back)
  long double calculator_f21 = calculator_f16;
    // QLSettingsManager::getLongDoubleValue("power", "power_inputs", "routing_activity_factor");       // routing_activity_factor or sb_activity_factor (internal)

  long double calculator_f22 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "lut_activity_factor");              // lut_activity_factor (internal)

  long double calculator_f28 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "clock_network_activity_factor");    // clock_network_activity_factor (internal)

  long double calculator_f29 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "dsp_activity_factor");              // dsp_activity_factor (users)

  long double calculator_f30 =
    QLSettingsManager::getLongDoubleValue("power", "power_inputs", "bram_activity_factor");             // bram_activity_factor (user)

  if(power_estimation_dbg) {
    
    // std::cout <<"# ====== Power Analysis Debug ======" << std::endl;
    power_analysis_debug_rpt << "# ====== Power Analysis Debug ======" << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;

    // std::cout <<">>> Inputs For Power Calculator Spreadsheet\n" << std::endl;
    power_analysis_debug_rpt << ">>> Inputs For Power Calculator Spreadsheet\n" << std::endl;

    // std::cout <<"calculator_d8  : " << std::left << std::setw(15) << std::to_string(calculator_d8) + " V" << "[Voltage]" <<  std::endl;
    power_analysis_debug_rpt << "calculator_d8  : " << std::left << std::setw(15) << std::to_string(calculator_d8) + " V" << "[Voltage]" << std::endl;

    // std::cout <<"calculator_e9  : " << std::left << std::setw(15) << std::to_string(calculator_e9) + " MHz" << "[System Frequency]" << std::endl;
    power_analysis_debug_rpt << "calculator_e9  : " << std::left << std::setw(15) << std::to_string(calculator_e9) + " MHz" << "[System Frequency]" << std::endl;

    // std::cout <<"calculator_f11 : " << std::left << std::setw(15) << std::to_string(calculator_f11) + " %" << "[INPUT ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f11 : " << std::left << std::setw(15) << std::to_string(calculator_f11) + " %" << "[INPUT ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f15 : " << std::left << std::setw(15) << std::to_string(calculator_f15) + " %" << "[INPUT XBAR ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f15 : " << std::left << std::setw(15) << std::to_string(calculator_f15) + " %" << "[INPUT XBAR ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f16 : " << std::left << std::setw(15) << std::to_string(calculator_f16) + " %" << "[OUTPUT ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f16 : " << std::left << std::setw(15) << std::to_string(calculator_f16) + " %" << "[OUTPUT ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f18 : " << std::left << std::setw(15) << std::to_string(calculator_f18) + " %" << "[OUTPUT CLB ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f18 : " << std::left << std::setw(15) << std::to_string(calculator_f18) + " %" << "[OUTPUT CLB ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f21 : " << std::left << std::setw(15) << std::to_string(calculator_f21) + " %" << "[TOTAL # SB ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f21 : " << std::left << std::setw(15) << std::to_string(calculator_f21) + " %" << "[TOTAL # SB ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f22 : " << std::left << std::setw(15) << std::to_string(calculator_f22) + " %" << "[TOTAL # LUT ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f22 : " << std::left << std::setw(15) << std::to_string(calculator_f22) + " %" << "[TOTAL # LUT ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f28 : " << std::left << std::setw(15) << std::to_string(calculator_f28) + " %" << "[CLOCK NETWORK ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f28 : " << std::left << std::setw(15) << std::to_string(calculator_f28) + " %" << "[CLOCK NETWORK ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f29 : " << std::left << std::setw(15) << std::to_string(calculator_f29) + " %" << "[DSP ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f29 : " << std::left << std::setw(15) << std::to_string(calculator_f29) + " %" << "[DSP ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"calculator_f30 : " << std::left << std::setw(15) << std::to_string(calculator_f30) + " %" << "[BRAM ACTIVITY FACTOR]" << std::endl;
    power_analysis_debug_rpt << "calculator_f30 : " << std::left << std::setw(15) << std::to_string(calculator_f30) + " %" << "[BRAM ACTIVITY FACTOR]" << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }

  // ===================================================== User Inputs --

  // ===================================================== Design Inputs ++
  // step 3: design inputs
  // design inputs are obtained from the QLMetricsManager
  // the design inputs needed should already be available when the logs
  // are parsed at the synthesis/pack/place/route stages.
  // ===================================================== Design Inputs From Metrics++
  // declaration of all design inputs - some are derived from other design inputs.
  long double calculator_d6     = QLMetricsManager::getDoubleValue("routing", "device_size_x");          // array_x
  long double calculator_d7     = QLMetricsManager::getDoubleValue("routing", "device_size_y");          // array_y
  long double calculator_d11    = QLMetricsManager::getDoubleValue("routing", "num_input");              // num_input
  long double calculator_d12    = 0;        // num_input_ff                                 -->  (not used currently)
  // long double calculator_d13    = 0;        // num_input_sb or num_input_wire_segment       -->  (not used currently)
  long double calculator_d14    = 0;        // num_input_cbx_cby        -->  (derived later)
  long double calculator_d15    = 0;        // num_input_xbar           -->  (derived later)
  long double calculator_d16    = QLMetricsManager::getDoubleValue("routing", "num_output");             // num_output
  long double calculator_d17    = 0;        // num_output_ff                                -->  (not used currently)
  long double calculator_d18    = 0;        // num_output_clb           -->  (derived later)
  // long double calculator_d19    = 0;        // num_output_sb or num_output_wire_segment     -->  (not used currently)
  long double calculator_d20    = 0;        // num_output_cbx_cby       -->  (derived later)
  long double calculator_d21    = QLMetricsManager::getDoubleValue("routing", "num_wiring_segments");    // num_sb or num_wire_segment (also == L1_O + L4_O)
  long double calculator_d22    = 0;        // num_lut                  -->  (derived later)
  // long double calculator_d23    = 0;        // num_lut5_ff                                  -->  (not used currently)
  // long double calculator_d24    = 0;        // num_lut6                                     -->  (not used currently)
  // long double calculator_d25    = 0;        // num_lut6_ff                                  -->  (not used currently)
  long double calculator_d26    = 0;        // num_clb_ff               -->  (derived later)
  long double calculator_d27    = 0;        // num_average_lut_input    -->  (derived later)
  long double calculator_d28    = QLMetricsManager::getDoubleValue("routing", "num_clock_network");    // num_clock_network
  long double calculator_d29    = QLMetricsManager::getDoubleValue("routing", "num_dsp");              // num_dsp
  long double calculator_d30    = QLMetricsManager::getDoubleValue("routing", "num_bram");             // num_bram
  // ===================================================== Design Inputs From Metrics--
  // ===================================================== Design Inputs Derived++
  // calculator_d14    = 0;                       // num_input_cbx_cby        -->  (derived later)
  // calculator_d15    = 0;                       // num_input_xbar           -->  (derived later)
  //
  // num_input_cbx_cby = num_input_xbar = total_lut_inputs_used (from spreadsheet theory)
  // total_lut_inputs_used = 1*num_1_LUT + 2*num_2_LUT + ... + 6*num_6_LUT (from yosys metrics, we obtain these numbers)
  int num_1_LUT = QLMetricsManager::getIntValue("synthesis", "num_1_LUT");
  int num_2_LUT = QLMetricsManager::getIntValue("synthesis", "num_2_LUT");
  int num_3_LUT = QLMetricsManager::getIntValue("synthesis", "num_3_LUT");
  int num_4_LUT = QLMetricsManager::getIntValue("synthesis", "num_4_LUT");
  int num_5_LUT = QLMetricsManager::getIntValue("synthesis", "num_5_LUT");
  int num_6_LUT = QLMetricsManager::getIntValue("synthesis", "num_6_LUT");
  
  // note: we consider Adder Carry blocks as 3-LUTs, so account for those as well:
  int num_adder_carry = QLMetricsManager::getIntValue("synthesis", "num_adder_carry");
  num_3_LUT += num_adder_carry;
  
  int total_num_luts = num_1_LUT + num_2_LUT + num_3_LUT + num_4_LUT + num_5_LUT + num_6_LUT;
  int total_num_lut_inputs = (num_1_LUT*1) + (num_2_LUT*2) + 
                             (num_3_LUT*3) + (num_4_LUT*4) + 
                             (num_5_LUT*5) + (num_6_LUT*6);

  calculator_d14    = total_num_lut_inputs;       // num_input_cbx_cby
  calculator_d15    = total_num_lut_inputs;       // num_input_xbar


  // calculator_d18    = 0;                       // num_output_clb           -->  (derived later)
  //
  // num_output_clb == total_num_luts
  // TODO: num_output_clb = num_5_LUT + num_6_LUT (from spreadsheet) needs clarification
  calculator_d18 = total_num_luts;                // num_output_clb


  // calculator_d20    = 0;                       // num_output_cbx_cby       -->  (derived later)
  //
  // num_output_cbx_cby == num_output == calculator_d16
  calculator_d20 = calculator_d16;                // num_output_cbx_cby


  // calculator_d22    = 0;                       // num_lut                  -->  (derived later)
  //
  // num_lut = sum of all LUTs
  calculator_d22 = total_num_luts;


  // calculator_d26    = 0;                       // num_clb_ff               -->  (derived later)
  //
  // num_clb_ff = sum of all ff primitives : scour the cells_sim.v of every device, and 
  // ensure that we account for all possible ff primitives across them here.
  int total_num_ffs = 0;
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dffsre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dffnsre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sdffsre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sdffnsre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sh_dff");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dff");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dffn");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dffre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_dffnre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sdffre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sdffnre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sh_dffre");
  total_num_ffs += QLMetricsManager::getIntValue("synthesis", "num_sh_dffnre");


  
  calculator_d26 = total_num_ffs;                 // num_clb_ff


  // calculator_d27    = 0;                       // num_average_lut_input    -->  (derived later)
  //
  // num_average_lut_input (only for spreadsheet purposes) == num_lut_inputs/num_luts
  // avoid a NaN result if there are no LUTs in design.
  if (total_num_luts > 0) {
    calculator_d27 = ((long double)total_num_lut_inputs / total_num_luts);      // num_average_lut_input
  }

  if(power_estimation_dbg) {

    // std::cout <<">>> Inputs For Power Calculator Spreadsheet\n" << std::endl;
    power_analysis_debug_rpt << ">>> Inputs For Power Calculator Spreadsheet\n" << std::endl;

    // std::cout <<"calculator_d6  : " << std::left << std::setw(15) << calculator_d6 << "[Array X]" << std::endl;
    power_analysis_debug_rpt << "calculator_d6  : " << std::left << std::setw(15) << calculator_d6 << "[Array X]" << std::endl;
    
    // std::cout <<"calculator_d7  : " << std::left << std::setw(15) << calculator_d7 << "[Array Y]" << std::endl;
    power_analysis_debug_rpt << "calculator_d7  : " << std::left << std::setw(15) << calculator_d7 << "[Array Y]" << std::endl;

    // std::cout <<"calculator_d11 : " << std::left << std::setw(15) << calculator_d11 << "[INPUT NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d11 : " << std::left << std::setw(15) << calculator_d11 << "[INPUT NUM]" << std::endl;
    
    // std::cout <<"calculator_d12 : " << std::left << std::setw(15) << calculator_d12 << "[INPUT FF NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d12 : " << std::left << std::setw(15) << calculator_d12 << "[INPUT FF NUM]" << std::endl;

    // std::cout << "(do not input to xls)[INPUT CBX/CBY NUM] calculator_d14 : " << calculator_d14 << std::endl;
    // power_analysis_debug_rpt << "(do not input to xls)[INPUT CBX/CBY NUM] calculator_d14 : " << calculator_d14 << std::endl;

    // std::cout << "(do not input to xls)[INPUT XBAR NUM] calculator_d15 : " << calculator_d15 << std::endl;
    // power_analysis_debug_rpt << "(do not input to xls)[INPUT XBAR NUM] calculator_d15 : " << calculator_d15 << std::endl;

    // std::cout <<"calculator_d16 : " << std::left << std::setw(15) << calculator_d16 << "[OUTPUT NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d16 : " << std::left << std::setw(15) << calculator_d16 << "[OUTPUT NUM]" << std::endl;
    
    // std::cout <<"calculator_d17 : " << std::left << std::setw(15) << calculator_d17 << "[OUTPUT FF NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d17 : " << std::left << std::setw(15) << calculator_d17 << "[OUTPUT FF NUM]" << std::endl;

    // std::cout << "(do not input to xls)[OUTPUT CLB NUM] calculator_d18 : " << calculator_d18 << std::endl;
    // power_analysis_debug_rpt << "(do not input to xls)[OUTPUT CLB NUM] calculator_d18 : " << calculator_d18 << std::endl;

    // std::cout << "(do not input to xls)[OUTPUT CBX/CBY NUM] calculator_d20 : " << calculator_d20 << std::endl;
    // power_analysis_debug_rpt << "(do not input to xls)[OUTPUT CBX/CBY NUM] calculator_d20 : " << calculator_d20 << std::endl;

    // std::cout <<"calculator_d21 : " << std::left << std::setw(15) << calculator_d21 << "[TOTAL # SB NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d21 : " << std::left << std::setw(15) << calculator_d21 << "[TOTAL # SB NUM]" << std::endl;

    // std::cout <<"calculator_d22 : " << std::left << std::setw(15) << calculator_d22 << "[TOTAL # LUT NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d22 : " << std::left << std::setw(15) << calculator_d22 << "[TOTAL # LUT NUM]" << std::endl;

    // std::cout <<"calculator_d26 : " << std::left << std::setw(15) << calculator_d26 << "[TOTAL CLB FF Only NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d26 : " << std::left << std::setw(15) << calculator_d26 << "[TOTAL CLB FF Only NUM]" << std::endl;

    // std::cout <<"calculator_d27 : " << std::left << std::setw(15) << calculator_d27 << "[Average # of LUT input NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d27 : " << std::left << std::setw(15) << calculator_d27 << "[Average # of LUT input NUM]" << std::endl;

    // std::cout <<"calculator_d28 : " << std::left << std::setw(15) << calculator_d28 << "[CLOCK Network NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d28 : " << std::left << std::setw(15) << calculator_d28 << "[CLOCK Network NUM]" << std::endl;

    // std::cout <<"calculator_d29 : " << std::left << std::setw(15) << calculator_d29 << "[DSP NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d29 : " << std::left << std::setw(15) << calculator_d29 << "[DSP NUM]" << std::endl;

    // std::cout <<"calculator_d30 : " << std::left << std::setw(15) << calculator_d30 << "[BRAM (w/ sram) NUM]" << std::endl;
    power_analysis_debug_rpt << "calculator_d30 : " << std::left << std::setw(15) << calculator_d30 << "[BRAM (w/ sram) NUM]" << std::endl;

    // std::cout <<"\n\n" << std::endl;
    power_analysis_debug_rpt << "\n\n" << std::endl;
  }
  // ===================================================== Design Inputs Derived--
  // ===================================================== Design Inputs --

  // ===================================================== Calculate Frequencies ++
  // step 4: calculate all clock frequencies
  // Using the user inputs, and the design inputs establish frequencies for all components
  // first, the frequencies directly calculated using the user inputs:
  long double calculator_e11 = (calculator_e9 * calculator_f11 / 100);                // freq_input
  long double calculator_e15 = (calculator_e9 * calculator_f15 / 100);                // freq_xbar
  long double calculator_e16 = (calculator_e9 * calculator_f16 / 100);                // freq_output
  long double calculator_e18 = (calculator_e9 * calculator_f18 / 100);                // freq_output_clb
  long double calculator_e21 = (calculator_e9 * calculator_f21 / 100);                // freq_sb
  long double calculator_e22 = (calculator_e9 * calculator_f22 / 100);                // freq_lut
  long double calculator_e28 = (calculator_e9 * calculator_f28 / 100);                // freq_clock_network
  long double calculator_e29 = (calculator_e9 * calculator_f29 / 100);                // freq_dsp
  long double calculator_e30 = (calculator_e9 * calculator_f30 / 100);                // freq_bram

  // second, the frequencies derived from user inputs + design input correlation
  long double calculator_e12 = 0;                                             // freq_input_ff (not used currently)
  long double calculator_e13 = calculator_e21;                                // freq_input_sb == freq_sb
  long double calculator_e14 = calculator_e11;                                // freq_input_cbx_cby == freq_input
  long double calculator_e17 = 0;                                             // freq_output_ff (not used currently)
  long double calculator_e19 = calculator_e21;                                // freq_output_sb == freq_sb
  long double calculator_e20 = calculator_e16;                                // freq_output_cbx_cby == freq_output
  long double calculator_e23 = calculator_e22;                                // freq_lut_5_ff == freq_lut
  long double calculator_e24 = calculator_e22;                                // freq_lut_6 == freq_lut
  long double calculator_e25 = calculator_e24;                                // freq_lut_6_ff == freq_lut_6
  // ===================================================== Calculate Frequencies --

  // ===================================================== Dynamic Calculations ++
  // ===================================================== Dynamic Linear ++
  long double dynamic_linear_o5 = 
    (calculator_e28 > 0) ? (dynamic_linear_k5 * calculator_e28 / 2 * calculator_e28 / 2) + (dynamic_linear_l5 * calculator_e28 / 2) + dynamic_linear_m5 : 0;
  long double dynamic_linear_o6 = 
    (calculator_e11 > 0) ? (dynamic_linear_k6 * calculator_e11 * calculator_e11) + (dynamic_linear_l6* calculator_e11) + dynamic_linear_m6 : 0;
  long double dynamic_linear_o7 = 
    (calculator_e12 > 0) ? (dynamic_linear_k7 * calculator_e12 * calculator_e12) + (dynamic_linear_l7 * calculator_e12) + dynamic_linear_m7 : 0;
  long double dynamic_linear_o8 =
    (calculator_e13 > 0) ? (dynamic_linear_k8 * calculator_e13 * calculator_e13) + (dynamic_linear_l8 * calculator_e13) + dynamic_linear_m8 : 0;
  long double dynamic_linear_o9 = 
    calculator_e14 > 0 ? (dynamic_linear_k9 * calculator_e14 * calculator_e14) + (dynamic_linear_l9 * calculator_e14) + dynamic_linear_m9 : 0;
  long double dynamic_linear_o10 = 
    calculator_e15 > 0 ? (dynamic_linear_k10 * calculator_e15 * calculator_e15) + (dynamic_linear_l10 * calculator_e15) + dynamic_linear_m10 : 0;
  long double dynamic_linear_o12 = 
    calculator_e16 > 0 ? (dynamic_linear_k12 * calculator_e16 * calculator_e16) + (dynamic_linear_l12 * calculator_e16) + dynamic_linear_m12 : 0;
  long double dynamic_linear_o13 = 
    calculator_e17 > 0 ? (dynamic_linear_k13 * calculator_e17 * calculator_e17) + (dynamic_linear_l13 * calculator_e17) + dynamic_linear_m13 : 0;
  long double dynamic_linear_o14 = 
    calculator_e19 > 0 ? (dynamic_linear_k14 * calculator_e19 * calculator_e19) + (dynamic_linear_l14 * calculator_e19) + dynamic_linear_m14 : 0;
  long double dynamic_linear_o15 = 
    calculator_e20 > 0 ? (dynamic_linear_k15 * calculator_e20 * calculator_e20) + (dynamic_linear_l15 * calculator_e20) + dynamic_linear_m15 : 0;
  long double dynamic_linear_o16 = 
    calculator_e18 > 0 ? (dynamic_linear_k16 * calculator_e18 * calculator_e18) + (dynamic_linear_l16 * calculator_e18) + dynamic_linear_m16 : 0;
  long double dynamic_linear_o35 = 
    calculator_e22 > 0 ? (dynamic_linear_k35 * calculator_e22 * calculator_e22) + (dynamic_linear_l35 * calculator_e22) + dynamic_linear_m35 : 0;
  long double dynamic_linear_o36 = 
    calculator_e23 > 0 ? (dynamic_linear_k36 * calculator_e23 * calculator_e23) + (dynamic_linear_l36 * calculator_e23) + dynamic_linear_m36 : 0;
  long double dynamic_linear_o37 = 
    calculator_e24 > 0 ? (dynamic_linear_k37 * calculator_e24 * calculator_e24) + (dynamic_linear_l37 * calculator_e24) + dynamic_linear_m37 : 0;
  long double dynamic_linear_o38 = 
    calculator_e25 > 0 ? (dynamic_linear_k38 * calculator_e25 * calculator_e25) + (dynamic_linear_l38 * calculator_e25) + dynamic_linear_m38 : 0;
  
  if(power_estimation_dbg) {
      
    // std::cout <<"dynamic_linear_o5  : " << dynamic_linear_o5 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o5  : " << dynamic_linear_o5 << std::endl;

    // std::cout <<"dynamic_linear_o6  : " << dynamic_linear_o6 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o6  : " << dynamic_linear_o6 << std::endl;
    
    // std::cout <<"dynamic_linear_o7  : " << dynamic_linear_o7 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o7  : " << dynamic_linear_o7 << std::endl;

    // std::cout <<"dynamic_linear_o8  : " << dynamic_linear_o8 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o8  : " << dynamic_linear_o8 << std::endl;

    // std::cout <<"dynamic_linear_o9  : " << dynamic_linear_o9 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o9  : " << dynamic_linear_o9 << std::endl;

    // std::cout <<"dynamic_linear_o10 : " << dynamic_linear_o10 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o10 : " << dynamic_linear_o10 << std::endl;
    
    // std::cout <<"dynamic_linear_o12 : " << dynamic_linear_o12 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o12 : " << dynamic_linear_o12 << std::endl;

    // std::cout <<"dynamic_linear_o13 : " << dynamic_linear_o13 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o13 : " << dynamic_linear_o13 << std::endl;

    // std::cout <<"dynamic_linear_o14 : " << dynamic_linear_o14 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o14 : " << dynamic_linear_o14 << std::endl;

    // std::cout <<"dynamic_linear_o15 : " << dynamic_linear_o15 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o15 : " << dynamic_linear_o15 << std::endl;

    // std::cout <<"dynamic_linear_o16 : " << dynamic_linear_o16 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o16 : " << dynamic_linear_o16 << std::endl;

    // std::cout <<"dynamic_linear_o35 : " << dynamic_linear_o35 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o35 : " << dynamic_linear_o35 << std::endl;

    // std::cout <<"dynamic_linear_o36 : " << dynamic_linear_o36 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o36 : " << dynamic_linear_o36 << std::endl;

    // std::cout <<"dynamic_linear_o37 : " << dynamic_linear_o37 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o37 : " << dynamic_linear_o37 << std::endl;

    // std::cout <<"dynamic_linear_o38 : " << dynamic_linear_o38 << std::endl;
    power_analysis_debug_rpt << "dynamic_linear_o38 : " << dynamic_linear_o38 << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== Dynamic Linear --
  // ===================================================== Dynamic Poly ++
  long double dynamic_poly_o5 = 
    (calculator_e28 > 0) ? (dynamic_poly_k5 * calculator_e28 / 2 * calculator_e28 / 2) + (dynamic_poly_l5 * calculator_e28 / 2) + dynamic_poly_m5 : 0;
  long double dynamic_poly_o6 = 
    (calculator_e11 > 0) ? (dynamic_poly_k6 * calculator_e11 * calculator_e11) + (dynamic_poly_l6* calculator_e11) + dynamic_poly_m6 : 0;
  long double dynamic_poly_o7 = 
    (calculator_e12 > 0) ? (dynamic_poly_l7 * std::log(calculator_e12)) + dynamic_poly_m7 : 0;
  long double dynamic_poly_o8 =
    (calculator_e13 > 0) ? (dynamic_poly_k8 * calculator_e13 * calculator_e13) + (dynamic_poly_l8 * calculator_e13) + dynamic_poly_m8 : 0;
  long double dynamic_poly_o9 = 
    calculator_e14 > 0 ? (dynamic_poly_k9 * calculator_e14 * calculator_e14) + (dynamic_poly_l9 * calculator_e14) + dynamic_poly_m9 : 0;
  long double dynamic_poly_o10 = 
    calculator_e15 > 0 ? (dynamic_poly_k10 * calculator_e15 * calculator_e15) + (dynamic_poly_l10 * calculator_e15) + dynamic_poly_m10 : 0;
  long double dynamic_poly_o12 = 
    calculator_e16 > 0 ? (dynamic_poly_k12 * calculator_e16 * calculator_e16) + (dynamic_poly_l12 * calculator_e16) + dynamic_poly_m12 : 0;
  long double dynamic_poly_o13 = 
    calculator_e17 > 0 ? (dynamic_poly_k13 * calculator_e17 * calculator_e17) + (dynamic_poly_l13 * calculator_e17) + dynamic_poly_m13 : 0;
  long double dynamic_poly_o14 = 
    calculator_e19 > 0 ? (dynamic_poly_k14 * calculator_e19 * calculator_e19) + (dynamic_poly_l14 * calculator_e19) + dynamic_poly_m14 : 0;
  long double dynamic_poly_o15 = 
    calculator_e20 > 0 ? (dynamic_poly_k15 * calculator_e20 * calculator_e20) + (dynamic_poly_l15 * calculator_e20) + dynamic_poly_m15 : 0;
  long double dynamic_poly_o16 = 
    calculator_e18 > 0 ? (dynamic_poly_k16 * calculator_e18 * calculator_e18) + (dynamic_poly_l16 * calculator_e18) + dynamic_poly_m16 : 0;
  long double dynamic_poly_o35 = 
    calculator_e22 > 0 ? (dynamic_poly_k35 * calculator_e22 * calculator_e22) + (dynamic_poly_l35 * calculator_e22) + dynamic_poly_m35 : 0;
  long double dynamic_poly_o36 = 
    calculator_e23 > 0 ? (dynamic_poly_k36 * calculator_e23 * calculator_e23) + (dynamic_poly_l36 * calculator_e23) + dynamic_poly_m36 : 0;
  long double dynamic_poly_o37 = 
    calculator_e24 > 0 ? (dynamic_poly_k37 * calculator_e24 * calculator_e24) + (dynamic_poly_l37 * calculator_e24) + dynamic_poly_m37 : 0;
  long double dynamic_poly_o38 = 
    calculator_e25 > 0 ? (dynamic_poly_k38 * calculator_e25 * calculator_e25) + (dynamic_poly_l38 * calculator_e25) + dynamic_poly_m38 : 0;

  if(power_estimation_dbg) {
      
    // std::cout <<"dynamic_poly_o5  : " << dynamic_poly_o5 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o5  : " << dynamic_poly_o5 << std::endl;

    // std::cout <<"dynamic_poly_o6  : " << dynamic_poly_o6 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o6  : " << dynamic_poly_o6 << std::endl;
    
    // std::cout <<"dynamic_poly_o7  : " << dynamic_poly_o7 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o7  : " << dynamic_poly_o7 << std::endl;

    // std::cout <<"dynamic_poly_o8  : " << dynamic_poly_o8 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o8  : " << dynamic_poly_o8 << std::endl;

    // std::cout <<"dynamic_poly_o9  : " << dynamic_poly_o9 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o9  : " << dynamic_poly_o9 << std::endl;

    // std::cout <<"dynamic_poly_o10 : " << dynamic_poly_o10 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o10 : " << dynamic_poly_o10 << std::endl;
    
    // std::cout <<"dynamic_poly_o12 : " << dynamic_poly_o12 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o12 : " << dynamic_poly_o12 << std::endl;

    // std::cout <<"dynamic_poly_o13 : " << dynamic_poly_o13 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o13 : " << dynamic_poly_o13 << std::endl;

    // std::cout <<"dynamic_poly_o14 : " << dynamic_poly_o14 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o14 : " << dynamic_poly_o14 << std::endl;

    // std::cout <<"dynamic_poly_o15 : " << dynamic_poly_o15 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o15 : " << dynamic_poly_o15 << std::endl;

    // std::cout <<"dynamic_poly_o16 : " << dynamic_poly_o16 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o16 : " << dynamic_poly_o16 << std::endl;

    // std::cout <<"dynamic_poly_o35 : " << dynamic_poly_o35 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o35 : " << dynamic_poly_o35 << std::endl;

    // std::cout <<"dynamic_poly_o36 : " << dynamic_poly_o36 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o36 : " << dynamic_poly_o36 << std::endl;

    // std::cout <<"dynamic_poly_o37 : " << dynamic_poly_o37 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o37 : " << dynamic_poly_o37 << std::endl;

    // std::cout <<"dynamic_poly_o38 : " << dynamic_poly_o38 << std::endl;
    power_analysis_debug_rpt << "dynamic_poly_o38 : " << dynamic_poly_o38 << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== Dynamic Poly --
  // ===================================================== Dynamic ++
  long double dynamic_o5 = 0;
  if(calculator_e28 <= 10) {
    dynamic_o5 = dynamic_linear_o5 > 0 ? dynamic_linear_o5 : 0;
  }
  else {
    dynamic_o5 = dynamic_poly_o5 > 0 ? dynamic_poly_o5 : 0;
  }

  long double dynamic_o6 = 0;
  if(calculator_e11 <= 10) {
    dynamic_o6 = dynamic_linear_o6 > 0 ? dynamic_linear_o6 : 0;
  }
  else {
    dynamic_o6 = dynamic_poly_o6 > 0 ? dynamic_poly_o6 : 0;
  }

  long double dynamic_o7 = 0;
  if(calculator_e12 <= 10) {
    dynamic_o7 = dynamic_linear_o7 > 0 ? dynamic_linear_o7 : 0;
  }
  else {
    dynamic_o7 = dynamic_poly_o7 > 0 ? dynamic_poly_o7 : 0;
  }

  long double dynamic_o8 = 0;
  if(calculator_e13 <= 10) {
    dynamic_o8 = dynamic_linear_o8 > 0 ? dynamic_linear_o8 : 0;
  }
  else {
    dynamic_o8 = dynamic_poly_o8 > 0 ? dynamic_poly_o8 : 0;
  }

  long double dynamic_o9 = 0;
  if(calculator_e14 <= 10) {
    dynamic_o9 = dynamic_linear_o9 > 0 ? dynamic_linear_o9 : 0;
  }
  else {
    dynamic_o9 = dynamic_poly_o9 > 0 ? dynamic_poly_o9 : 0;
  }
  
  long double dynamic_o10 = 0;
  if(calculator_e15 <= 10) {
    dynamic_o10 = dynamic_linear_o10 > 0 ? dynamic_linear_o10 : 0;
  }
  else {
    dynamic_o10 = dynamic_poly_o10 > 0 ? dynamic_poly_o10 : 0;
  }
  
  long double dynamic_o12 = 0;
  if(calculator_e16 <= 10) {
    dynamic_o12 = dynamic_linear_o12 > 0 ? dynamic_linear_o12 : 0;
  }
  else {
    dynamic_o12 = dynamic_poly_o12 > 0 ? dynamic_poly_o12 : 0;
  }
  
  long double dynamic_o13 = 0;
  if(calculator_e17 <= 10) {
    dynamic_o13 = dynamic_linear_o13 > 0 ? dynamic_linear_o13 : 0;
  }
  else {
    dynamic_o13 = dynamic_poly_o13 > 0 ? dynamic_poly_o13 : 0;
  }
  
  long double dynamic_o14 = 0;
  if(calculator_e19 <= 10) {
    dynamic_o14 = dynamic_linear_o14 > 0 ? dynamic_linear_o14 : 0;
  }
  else {
    dynamic_o14 = dynamic_poly_o14 > 0 ? dynamic_poly_o14 : 0;
  }

  long double dynamic_o15 = 0;
  if(calculator_e20 <= 10) {
    dynamic_o15 = dynamic_linear_o15 > 0 ? dynamic_linear_o15 : 0;
  }
  else {
    dynamic_o15 = dynamic_poly_o15 > 0 ? dynamic_poly_o15 : 0;
  }

  long double dynamic_o16 = 0;
  if(calculator_e18 <= 10) {
    dynamic_o16 = dynamic_linear_o16 > 0 ? dynamic_linear_o16 : 0;
  }
  else {
    dynamic_o16 = dynamic_poly_o16 > 0 ? dynamic_poly_o16 : 0;
  }

  long double dynamic_o35 = 0;
  if(calculator_e22 <= 10) {
    dynamic_o35 = dynamic_linear_o35 > 0 ? dynamic_linear_o35 : 0;
  }
  else {
    dynamic_o35 = dynamic_poly_o35 > 0 ? dynamic_poly_o35 : 0;
  }

  long double dynamic_o36 = 0;
  if(calculator_e23 <= 10) {
    dynamic_o36 = dynamic_linear_o36 > 0 ? dynamic_linear_o36 : 0;
  }
  else {
    dynamic_o36 = dynamic_poly_o36 > 0 ? dynamic_poly_o36 : 0;
  }

  long double dynamic_o37 = 0;
  if(calculator_e24 <= 10) {
    dynamic_o37 = dynamic_linear_o37 > 0 ? dynamic_linear_o37 : 0;
  }
  else {
    dynamic_o37 = dynamic_poly_o37 > 0 ? dynamic_poly_o37 : 0;
  }

  long double dynamic_o38 = 0;
  if(calculator_e25 <= 10) {
    dynamic_o38 = dynamic_linear_o38 > 0 ? dynamic_linear_o38 : 0;
  }
  else {
    dynamic_o38 = dynamic_poly_o38 > 0 ? dynamic_poly_o38 : 0;
  }
  
  if(power_estimation_dbg) {
      
    // std::cout <<"dynamic_o5  : " << dynamic_o5 << std::endl;
    power_analysis_debug_rpt << "dynamic_o5  : " << dynamic_o5 << std::endl;

    // std::cout <<"dynamic_o6  : " << dynamic_o6 << std::endl;
    power_analysis_debug_rpt << "dynamic_o6  : " << dynamic_o6 << std::endl;
    
    // std::cout <<"dynamic_o7  : " << dynamic_o7 << std::endl;
    power_analysis_debug_rpt << "dynamic_o7  : " << dynamic_o7 << std::endl;

    // std::cout <<"dynamic_o8  : " << dynamic_o8 << std::endl;
    power_analysis_debug_rpt << "dynamic_o8  : " << dynamic_o8 << std::endl;

    // std::cout <<"dynamic_o9  : " << dynamic_o9 << std::endl;
    power_analysis_debug_rpt << "dynamic_o9  : " << dynamic_o9 << std::endl;

    // std::cout <<"dynamic_o10 : " << dynamic_o10 << std::endl;
    power_analysis_debug_rpt << "dynamic_o10 : " << dynamic_o10 << std::endl;
    
    // std::cout <<"dynamic_o12 : " << dynamic_o12 << std::endl;
    power_analysis_debug_rpt << "dynamic_o12 : " << dynamic_o12 << std::endl;

    // std::cout <<"dynamic_o13 : " << dynamic_o13 << std::endl;
    power_analysis_debug_rpt << "dynamic_o13 : " << dynamic_o13 << std::endl;

    // std::cout <<"dynamic_o14 : " << dynamic_o14 << std::endl;
    power_analysis_debug_rpt << "dynamic_o14 : " << dynamic_o14 << std::endl;

    // std::cout <<"dynamic_o15 : " << dynamic_o15 << std::endl;
    power_analysis_debug_rpt << "dynamic_o15 : " << dynamic_o15 << std::endl;

    // std::cout <<"dynamic_o16 : " << dynamic_o16 << std::endl;
    power_analysis_debug_rpt << "dynamic_o16 : " << dynamic_o16 << std::endl;

    // std::cout <<"dynamic_o35 : " << dynamic_o35 << std::endl;
    power_analysis_debug_rpt << "dynamic_o35 : " << dynamic_o35 << std::endl;

    // std::cout <<"dynamic_o36 : " << dynamic_o36 << std::endl;
    power_analysis_debug_rpt << "dynamic_o36 : " << dynamic_o36 << std::endl;

    // std::cout <<"dynamic_o37 : " << dynamic_o37 << std::endl;
    power_analysis_debug_rpt << "dynamic_o37 : " << dynamic_o37 << std::endl;

    // std::cout <<"dynamic_o38 : " << dynamic_o38 << std::endl;
    power_analysis_debug_rpt << "dynamic_o38 : " << dynamic_o38 << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== Dynamic --
  // ===================================================== Dynamic Calculations --

  // ===================================================== FFB Calculations ++
  // ===================================================== FFB Linear ++
  long double ffb_linear_n3 = 
    (calculator_e29 > 0) ? (ffb_linear_k3 * calculator_e29 * calculator_e29) + (ffb_linear_l3 * calculator_e29) + ffb_linear_m3 : 0;
  long double ffb_linear_n4 = 
    (calculator_e30 > 0) ? (ffb_linear_k4 * calculator_e30 * calculator_e30) + (ffb_linear_l4* calculator_e30) + ffb_linear_m4 : 0;
  
  if(power_estimation_dbg) {
      
    // std::cout <<"ffb_linear_n3 : " << ffb_linear_n3 << std::endl;
    power_analysis_debug_rpt << "ffb_linear_n3 : " << ffb_linear_n3 << std::endl;

    // std::cout <<"ffb_linear_n4 : " << ffb_linear_n4 << std::endl;
    power_analysis_debug_rpt << "ffb_linear_n4 : " << ffb_linear_n4 << std::endl;
    
    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== FFB Linear --
  // ===================================================== FFB Poly ++
  long double ffb_poly_n3 = 
    (calculator_e29 > 0) ? (ffb_poly_k3 * calculator_e29 * calculator_e29) + (ffb_poly_l3 * calculator_e29) + ffb_poly_m3 : 0;
  long double ffb_poly_n4 = 
    (calculator_e30 > 0) ? (ffb_poly_k4 * calculator_e30 * calculator_e30) + (ffb_poly_l4* calculator_e30) + ffb_poly_m4 : 0;
  
  if(power_estimation_dbg) {
      
    // std::cout <<"ffb_poly_n3 : " << ffb_poly_n3 << std::endl;
    power_analysis_debug_rpt << "ffb_poly_n3 : " << ffb_poly_n3 << std::endl;

    // std::cout <<"ffb_poly_n4 : " << ffb_poly_n4 << std::endl;
    power_analysis_debug_rpt << "ffb_poly_n4 : " << ffb_poly_n4 << std::endl;
    
    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== FFB Poly --
  // ===================================================== FFB ++
  long double ffb_n3 = 0;
  if(calculator_e29 <= 10) {
    ffb_n3 = ffb_linear_n3 > 0 ? ffb_linear_n3 : 0;
  }
  else {
    ffb_n3 = ffb_poly_n3 > 0 ? ffb_poly_n3 : 0;
  }

  long double ffb_n4 = 0;
  if(calculator_e30 <= 10) {
    ffb_n4 = ffb_linear_n4 > 0 ? ffb_linear_n4 : 0;
  }
  else {
    ffb_n4 = ffb_poly_n4 > 0 ? ffb_poly_n4 : 0;
  }

  if(power_estimation_dbg) {
      
    // std::cout <<"ffb_n3 : " << ffb_n3 << std::endl;
    power_analysis_debug_rpt << "ffb_n3 : " << ffb_n3 << std::endl;

    // std::cout <<"ffb_n4 : " << ffb_n4 << std::endl;
    power_analysis_debug_rpt << "ffb_n4 : " << ffb_n4 << std::endl;
    
    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== FFB --
  // ===================================================== FFB Calculations --

  // ===================================================== ClockNetwork Calculations ++
  long double clock_network_n93 = dynamic_o5 / clock_network_l90;
  long double clock_network_t96 = clock_network_n93 * clock_network_p95;

  if(power_estimation_dbg) {
      
    // std::cout <<"clock_network_n93 : " << clock_network_n93 << std::endl;
    power_analysis_debug_rpt << "clock_network_n93 : " << clock_network_n93 << std::endl;

    // std::cout <<"clock_network_t96 : " << clock_network_t96 << std::endl;
    power_analysis_debug_rpt << "clock_network_t96 : " << clock_network_t96 << std::endl;
    
    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== ClockNetwork Calculations --
  
  // ===================================================== Final Power Calculations ++
  power_dynamic = 0;

  power_dynamic += (calculator_d11 * dynamic_o6) +
                   (calculator_d12 * dynamic_o7) +
                   (calculator_d21 * (dynamic_o8 + dynamic_o14) / 2) +
                   (calculator_d14 * dynamic_o9) +
                   (calculator_d15 * dynamic_o10);

  power_dynamic += (calculator_d16 * dynamic_o12) +
                   (calculator_d17 * dynamic_o13) +
                   (calculator_d20 * dynamic_o15) +
                   (calculator_d18 * dynamic_o16);

  if(calculator_d27 <= 5) {
    power_dynamic += (calculator_d22 * dynamic_o35);
  }
  else if (calculator_d27 <= 6) {
    power_dynamic += (calculator_d22 * dynamic_o37);
  }
  // else we have a problem!! avg_lut_inputs cannot be > 6?

  power_dynamic += (std::max((dynamic_o36 - dynamic_o35), (dynamic_o38 - dynamic_o37)) * calculator_d26);

  power_dynamic += (calculator_d28 * (std::ceil(calculator_d26/(80*clock_network_n97)) * clock_network_t96 ));

  power_dynamic /= (0.8 * 0.8);

  power_dynamic *= (calculator_d8 * calculator_d8);

  power_dynamic += (calculator_d29 * ffb_n3);

  power_dynamic += (calculator_d30 * ffb_n4);

  if(power_estimation_dbg) {
      
    // std::cout <<"power_dynamic : " << power_dynamic << " mW" << std::endl;
    power_analysis_debug_rpt << "power_dynamic : " << power_dynamic << " mW" << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }

  // ===================================================== Final Power Calculations --

  // close the file stream
  power_analysis_debug_rpt.close();

  return power_dynamic;
}


long double CompilerOpenFPGA_ql::PowerEstimator_Leakage() {

  long double power_leakage = 0;

  // ===================================================== Constants ++
  long double leakage_g5          = 55.8446;
  long double leakage_g6          = 65.0463;
  long double leakage_g7          = 117.2489;
  long double leakage_g8          = 56.5525;
  long double leakage_g9          = 60.4654;
  long double leakage_g10         = 115.2604;
  long double leakage_g11         = 10892.9;
  long double leakage_g12         = 67.3532;
  long double leakage_g13         = 67.3532;
  long double leakage_g14         = 67.3562;
  long double leakage_g15         = 67.3562;
  long double leakage_g16         = 170.2399;
  long double leakage_g17         = 1362.8;
  long double leakage_g18         = 181.4737;
  long double leakage_g19         = 1406.2;
  long double leakage_g20         = 3911;
  long double leakage_g21         = 1818;
  long double leakage_g22         = 623.4635;
  long double leakage_g23         = 1645.5;
  long double leakage_g24         = 415.1491;
  long double leakage_g25         = 61777.6;
  // leakage_g26 is derived!
  long double leakage_g27         = 488.3011;
  long double leakage_g28         = 20293;
  // ===================================================== Constants --

  // ===================================================== Design Inputs ++
  long double calculator_d6       = QLMetricsManager::getDoubleValue("routing", "device_size_x");        // array_x
  long double calculator_d7       = QLMetricsManager::getDoubleValue("routing", "device_size_y");        // array_y
  long double calculator_d29      = QLMetricsManager::getDoubleValue("routing", "num_dsp");              // num_dsp
  long double calculator_d30      = QLMetricsManager::getDoubleValue("routing", "num_bram");             // num_bram

  // check for user inputs power json:
  if( QLSettingsManager::getJson("power") == nullptr ) {

    // there are no power_inputs parameters required for power analysis!
    Message("\n>> power_inputs in JSON unavailable, skipping power analysis!");

    return power_leakage;
  }

  // check if the user has explicitly enabled power estimation:
  if( QLSettingsManager::getStringValue("power", "power_outputs", "leakage_power") != "checked" ) {

    // user has not enabled power analysis
    Message("\n>> leakage_power is disabled in JSON, skipping power analysis!");

    return power_leakage;
  }

  // enable debug prints if specified in JSON
  bool power_estimation_dbg = false;
  std::ofstream power_analysis_debug_rpt;

  if( QLSettingsManager::getStringValue("power", "power_outputs", "debug") == "checked" ) {

    power_estimation_dbg = true;

    // write power analysis debug prints into file
    std::filesystem::path power_analysis_debug_rpt_filepath = 
      std::filesystem::path(ProjManager()->projectPath()) / std::string("power_analysis_debug.rpt");

    // NOTE: in leakage, we append to existing file!!
    power_analysis_debug_rpt.open(power_analysis_debug_rpt_filepath, std::ios_base::app);

    if(!power_analysis_debug_rpt) {
      ErrorMessage("File: " + power_analysis_debug_rpt_filepath.string() + " could not be opened");
      power_analysis_debug_rpt.close();
      return power_leakage;
    }
  }

  // all of these are already output as part of the dynamic power estimation
  // if(power_estimation_dbg) {

  //   std::cout << "[Array X] calculator_d6 : " << calculator_d6 << std::endl;
  //   power_analysis_debug_rpt << "[Array X] calculator_d6 : " << calculator_d6 << std::endl;

  //   std::cout << "[Array Y] calculator_d7 : " << calculator_d7 << std::endl;
  //   power_analysis_debug_rpt << "[Array Y] calculator_d7 : " << calculator_d7 << std::endl;

  //   std::cout << "[DSP NUM] calculator_d29 : " << calculator_d29 << std::endl;
  //   power_analysis_debug_rpt << "[DSP NUM] calculator_d29 : " << calculator_d29 << std::endl;

  //   std::cout << "[BRAM (w/ sram) NUM] calculator_d30 : " << calculator_d30 << std::endl;
  //   power_analysis_debug_rpt << "[BRAM (w/ sram) NUM] calculator_d30 : " << calculator_d30 << std::endl;

  //   std::cout << "\n" << std::endl;
  //   power_analysis_debug_rpt << "\n" << std::endl;
  // }
  // ===================================================== Design Inputs --

  // ===================================================== Leakage Calculations ++
  // G26=G25-(G5*2+G6*2+G7*2)-(G8*2+G9*2+G10*2)-(G11*4)-(G12*2+G13*2+G14*2+G15*2)-SUM(G16:G24)
  long double leakage_g26   = 0;
  leakage_g26 += leakage_g25;
  leakage_g26 -= ((leakage_g5 * 2) + (leakage_g6 * 2) + (leakage_g7 * 2));
  leakage_g26 -= ((leakage_g8 * 2) + (leakage_g9 * 2) + (leakage_g10 * 2));
  leakage_g26 -= (leakage_g11 * 4);
  leakage_g26 -= ((leakage_g12 * 2) + (leakage_g13 * 2) + (leakage_g14 * 2) + (leakage_g15 * 2));
  leakage_g26 -= (leakage_g16 + leakage_g17 + leakage_g18 + leakage_g19 + leakage_g20 + leakage_g21 + leakage_g22 + leakage_g23 + leakage_g24);
  
  long double leakage_l5  = leakage_g5 * calculator_d6;
  long double leakage_l6  = leakage_g6 * (calculator_d7 - 1) * calculator_d6;
  long double leakage_l7  = leakage_g7 * calculator_d6;
  long double leakage_l8  = leakage_g8 * calculator_d7;
  long double leakage_l9  = leakage_g9 * (calculator_d6 - 1) * calculator_d7;
  long double leakage_l10 = leakage_g10 * calculator_d7;
  long double leakage_l11 = leakage_g11 * calculator_d6 * calculator_d7;
  long double leakage_l12 = leakage_g12 * calculator_d6;
  long double leakage_l13 = leakage_g14 * calculator_d7; // TODO clarify if mistake
  long double leakage_l14 = leakage_g14 * calculator_d7;
  long double leakage_l15 = leakage_g15 * calculator_d6;
  long double leakage_l16 = leakage_g16;
  long double leakage_l17 = leakage_g17 * (calculator_d7 - 1);
  long double leakage_l18 = leakage_g18;
  long double leakage_l19 = leakage_g19 * (calculator_d6 - 1);
  long double leakage_l20 = leakage_g20 * (calculator_d6 - 1) * (calculator_d7 - 1);
  long double leakage_l21 = leakage_g21 * (calculator_d6 - 1);
  long double leakage_l22 = leakage_g22;
  long double leakage_l23 = leakage_g23 * (calculator_d7 - 1);
  long double leakage_l24 = leakage_g24;
  long double leakage_l25 = 0;
  long double leakage_l26 = leakage_g26 * (calculator_d6 / 2 * calculator_d7 / 2);
  long double leakage_l27 = leakage_g27 * (calculator_d29);
  long double leakage_l28 = leakage_g28 * (calculator_d30);

  if(power_estimation_dbg) {

    // std::cout <<"leakage_g26 : " << leakage_g26 << std::endl;
    power_analysis_debug_rpt << "leakage_g26 : " << leakage_g26 << std::endl;

    // std::cout <<"leakage_l5  : " << leakage_l5 << std::endl;
    power_analysis_debug_rpt << "leakage_l5  : " << leakage_l5 << std::endl;

    // std::cout <<"leakage_l6  : " << leakage_l6 << std::endl;
    power_analysis_debug_rpt << "leakage_l6  : " << leakage_l6 << std::endl;

    // std::cout <<"leakage_l7  : " << leakage_l7 << std::endl;
    power_analysis_debug_rpt << "leakage_l7  : " << leakage_l7 << std::endl;

    // std::cout <<"leakage_l8  : " << leakage_l8 << std::endl;
    power_analysis_debug_rpt << "leakage_l8  : " << leakage_l8 << std::endl;

    // std::cout <<"leakage_l9  : " << leakage_l9 << std::endl;
    power_analysis_debug_rpt << "leakage_l9  : " << leakage_l9 << std::endl;

    // std::cout <<"leakage_l10 : " << leakage_l10 << std::endl;
    power_analysis_debug_rpt << "leakage_l10 : " << leakage_l10 << std::endl;

    // std::cout <<"leakage_l11 : " << leakage_l11 << std::endl;
    power_analysis_debug_rpt << "leakage_l11 : " << leakage_l11 << std::endl;

    // std::cout <<"leakage_l12 : " << leakage_l12 << std::endl;
    power_analysis_debug_rpt << "leakage_l12 : " << leakage_l12 << std::endl;

    // std::cout <<"leakage_l13 : " << leakage_l13 << std::endl;
    power_analysis_debug_rpt << "leakage_l13 : " << leakage_l13 << std::endl;

    // std::cout <<"leakage_l14 : " << leakage_l14 << std::endl;
    power_analysis_debug_rpt << "leakage_l14 : " << leakage_l14 << std::endl;

    // std::cout <<"leakage_l15 : " << leakage_l15 << std::endl;
    power_analysis_debug_rpt << "leakage_l15 : " << leakage_l15 << std::endl;

    // std::cout <<"leakage_l16 : " << leakage_l16 << std::endl;
    power_analysis_debug_rpt << "leakage_l16 : " << leakage_l16 << std::endl;
    
    // std::cout <<"leakage_l17 : " << leakage_l17 << std::endl;
    power_analysis_debug_rpt << "leakage_l17 : " << leakage_l17 << std::endl;

    // std::cout <<"leakage_l18 : " << leakage_l18 << std::endl;
    power_analysis_debug_rpt << "leakage_l18 : " << leakage_l18 << std::endl;

    // std::cout <<"leakage_l19 : " << leakage_l19 << std::endl;
    power_analysis_debug_rpt << "leakage_l19 : " << leakage_l19 << std::endl;

    // std::cout <<"leakage_l20 : " << leakage_l20 << std::endl;
    power_analysis_debug_rpt << "leakage_l20 : " << leakage_l20 << std::endl;

    // std::cout <<"leakage_l21 : " << leakage_l21 << std::endl;
    power_analysis_debug_rpt << "leakage_l21 : " << leakage_l21 << std::endl;

    // std::cout <<"leakage_l22 : " << leakage_l22 << std::endl;
    power_analysis_debug_rpt << "leakage_l22 : " << leakage_l22 << std::endl;

    // std::cout <<"leakage_l23 : " << leakage_l23 << std::endl;
    power_analysis_debug_rpt << "leakage_l23 : " << leakage_l23 << std::endl;

    // std::cout <<"leakage_l24 : " << leakage_l24 << std::endl;
    power_analysis_debug_rpt << "leakage_l24 : " << leakage_l24 << std::endl;

    // std::cout <<"leakage_l25 : " << leakage_l25 << std::endl;
    power_analysis_debug_rpt << "leakage_l25 : " << leakage_l25 << std::endl;

    // std::cout <<"leakage_l26 : " << leakage_l26 << std::endl;
    power_analysis_debug_rpt << "leakage_l26 : " << leakage_l26 << std::endl;

    // std::cout <<"leakage_l27 : " << leakage_l27 << std::endl;
    power_analysis_debug_rpt << "leakage_l27 : " << leakage_l27 << std::endl;

    // std::cout <<"leakage_l28 : " << leakage_l28 << std::endl;
    power_analysis_debug_rpt << "leakage_l28 : " << leakage_l28 << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== Leakage Calculations --

  // ===================================================== Final Leakage Power Calculations ++
  power_leakage = 
    (
      leakage_l5  +
      leakage_l6  +
      leakage_l7  +
      leakage_l8  +
      leakage_l9  +
      leakage_l10 +
      leakage_l11 +
      leakage_l12 +
      leakage_l13 +
      leakage_l14 +
      leakage_l15 +
      leakage_l16 +
      leakage_l17 +
      leakage_l18 +
      leakage_l19 +
      leakage_l20 +
      leakage_l21 +
      leakage_l22 +
      leakage_l23 +
      leakage_l24 +
      leakage_l25 +
      leakage_l26 +
      leakage_l27 +
      leakage_l28
    )
    /
    1000000;

  if(power_estimation_dbg) {
      
    // std::cout <<"power_leakage : " << power_leakage << " mW" << std::endl;
    power_analysis_debug_rpt << "power_leakage : " << power_leakage << " mW" << std::endl;

    // std::cout <<"\n" << std::endl;
    power_analysis_debug_rpt << "\n" << std::endl;
  }
  // ===================================================== Final Leakage Power Calculations --

  // close the file stream
  power_analysis_debug_rpt.close();

  return power_leakage;
}

// clang-format on
