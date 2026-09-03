/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2021-2022 The Open-Source FPGA Foundation

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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <QString>

#include "Compiler/Compiler.h"
#include "Compiler/BlifParser.h"
#include "Compiler/CommandWrapper.h"
#include "Compiler/TaskCompilationStateManager.h"
#include "QLDeviceManager.h"
#include "QLMetricsManager.h"

#ifndef COMPILER_OPENFPGA_QL_H
#define COMPILER_OPENFPGA_QL_H

struct VprStageCfg {
  bool use_place_file = true;
  bool use_route_file = true;
};

namespace FOEDAG {
#if UPSTREAM_UNUSED
//enum class SynthesisType { Yosys, QL, RS };
#endif // #if UPSTREAM_UNUSED

class CompilerOpenFPGA_ql : public Compiler {
    friend class QLSettingsManager;
    friend class QLMetricsManager;
    friend class QLDeviceManager;
 public:
  CompilerOpenFPGA_ql();
#if UPSTREAM_UNUSED
  ~CompilerOpenFPGA_ql() = default;
#endif // #if UPSTREAM_UNUSED
  ~CompilerOpenFPGA_ql();

  void AnalyzeExecPath(const std::filesystem::path& path) {
    m_analyzeExecutablePath = path;
  }
  void YosysExecPath(const std::filesystem::path& path) {
    m_yosysExecutablePath = path;
  }
  void OpenFpgaExecPath(const std::filesystem::path& path) {
    m_openFpgaExecutablePath = path;
  }
  void VprExecPath(const std::filesystem::path& path) {
    m_vprExecutablePath = path;
  }
  void StaExecPath(const std::filesystem::path& path) {
    m_staExecutablePath = path;
  }
  void PinConvExecPath(const std::filesystem::path& path) {
    m_pinConvExecutablePath = path;
  }
  void ArchitectureFile(const std::filesystem::path& path) {
    m_architectureFile = path;
  }

  std::filesystem::path getPostSynthNetFilePath() const;
  std::filesystem::path getPostSynthBlifFilePath() const;

  void setCustomYosysScript(const std::string& script) { m_customYosysScript = script; }
  void OpenFPGAScript(const std::string& script) { m_openFPGAScript = script; }
  void OpenFpgaArchitectureFile(const std::filesystem::path& path) {
    m_OpenFpgaArchitectureFile = path;
  }
  void OpenFpgaSimSettingFile(const std::filesystem::path& path) {
    m_OpenFpgaSimSettingFile = path;
  }
  void OpenFpgaBitstreamSettingFile(const std::filesystem::path& path) {
    m_OpenFpgaBitstreamSettingFile = path;
  }
  void OpenFpgaRepackConstraintsFile(const std::filesystem::path& path) {
    m_OpenFpgaRepackConstraintsFile = path;
  }
  void OpenFpgaFabricKeyFile(const std::filesystem::path& path) {
    m_OpenFpgaFabricKeyFile = path;
  }
  void OpenFpgaPinmapXMLFile(const std::filesystem::path& path) {
    m_OpenFpgaPinMapXml = path;
  }
  void PbPinFixup(const std::string& name) { m_pb_pin_fixup = name; }
  void DeviceSize(const std::string& XxY) { m_deviceSize = XxY; }
  void Help(std::ostream* out);
  void Version(std::ostream* out);
  void KeepAllSignals(bool on) { m_keepAllSignals = on; }
  const std::string& YosysPluginLibName() { return m_yosysPluginLib; }
  const std::string& YosysPluginName() { return m_yosysPlugin; }
  const std::string& YosysMapTechnology() { return m_mapToTechnology; }

  void YosysPluginLibName(const std::string& libname) {
    m_yosysPluginLib = libname;
  }
  void YosysPluginName(const std::string& name) { m_yosysPlugin = name; }
  void YosysMapTechnology(const std::string& tech) { m_mapToTechnology = tech; }

  const std::string& PerDeviceSynthOptions() { return m_perDeviceSynthOptions; }
  void PerDeviceSynthOptions(const std::string& options) {
    m_perDeviceSynthOptions = options;
  }

#if UPSTREAM_UNUSED
  void SynthType(SynthesisType type) { m_synthType = type; }
#endif // #if UPSTREAM_UNUSED
  
  const std::string& PerDevicePnROptions() { return m_perDevicePnROptions; }
  void PerDevicePnROptions(const std::string& options) {
    m_perDevicePnROptions = options;
  }

  std::pair<std::filesystem::path, std::string> findCurrentDevicePinTableCsv() const;

  std::filesystem::path GenerateTempFilePath(bool managedOutside = false);
  int CleanTempFiles();
  void CleanScripts();

  /// Decrypt a device `.en` file to disk using the `<family>_Supp.db` key
  /// database located in `deviceTypeDir`. On success writes `dst_plain` and
  /// returns true. On failure logs via ErrorMessage() and returns false.
  bool decryptDeviceFile(const std::filesystem::path& src_en,
                         const std::filesystem::path& dst_plain,
                         const std::filesystem::path& deviceTypeDir,
                         const std::string& deviceTypeString);

  /// Decrypt a device `.en` file into `out_plaintext` instead of to disk, using
  /// the same key database as decryptDeviceFile(). For data this process parses
  /// itself: staging a ~12MB architecture in /tmp to read it back costs the
  /// write, leaves the plaintext world-readable, and strands the file if the run
  /// is killed. Files handed to vpr/openfpga still need decryptDeviceFile(),
  /// because those are separate processes that take a path.
  /// On failure logs via ErrorMessage() and returns false; `out_plaintext` is
  /// then unspecified.
  bool decryptDeviceFileToString(const std::filesystem::path& src_en,
                                 const std::filesystem::path& deviceTypeDir,
                                 const std::string& deviceTypeString,
                                 std::string& out_plaintext);

  /// Encrypt each file in `paths` in place (produces `<path>.en`) using a
  /// key DB at `<deviceTypeDir>/<deviceTypeString>_Supp.db`. If the key DB
  /// does not exist, one is generated and saved to that path.
  bool encryptDeviceFiles(const std::vector<std::filesystem::path>& paths,
                          const std::filesystem::path& deviceTypeDir,
                          const std::string& deviceTypeString);

  std::string ToUpper(std::string str);
  std::string ToLower(std::string str);
  
  std::filesystem::path configurePowerCalculatorInput(QLDeviceTarget);
#ifdef LEGACY_POWER_CALCULATOR
  long double PowerEstimator_Dynamic();
  long double PowerEstimator_Leakage();
#endif // LEGACY_POWER_CALCULATOR

  virtual std::tuple<std::string, std::string> BaseVprCommandLEGACY(QLDeviceTarget device_target = QLDeviceTarget());
  CommandWrapperPtr BaseVprCommand(QLDeviceTarget device_target = QLDeviceTarget(), const VprStageCfg& cfg = VprStageCfg());

  // CRR rr_graph origin offsets for this device, one "--flag value" per entry,
  // empty when none are needed. An offset already present in 'existing_options'
  // (the vpr options so far) is kept, with a warning if it disagrees.
  std::vector<std::string> rrGraphOffsetOptions(QLDeviceTarget device_target,
                                                const std::string& existing_options);

  std::string staProfile(const QLDeviceTarget& device) const;  
  bool collectStaDevices(std::map<std::string, QLDeviceTarget>& devices) const;
  QLDeviceTarget getDeviceByStaProfile(const std::string staProfile) const;
  std::string uniqueStaVprOptions() const;
  
  void onQdcFileSaved();
  void onPcfFileSaved();

  /// Register an annotated relative-placement IP netlist (via the
  /// `ip_add_to_design` Tcl command, either as an explicit .eblif/.blif path
  /// or auto-discovered from a catalog IP instance's rel_macro/ dir).
  /// Duplicates are ignored. Registrations are per project — see
  /// PruneRelIpBlifs(). See docs/development/relative_macro_placement/ in
  /// aurora2.
  ///
  /// `stub` is the IP's generated blackbox stub source (catalog IPs only;
  /// empty for the direct-netlist form, which carries no stub). It is only
  /// consumed by the Synplify synthesis flow, whose yosys pass reads the
  /// Synplify netlist instead of the design sources and therefore must read
  /// the stub explicitly. m_relIpStubs stays index-aligned with
  /// m_relIpBlifs.
  void AddRelIpBlif(const std::filesystem::path& path,
                    const std::filesystem::path& stub = {}) {
    PruneRelIpBlifs();
    if (std::find(m_relIpBlifs.begin(), m_relIpBlifs.end(), path) ==
        m_relIpBlifs.end()) {
      m_relIpBlifs.push_back(path);
      m_relIpStubs.push_back(stub);
    }
  }
  const std::vector<std::filesystem::path>& RelIpBlifs() const {
    return m_relIpBlifs;
  }
  const std::vector<std::filesystem::path>& RelIpStubs() const {
    return m_relIpStubs;
  }
  /// Registered netlists belong to the project they were registered under;
  /// clear them when the session's project changed since. Called on
  /// registration and by every consumer of m_relIpBlifs, so netlists cannot
  /// leak into another project's synthesis through ANY project-switch path —
  /// GUI open_project in particular never passes through CreateDesign.
  void PruneRelIpBlifs();

 protected:
  virtual bool IPGenerate();
  virtual bool Analyze();
  virtual bool Synthesize();
  virtual bool Packing();
  virtual bool GlobalPlacement();
  virtual bool Placement();
  virtual bool ConvertSdcPinConstrainToPcf(std::vector<std::string>&);
  virtual bool Route();
  virtual bool TimingAnalysis();
  bool TimingAnalysisHelper(const QLDeviceTarget&, const std::string&);
  virtual bool PowerAnalysis();
  virtual bool GenerateBitstream();
  // Secure bitstream post-process (checksum -> ASCON -> BCH). Resolve which
  // stages to run from the settings JSON toggles and the `bitstream encode`
  // Tcl verb; run the vendored scripts/bitstream/aurora_bitstream_encode.py.
  // A post-process failure is logged but does not fail GenerateBitstream
  // (REQUIREMENTS.md §4.5). Returns false only on a hard invocation error.
  uint32_t ResolveBitstreamEncodeStages() const;
  [[nodiscard]] bool RunBitstreamEncode(uint32_t stages);
  bool GeneratePinConstraints(std::string& filepath_fpga_fix_pins_place_str);
  bool GenerateIOFloorPlanConstraints(bool forceOverwrite = false);
  /// Derive relative-macro constraints from the annotated post-synthesis
  /// netlist and merge them with the IO floorplan constraints into
  /// <project>_rpm_constraints.xml. Only called when annotated IP netlists
  /// are registered; the default flow's <project>_constraints.xml is never
  /// touched.
  bool GenerateRelMacroConstraints(const std::string& netlistFile);
  /// RPM-authoring project record, populated by the QL-extended
  /// `create_design <name> -rpm_ip -stub <f> ?-version v1_0?
  /// ?-catalog <dir>?` (a flag orthogonal to -type, which keeps meaning
  /// source kind + synthesis tool) and reset whenever a design is created. While
  /// active: the place stage emits the authoring inputs (--echo_file on,
  /// --write_flat_place) and re-packages the IP on every success path of
  /// Placement() except `place clean` (PackageRpmAuthorProject). The IP
  /// name IS the project name; REL_MACRO_TYPE derives from it. Session-only
  /// state, stamped with its project: a project switch expires it
  /// (RpmAuthorProjectActive), and re-running create_design re-establishes
  /// it — batch scripts always do. See docs/development/relative_macro_placement/
  /// in aurora2 (AUTHORING_PROJECT_TYPE_PLAN.md).
  struct RpmAuthorProject {
    bool active = false;
    std::string name;               // == project name
    std::filesystem::path stub;     // absolute, validated at create
    std::string version;            // v<major>_<minor>
    std::filesystem::path catalog;  // absolute target catalog root
    std::filesystem::path project;  // the project this record belongs to
  };
  const RpmAuthorProject& RpmAuthor() const { return m_rpmAuthorProject; }
  void RpmAuthor(const RpmAuthorProject& record) {
    m_rpmAuthorProject = record;
  }
  /// True when the authoring record belongs to the CURRENTLY open project.
  /// The record is stamped with its project and expired lazily here — a
  /// session that switches projects without create_design (GUI
  /// open_project) must not author the old IP from the new project's
  /// artifacts. Same leak class and same lazy-expiry pattern as
  /// PruneRelIpBlifs().
  bool RpmAuthorProjectActive();
  /// Annotate + package the authoring project's IP from the current
  /// placement artifacts by driving
  /// scripts/rel_macro_placement/package_rpm_ip.py, then (re)load the
  /// target catalog. Called from Placement()'s success paths; a failure
  /// fails the stage ("placement succeeded; RPM packaging failed").
  bool PackageRpmAuthorProject();
  /// Clears per-design RPM state: netlists registered by an earlier design
  /// of the session (ip_add_to_design) must not leak -rel_ip_blif options
  /// into the next design's synthesis, and the authoring record is per
  /// design.
  bool CreateDesign(const std::string& name,
                    const std::string& type = std::string{}) override;
  virtual bool LoadDeviceData(const std::string& deviceName);
  virtual bool LicenseDevice(const std::string& deviceName);
  virtual bool DesignChanged(const std::string& synth_script,
                             const std::filesystem::path& synth_scrypt_path,
                             const std::filesystem::path& outputFile);
  virtual std::vector<std::string> GetCleanFiles(
      Action action, const std::string& projectName,
      const std::string& topModule) const;
  std::string GetYosysScriptTemplate() const;
  void FinishSynthesisScript(const ScriptRendererPtr& script);
  virtual std::string InitAnalyzeScript();
  virtual std::string FinishAnalyzeScript(const std::string& script);
  virtual std::string InitOpenFPGAScript();
  virtual std::string FinishOpenFPGAScript(const std::string& script);
  std::string GetSynplifyScriptTemplate() const;
  virtual std::filesystem::path FindSynthSDCPaths();
  virtual bool RegisterCommands(TclInterpreter* interp, bool batchMode);
  virtual std::pair<bool, std::string> IsDeviceSizeCorrect(
      const std::string& size) const;
  bool VerifyTargetDevice() const;
  static std::filesystem::path removeLog(FOEDAG::ProjectManager* projManager,
                                       const std::string& fileName);
  static std::filesystem::path copyLog(FOEDAG::ProjectManager* projManager,
                                       const std::string& srcFileName,
                                       const std::string& destFileName);
  std::filesystem::path m_yosysExecutablePath = "yosys";
  std::filesystem::path m_analyzeExecutablePath = "analyze";
#if UPSTREAM_UNUSED
  SynthesisType m_synthType = SynthesisType::Yosys;
#endif // #if UPSTREAM_UNUSED
  std::string m_yosysPluginLib;
  std::string m_yosysPlugin;
  std::string m_mapToTechnology;
  std::string m_perDeviceSynthOptions;
  std::string m_perDevicePnROptions;
  std::string m_synthesisType;  // QL, Yosys, ...
  std::filesystem::path m_openFpgaExecutablePath = "openfpga";
  std::filesystem::path m_vprExecutablePath = "vpr";
  std::filesystem::path m_staExecutablePath = "sta";
  std::filesystem::path m_pinConvExecutablePath = "pin_c";
  std::filesystem::path m_aurora_template_script_yosys_path;
  std::filesystem::path m_aurora_template_script_synplify_path;
  std::filesystem::path m_aurora_template_script_openfpga_path;
  // Annotated relative-placement IP netlists (registered via
  // ip_add_to_design). Empty in the default flow; every consumer is gated on
  // this so that projects without relative-placement IPs behave identically
  // to before. Valid only for the project recorded in m_relIpBlifsProject
  // (see PruneRelIpBlifs).
  std::vector<std::filesystem::path> m_relIpBlifs;
  // Index-aligned with m_relIpBlifs: the IP's blackbox stub source, empty
  // for the direct-netlist registration form. See AddRelIpBlif().
  std::vector<std::filesystem::path> m_relIpStubs;
  std::filesystem::path m_relIpBlifsProject;
  // See RpmAuthor().
  RpmAuthorProject m_rpmAuthorProject;
  /*!
   * \brief m_architectureFile
   * We required from user explicitly specify architecture file.
   */
  std::filesystem::path m_architectureFile = "";

  /*!
   * \brief m_OpenFpgaArchitectureFile
   * We required from user explicitly specify openfpga architecture file.
   */
  std::filesystem::path m_OpenFpgaArchitectureFile = "";
  std::filesystem::path m_OpenFpgaSimSettingFile = "";
  std::filesystem::path m_OpenFpgaBitstreamSettingFile = "";
  std::filesystem::path m_OpenFpgaRepackConstraintsFile = "";
  std::filesystem::path m_OpenFpgaFabricKeyFile = "";
  std::filesystem::path m_OpenFpgaPinMapXml = "";
  std::filesystem::path m_OpenFpgaBitstreamRemappingFile = "";
  std::filesystem::path m_SBMapsFile = "";
  std::filesystem::path m_SBTemplatesDir = "";
  std::string m_deviceSize;
  std::string m_customYosysScript;
  std::string m_openFPGAScript;
  std::string m_pb_pin_fixup;

#ifdef ENABLE_INCREMENTAL_COMPILATION_FOR_STA
  CommandWrapperPtr BaseStaCommand();
#else // ENABLE_INCREMENTAL_COMPILATION_FOR_STA
  std::string BaseStaCommand();
#endif // ENABLE_INCREMENTAL_COMPILATION_FOR_STA
  virtual std::string BaseStaScript(std::string libFileName,
                                    std::string netlistFileName,
                                    std::string sdfFileName,
                                    std::string sdcFileName);
  bool m_keepAllSignals = false;

private:
  std::vector<std::filesystem::path> m_TempFileList;
  std::filesystem::path m_cryptdbPath;

  BlifParser m_blifParser;
  TaskCompilationStateManager m_taskCompilationStateManager;

  std::unordered_map<int, CommandWrapperPtr> getSynthesisCommands();
  CommandWrapperPtr getPackingCommand();
  CommandWrapperPtr getPlacementCommand();
  CommandWrapperPtr getRoutingCommand();      
#ifdef ENABLE_INCREMENTAL_COMPILATION_FOR_STA       
  CommandWrapperPtr getTimingAnalysisCommand(const QLDeviceTarget& current_device_sta, const std::string& profile);
#endif

  void invalidateTaskStatuses() override final;
  bool isSynthesisStatusActual();
  bool isPackingStatusActual();
  bool isPlacementStatusActual();
  bool isRoutingStatusActual();
#ifdef ENABLE_INCREMENTAL_COMPILATION_FOR_STA
  bool isTimingAnalysysStatusActual();
#endif

  void clearCompilationCache() override final;
  bool hasCompilationCache() const override final;

  const std::vector<std::string>& vprRedirectedWarnings() const;

  struct Offset {
    Offset() {}
    Offset(int col, int row): col(col), row(row) {}
    int col = 0;
    int row = 0;
  };
};

class VprArchitectureFileProfider {
public:
  VprArchitectureFileProfider(CompilerOpenFPGA_ql* compiler): m_compiler(compiler) {}
  ~VprArchitectureFileProfider();

  const std::filesystem::path& get();
  void clean();

private:
  CompilerOpenFPGA_ql* m_compiler = nullptr;
  std::filesystem::path m_architectureFile;
  bool m_isFileTemporary = false;

  const std::filesystem::path& error(const std::string& msg);
};

}  // namespace FOEDAG

#endif

// clang-format on
