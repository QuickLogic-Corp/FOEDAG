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

#include "IPGenerate/IPGenerator.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <QDebug>
#include <QProcess>
#include <QCoreApplication>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>

#include "Compiler/Compiler.h"
#include "Compiler/Log.h"
#include "Compiler/WorkerThread.h"
#include "Compiler/QLSettingsManager.h"
#include "Compiler/QLDeviceManager.h"
#include "IPGenerate/IPCatalog.h"
#include "MainWindow/Session.h"
#include "NewProject/ProjectManager/project_manager.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

extern FOEDAG::Session* GlobalSession;
using namespace FOEDAG;
using Time = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;

// Right now we bypass new approach and still use legacy flow of building commandline arguments.
#define EXCLUDE_MODIFICATION_JSON_FLOW

namespace {
// "2_0" -> "DSPV2", "1_1" -> "DSPV1.1". Mirrors the DSP_TYPE spelling in the
// device config.json so a message names the fabric the way the device data
// does, rather than echoing the internal "<major>_<minor>" form.
std::string dspFabricName(const std::string& version) {
  const size_t sep = version.find('_');
  if (sep == std::string::npos) return "DSPV" + version;
  const std::string major = version.substr(0, sep);
  const std::string minor = version.substr(sep + 1);
  return "DSPV" + major + ((minor.empty() || minor == "0") ? "" : "." + minor);
}

// Name of the same IP built for a different DSP version, derived purely by
// rewriting the "_v<required>" suffix - no IP name is hard-coded anywhere.
// The version marker is matched case-insensitively and its case preserved,
// because catalog directories use both "v1_0" and "V1_0". Returns "" when the
// name does not carry the required version as a suffix.
std::string siblingForDspVersion(const std::string& ipName,
                                 const std::string& requiredVersion,
                                 const std::string& deviceVersion) {
  // The suffix is a two-character marker followed by the version:
  //
  //     dsp_generator_v2_0
  //                  |||
  //     underscoreAt -+||
  //         markerAt --+|   'v' or 'V'
  //        versionAt ---+
  //
  constexpr size_t kVersionMarkerLength = 2;  // the '_' and the 'v'
  const size_t suffixLength = kVersionMarkerLength + requiredVersion.size();
  if (ipName.size() <= suffixLength) return {};

  const size_t underscoreAt = ipName.size() - suffixLength;
  const size_t markerAt = underscoreAt + 1;
  const size_t versionAt = underscoreAt + kVersionMarkerLength;

  if (ipName[underscoreAt] != '_') return {};
  const char marker = ipName[markerAt];
  if (marker != 'v' && marker != 'V') return {};
  if (ipName.compare(versionAt, requiredVersion.size(), requiredVersion) != 0)
    return {};

  // substr() first, so the '_' and the marker are appended to a std::string
  // rather than doing pointer arithmetic on a string literal.
  return ipName.substr(0, underscoreAt) + '_' + marker + deviceVersion;
}

}  // namespace

// Prepends `line` as a comment to the generated wrapper source, so a preview
// IP carries its status into the netlist and not just into the session log.
//
// The generators write "<module>_<version>.v" (lib/common.py builds
// build_name + "_" + version), so `baseName` must already carry the version.
// Getting that wrong used to make this a silent no-op, hence the diagnostic
// when nothing matches. The rewrite goes through a temporary file so an
// interrupted stamp cannot destroy a generated wrapper.
bool IPGenerator::StampWrapperComment(Compiler* compiler,
                                      const std::filesystem::path& srcDir,
                                      const std::string& baseName,
                                      const std::string& line) {
  bool found = false;
  bool stamped = false;
  for (const char* extension : {".v", ".sv"}) {
    const std::filesystem::path wrapper = srcDir / (baseName + extension);
    if (!FileUtils::FileExists(wrapper)) continue;
    found = true;

    std::string body;
    {
      std::ifstream in(wrapper.string());
      if (!in) continue;
      std::stringstream buffer;
      buffer << in.rdbuf();
      body = buffer.str();
    }
    if (body.rfind(line, 0) == 0) {  // already stamped
      stamped = true;
      continue;
    }

    // Keep the generated file's mode: the temporary is created with
    // 0666 & ~umask, so renaming it over a read-only wrapper would widen it.
    std::error_code error;
    const auto mode = std::filesystem::status(wrapper, error).permissions();
    const bool haveMode = !error;

    const std::filesystem::path temporary = wrapper.string() + ".stamp.tmp";
    auto discardTemporary = [&temporary]() {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    };
    {
      std::ofstream out(temporary.string(), std::ios::trunc);
      if (!out) continue;
      out << line << "\n" << body;
      // close() explicitly and then check: a deferred write error can surface
      // only at close, and letting the destructor swallow it would rename a
      // truncated file over the generated wrapper.
      out.close();
      if (!out) {
        discardTemporary();
        continue;
      }
    }
    if (haveMode) {
      std::filesystem::permissions(temporary, mode,
                                   std::filesystem::perm_options::replace,
                                   error);
      if (error) {
        discardTemporary();
        continue;
      }
    }
    std::filesystem::rename(temporary, wrapper, error);
    if (error) {
      discardTemporary();
      continue;
    }
    stamped = true;
  }

  if (!stamped && compiler) {
    // Distinguish the two failures: during an incident, "no source found" for
    // a file that exists but could not be rewritten sends the reader the wrong
    // way entirely.
    compiler->WarningMessage(
        "IP Generate, could not record the preview notice in the generated "
        "wrapper: " +
        std::string(found ? "could not rewrite " : "no source found at ") +
        (srcDir / (baseName + ".v")).string());
  }
  return stamped;
}

std::string IPGenerator::PreviewNotice(const IPDefinition* def) {
  std::string notice = "IP " + def->Name() +
                       " is a preview IP and is not production-qualified";
  const std::string& note = def->Availability().maturityNote;
  if (!note.empty()) notice += ": " + note;
  notice += ".";
  return notice;
}

std::string IPGenerator::UnverifiedRequirementNotice(const IPDefinition* def) {
  const IPAvailability& availability = def->Availability();
  // Two distinct situations: nothing usable was read, or a requirement was
  // read and enforced but the rest of the block was not.
  const std::string lead =
      availability.requiredDspVersion.empty()
          ? "the fabric requirement in ip_manifest.json could not be read, so "
            "it has not been checked against this device."
          : "part of the \"requires\" block in ip_manifest.json could not be "
            "read, so this IP may have further requirements that were not "
            "checked.";
  return "IP " + def->Name() + ": " + lead + " " +
         availability.manifestWarning;
}

IPGenerator::DeviceFacts IPGenerator::CurrentDeviceFacts() {
  DeviceFacts facts;
  auto* devices = QLDeviceManager::getInstance();
  const auto target = devices->getCurrentDeviceTarget();
  facts.valid = devices->isDeviceTargetValid(target);
  if (!facts.valid) return facts;
  facts.name = target.device_variant.devicename;
  // deviceDSPVersion() defaults to "1_0" for devices that declare no DSP_TYPE,
  // which is most of them. Only ask for it once a device is actually selected,
  // so that default cannot stand in for a device that is not there.
  facts.dspVersion = devices->deviceDSPVersion(target);
  return facts;
}

IPGenerator::IPStatus IPGenerator::EvaluateAvailability(
    const IPDefinition* def, IPCatalog* catalog, const DeviceFacts& device) {
  IPStatus status;
  const IPAvailability& availability = def->Availability();
  status.preview = availability.maturity == IPAvailability::Maturity::Preview;
  status.state = status.preview ? "preview" : "production";

  // Invariant 3: an unreadable manifest does not get to mean "ungated". Note
  // that this is NOT tested ahead of the gate - a "requires" block can be
  // partly readable (a valid dsp_version next to a key from a newer schema),
  // and a requirement we did read must still be enforced. Dropping it there
  // would put a DSPV2 IP on DSPV1 fabric the moment ipgenerator adds a second
  // requires key against an older FOEDAG.
  status.unverified = availability.requirementUnverifiable;

  std::vector<std::string> clauses;
  if (status.unverified && availability.requiredDspVersion.empty()) {
    // Nothing usable was read. The IP stays listed and buildable - we cannot
    // prove it would fail - but the fact that the gate never ran is stated at
    // every surface.
    clauses.push_back(
        std::string(
            "fabric requirement could not be read from ip_manifest.json and "
            "has not been checked") +
        (device.valid ? " against device " + device.name
                      : " (no device is selected)") +
        ".");
  } else if (availability.requiredDspVersion.empty()) {
    // Distinguish "the file says production" from "there was no file". The
    // second is the signature of a stale or partial catalog install, and is
    // the only way an operator can see it.
    clauses.push_back(
        availability.manifestPresent
            ? "available on all devices."
            : "no ip_manifest.json; defaulted to production with no fabric "
              "requirement.");
  } else if (!device.valid) {
    // With no device selected there is nothing to gate against. Say so and
    // keep the IP listed, rather than judging it against a
    // default-constructed target that would silently look like a DSPV1 part.
    clauses.push_back("requires " +
                      dspFabricName(availability.requiredDspVersion) +
                      " fabric; no device is selected, so availability is not "
                      "evaluated.");
  } else if (device.dspVersion == availability.requiredDspVersion) {
    clauses.push_back("requires " +
                      dspFabricName(availability.requiredDspVersion) +
                      " fabric; device " + device.name + " provides " +
                      dspFabricName(device.dspVersion) + ".");
  } else {
    status.available = false;
    status.listed = false;
    status.state = "unavailable";
    std::string clause = "requires " +
                         dspFabricName(availability.requiredDspVersion) +
                         " fabric; device " + device.name + " provides " +
                         dspFabricName(device.dspVersion) + ".";
    const std::string alternative = siblingForDspVersion(
        def->Name(), availability.requiredDspVersion, device.dspVersion);
    if (!alternative.empty() && catalog != nullptr &&
        catalog->Definition(alternative) != nullptr) {
      clause += " Use " + alternative + " on this device.";
    } else {
      clause += " No version of this IP targets this device.";
    }
    clauses.push_back(clause);
  }

  // A requirement was read and enforced above, but the rest of the block was
  // not: say so, because the IP may carry requirements this build never saw.
  if (status.unverified && !availability.requiredDspVersion.empty())
    clauses.push_back(
        "Part of the \"requires\" block could not be read, so this IP may have "
        "further requirements that were not checked.");

  if (status.preview) {
    std::string clause = "Preview IP, not production-qualified";
    if (!availability.maturityNote.empty())
      clause += ": " + availability.maturityNote;
    clause += ".";
    clauses.push_back(clause);
  }
  if (!availability.manifestWarning.empty())
    clauses.push_back(availability.manifestWarning);

  status.reason = StringUtils::join(clauses, " ");
  return status;
}

IPGenerator::IPStatus IPGenerator::EvaluateAvailability(const IPDefinition* def,
                                                        IPCatalog* catalog) {
  return EvaluateAvailability(def, catalog, CurrentDeviceFacts());
}

std::filesystem::path IPGenerator::EnvsPath() const {
  return std::filesystem::weakly_canonical(m_installDir / "envs");
}

std::filesystem::path IPGenerator::IPCatalogPath() const {
  return std::filesystem::weakly_canonical(m_installDir / "IP_Catalog");
}

std::filesystem::path IPGenerator::DefaultIPCatalogPath() const {
  // DataPath()-relative (rather than the m_installDir-based
  // IPCatalogPath() above): the two agree in the aurora layout (DataPath =
  // <install>/device_data), but DataPath falls back to <install>/share/<exe>
  // when device_data is absent, and this path must match what the rest of
  // the tool resolves.
#ifdef UPSTREAM_IP_GENERATOR
  std::filesystem::path path =
      GlobalSession->Context()->DataPath() / "IP_Catalog";
#else
  std::filesystem::path path =
      GlobalSession->Context()->DataPath() / ".." / "IP_Catalog";
#endif
  return path.lexically_normal();
}

std::filesystem::path IPGenerator::ProjectUserCatalogPath() const {
  if (m_compiler == nullptr || m_compiler->ProjManager() == nullptr ||
      !m_compiler->ProjManager()->HasDesign()) {
    return {};
  }
  return std::filesystem::path(m_compiler->ProjManager()->projectPath()) /
         "IP_Catalog";
}

void IPGenerator::LoadDefaultCatalogs() {
  const std::filesystem::path defaultRoot = DefaultIPCatalogPath();
  const std::filesystem::path userRoot = ProjectUserCatalogPath();
  for (const std::filesystem::path& root : {defaultRoot, userRoot}) {
    if (root.empty()) continue;
    const std::string key = std::filesystem::weakly_canonical(root).string();
    if (m_compiler->LoadedIpCatalogRoots().count(key)) continue;
    if (!FileUtils::FileExists(root)) {
      // The project-local catalog is optional and usually absent; the
      // installed one missing is worth a warning (but not a hard failure —
      // an explicitly added catalog may be all the session needs).
      if (root == defaultRoot) {
        m_compiler->Message("WARNING: installed IP catalog not found: " +
                            root.string());
      }
      continue;
    }
    // Through the Tcl command (not BuildLiteXIPCatalog directly) so the
    // load runs with the same threading as an explicit user call.
    m_compiler->TclInterp()->evalCmd("add_litex_ip_catalog {" + root.string() +
                                     "}");
  }
}

void IPGenerator::setIpOutputLocation(const std::string& moduleName, const std::string& version, const std::filesystem::path& ipOutputLocation)
{
  m_ipOutputLocations[moduleName + "_" + version] = ipOutputLocation;
}

IPGenerator::IPGenerator(const std::filesystem::path& installDir, IPCatalog* catalog, Compiler* compiler): m_catalog(catalog), m_compiler(compiler), m_installDir(installDir) {
  // Only set PYTHONHOME and LD_LIBRARY_PATH when the bundled envs directory
  // actually exists.  When it doesn't (e.g. macOS builds without the litex
  // environment), setting PYTHONHOME to a non-existent path poisons the
  // fallback to system python3, causing "Failed to import encodings module".
  auto pythonHome = EnvsPath() / "python3.8";
  if (std::filesystem::is_directory(pythonHome)) {
    m_environment["PYTHONHOME"] = pythonHome.string();
#ifndef __WIN32
    // IP Generator requires libffi.so.6 which is absent on ubuntu>=20.04
    std::string ldLibraryPath = qgetenv("LD_LIBRARY_PATH").toStdString();
    std::string newLdLibraryPath = (pythonHome / "lib" / "os_libs").string();
    m_environment["LD_LIBRARY_PATH"] = newLdLibraryPath + ":" + ldLibraryPath;
#endif
  }
}

void IPGenerator::shareContext()
{
  std::filesystem::path ipBuildPath = GetProjectIPsPath() / "quicklogic" / "ip";
  m_environment["QL_IPS_BUILD_PATH"] = ipBuildPath.string();
  dumpDeviceInfo(ipBuildPath);
#ifndef EXCLUDE_MODIFICATION_JSON_FLOW
  dumpParameterModifications(ipBuildPath);
#endif // EXCLUDE_MODIFICATION_JSON_FLOW
}

void IPGenerator::dumpDeviceInfo(const std::filesystem::path& path)
{
  QLSettingsManager::getInstance(); // is required in order to proper QLSettingsManager and QLDeviceManager initilization

  auto targetDevice = QLDeviceManager::getInstance()->getCurrentDeviceTarget();
  if( QLDeviceManager::getInstance()->isDeviceTargetValid(targetDevice) ) {
    std::string family              = targetDevice.device_variant.family;
    std::string foundry             = targetDevice.device_variant.foundry;
    std::string node                = targetDevice.device_variant.node;
    std::string device              = targetDevice.device_variant.devicename;
    std::string voltage_threshold   = targetDevice.device_variant.voltage_threshold;
    std::string p_v_t_corner        = targetDevice.device_variant.p_v_t_corner;

    std::string layout = targetDevice.device_variant_layout.name;
    int width = targetDevice.device_variant_layout.width;
    int height = targetDevice.device_variant_layout.height;
    std::optional<int> bram = targetDevice.device_variant_layout.bram;
    std::optional<int> dsp = targetDevice.device_variant_layout.dsp;
    std::optional<int> clb = targetDevice.device_variant_layout.clb;
    std::optional<int> io = targetDevice.device_variant_layout.io;

    nlohmann::ordered_json json;

    // null rather than a string count for a resource that is not yet known -
    // e.g. an AUTO/RESOURCES device before Packing() has resolved its real
    // geometry - so a consumer can tell "unresolved" apart from a real count.
    const auto resourceJson = [](const std::optional<int>& count) -> nlohmann::ordered_json {
      return count.has_value() ? nlohmann::ordered_json(std::to_string(*count))
                                : nlohmann::ordered_json(nullptr);
    };

    json["family"] = StringUtils::toLower(family);
    json["foundry"] = StringUtils::toLower(foundry);
    json["node"] = StringUtils::toLower(node);
    json["device"] = StringUtils::toLower(device);
    json["layout"] = StringUtils::toLower(layout);
    json["width"] = std::to_string(width);
    json["height"] = std::to_string(height);
    json["bram"] = resourceJson(bram);
    json["dsp"] = resourceJson(dsp);
    json["clb"] = resourceJson(clb);
    json["io"] = resourceJson(io);

    if (!std::filesystem::exists(path)) {
      FileUtils::MkDirs(path);
    }
    saveJsonFile(json, path / "device_info.json");
  } else {
    m_compiler->WarningMessage("Cannot dump device info because target device is invalid");
  }
}

void IPGenerator::dumpParameterModifications(const std::filesystem::path& path)
{
  QLSettingsManager::getInstance(); // is required in order to proper QLSettingsManager and QLDeviceManager initilization

  auto targetDevice = QLDeviceManager::getInstance()->getCurrentDeviceTarget();
  if( QLDeviceManager::getInstance()->isDeviceTargetValid(targetDevice) ) {
    std::string foundry             = targetDevice.device_variant.foundry;
    std::string node                = targetDevice.device_variant.node;

    // TODO: this is temprorary solution, we need find the way how to fetch data based on HW info
    if (StringUtils::toLower(foundry) == "gf" && StringUtils::toLower(node) == "22nm") {
      nlohmann::json paramsModData = {
          {"parameters", {
              {
                  {"parameter", "memory_type"},
                  {"options", {"Single_Port", "Simple_Dual_Port"}}
              }
          }}
      };
      saveJsonFile(paramsModData, path / "on_chip_memory" / "params_mod.json");
    }
  }
}

void IPGenerator::saveJsonFile(const nlohmann::json& json, const std::filesystem::path& filepath)
{
  std::filesystem::path path = filepath.parent_path();
  if (!std::filesystem::exists(path)) {
    FileUtils::MkDirs(path.parent_path());
  }

  std::ofstream json_ofstream(filepath);
  json_ofstream << std::setw(4) << json << std::endl;
}

bool IPGenerator::RegisterCommands(TclInterpreter* interp, bool batchMode) {
  auto add_litex_ip_catalog = [](void* clientData, Tcl_Interp* interp, int argc,
                                 const char* argv[]) -> int {
    IPGenerator* generator = (IPGenerator*)clientData;
    Compiler* compiler = generator->GetCompiler();
    if (argc < 2) {
      compiler->ErrorMessage(
          "Missing directory path for LiteX ip generator(s)");
    }
    const std::filesystem::path file = argv[1];
    std::filesystem::path expandedFile = file;
    bool use_orig_path = false;
    if (FileUtils::FileExists(expandedFile) && expandedFile != "./") {
      use_orig_path = true;
    }

    if ((!use_orig_path) &&
        (!compiler->GetSession()->CmdLine()->Script().empty())) {
      std::filesystem::path script =
          compiler->GetSession()->CmdLine()->Script();
      std::filesystem::path scriptPath = script.parent_path();
      std::filesystem::path fullPath = scriptPath;
      fullPath = fullPath / file;
      expandedFile = fullPath;
    }
    std::filesystem::path the_path = expandedFile;
    if (!the_path.is_absolute()) {
      const auto& path = std::filesystem::current_path();
      expandedFile = path / expandedFile;
    }
    auto fn = [compiler, expandedFile]() -> bool {
      return compiler->BuildLiteXIPCatalog(expandedFile.lexically_normal());
    };
    WorkerThread* thread = new WorkerThread{
        {}, Compiler::Action::NoAction, generator->GetCompiler()};
    bool res = thread->Start(fn);
    return res ? TCL_OK : TCL_ERROR;
  };
  interp->registerCmd("add_litex_ip_catalog", add_litex_ip_catalog, this, 0);

  auto ip_catalog = [](void* clientData, Tcl_Interp* interp, int argc,
                       const char* argv[]) -> int {
    IPGenerator* generator = (IPGenerator*)clientData;
    Compiler* compiler = generator->GetCompiler();

    // Load the installed and (if a project is open) project-local catalogs,
    // each at most once per session.
    generator->LoadDefaultCatalogs();

    // Argument scan. `ip_catalog` used to be a flat argc test; the options
    // below need a real scan, but the no-argument result must not change:
    // shipped testcases and user scripts do `foreach ip [ip_catalog]` against
    // the space-separated name list it returns.
    const std::string usage{
        "\n\nUsage:\nip_catalog ?<IP_NAME>? ?-all? ?-format text|json?"};
    bool listAll = false;
    bool jsonFormat = false;
    std::string ipName;
    for (int i = 1; i < argc; i++) {
      const std::string arg{argv[i]};
      if (arg == "-all") {
        listAll = true;
      } else if (arg == "-format") {
        if (i + 1 >= argc) {
          compiler->ErrorMessage("ip_catalog: -format requires a value" +
                                 usage);
          return TCL_ERROR;
        }
        const std::string format{argv[++i]};
        if (format == "json") {
          jsonFormat = true;
        } else if (format != "text") {
          compiler->ErrorMessage("ip_catalog: unknown -format '" + format +
                                 "', expected text or json" + usage);
          return TCL_ERROR;
        }
      } else if (arg.size() > 1 && arg[0] == '-') {
        compiler->ErrorMessage("ip_catalog: unknown option '" + arg + "'" +
                               usage);
        return TCL_ERROR;
      } else if (ipName.empty()) {
        ipName = arg;
      } else {
        compiler->ErrorMessage("ip_catalog: unexpected argument '" + arg + "'" +
                               usage);
        return TCL_ERROR;
      }
    }

    if (!ipName.empty() && (listAll || jsonFormat)) {
      compiler->ErrorMessage(
          "ip_catalog: -all and -format apply to the catalog listing, not to a "
          "named IP" +
          usage);
      return TCL_ERROR;
    }

    // One device lookup per command. EvaluateAvailability() needs the device's
    // DSP version, and reading it re-parses the device config.json - doing
    // that once per IP made a listing O(N) file parses.
    const IPGenerator::DeviceFacts device = IPGenerator::CurrentDeviceFacts();

    if (!ipName.empty()) {
      // Querying a named IP. An unknown name still yields an empty result, as
      // it always has; only an IP the current device cannot use is rejected,
      // and then with the same reason the listing would have given.
      IPDefinition* named = generator->Catalog()->Definition(ipName);
      if (named != nullptr) {
        const IPGenerator::IPStatus status =
            IPGenerator::EvaluateAvailability(named, generator->Catalog(),
                                              device);
        if (!status.available) {
          compiler->ErrorMessage(named->Name() + " " + status.reason);
          return TCL_ERROR;
        }
      }
      std::string ip_def;
      for (auto def : generator->Catalog()->Definitions()) {
        if (ipName == def->Name()) {
          for (auto param : def->Parameters()) {
            std::string defaultValue;
            switch (param->GetType()) {
              case Value::Type::ParamIpVal:
                defaultValue = param->GetSValue();
                break;
              case Value::Type::ParamInt:
                defaultValue = param->GetSValue();
                break;
              case Value::Type::ParamString:
                defaultValue = param->GetSValue();
                break;
              case Value::Type::ConstInt:
                defaultValue = param->GetSValue();
                break;
            }
            ip_def += "{" + param->Name() + " " + defaultValue + "} ";
          }
        }
      }
      compiler->TclInterp()->setResult(ip_def);
      return TCL_OK;
    }

    // Listing. -all and -format json both report every IP, including the ones
    // the device cannot take, because an entry that is hidden without a stated
    // reason is exactly what this model exists to prevent. The `listed` flag
    // records what the default listing would have shown.
    if (jsonFormat) {
      nlohmann::ordered_json records = nlohmann::ordered_json::array();
      for (auto def : generator->Catalog()->Definitions()) {
        const IPGenerator::IPStatus status =
            IPGenerator::EvaluateAvailability(def, generator->Catalog(),
                                              device);
        nlohmann::ordered_json record;
        record["name"] = def->Name();
        record["state"] = status.state;
        record["maturity"] = status.preview ? "preview" : "production";
        record["available"] = status.available;
        record["listed"] = status.listed;
        record["manifest_present"] = def->Availability().manifestPresent;
        record["requirement_verified"] = !status.unverified;
        record["reason"] = status.reason;
        records.push_back(record);
      }
      compiler->TclInterp()->setResult(records.dump());
    } else if (listAll) {
      std::vector<std::string> lines;
      for (auto def : generator->Catalog()->Definitions()) {
        const IPGenerator::IPStatus status =
            IPGenerator::EvaluateAvailability(def, generator->Catalog(),
                                              device);
        lines.push_back(def->Name() + " [" + status.state + "] " +
                        status.reason);
      }
      compiler->TclInterp()->setResult(StringUtils::join(lines, "\n"));
    } else {
      std::string ips;
      for (auto def : generator->Catalog()->Definitions()) {
        if (!IPGenerator::EvaluateAvailability(def, generator->Catalog(),
                                               device)
                 .listed)
          continue;
        ips += def->Name() + " ";
      }
      compiler->TclInterp()->setResult(ips);
    }
    return TCL_OK;
  };
  interp->registerCmd("ip_catalog", ip_catalog, this, 0);

  auto configure_ip = [](void* clientData, Tcl_Interp* interp, int argc,
                         const char* argv[]) -> int {
    bool ok = true;
    IPGenerator* generator = (IPGenerator*)clientData;
    Compiler* compiler = generator->GetCompiler();
    if (!compiler->ProjManager()->HasDesign()) {
      compiler->ErrorMessage("Create a design first: create_design <name>");
      return TCL_ERROR;
    }

    // Load the installed and project-local catalogs, each at most once per
    // session (a design exists here, so the project-local root is known).
    generator->LoadDefaultCatalogs();

    auto printWrongUsageMsgHelperFn = [compiler](const std::string& msg){
        compiler->ErrorMessage(msg +
          "\n\nUsage:\nconfigure_ip <IP_NAME> -mod_name <name> "
          "-out_location <path> -version <ver_name> -P<param>=\"<value>\"...");
    };

    std::string ip_name;
    std::string mod_name;
    std::filesystem::path out_location;
    std::string version;
    std::vector<SParameter> parameters;
    bool generated{true};
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (i == 1) {
        ip_name = arg;
      } else if (arg == "-mod_name") {
        i++;
        mod_name = argv[i];
      } else if (arg == "-out_location") {
        i++;
        out_location = argv[i];
      } else if (arg == "-version") {
        i++;
        version = argv[i];
      } else if (arg == "-template") {
        generated = false;
      } else if (arg.find("-P") == 0) {
        std::string def;
        std::string value;
        const size_t loc = arg.find('=');
        if (loc == std::string::npos) {
          def = arg.substr(2);
        } else {
          def = arg.substr(2, loc - 2);
          value = arg.substr(loc + 1);
        }
        if (!def.empty()) {
          SParameter param(def, value);
          parameters.push_back(param);
        }
      } else {
        printWrongUsageMsgHelperFn("Unsupported parameter " + std::string(argv[i]));
        return TCL_ERROR;
      }
    }
    IPDefinition* def = generator->Catalog()->Definition(ip_name);
    if (def == nullptr) {
      compiler->ErrorMessage("Unknown IP: " + ip_name);
      ok = false;
      return TCL_ERROR;
    }
    // Enforce the device gate here too: hiding an IP from the ip_catalog
    // listing only affects what is shown. A script can still call configure_ip
    // with a hard-coded name without ever listing the catalog, so generation
    // must be blocked at this point, not just in the listing.
    const IPGenerator::IPStatus status =
        IPGenerator::EvaluateAvailability(def, generator->Catalog());
    if (!status.available) {
      compiler->ErrorMessage(def->Name() + " " + status.reason);
      ok = false;
      return TCL_ERROR;
    }
    if (!out_location.empty()) {
      generator->setIpOutputLocation(mod_name, version, out_location);
    } else {
      out_location = generator->GetProjectIPsPath();
    }

    if (ip_name.empty()) {
      printWrongUsageMsgHelperFn("ip_name is not set (must be first argument)");
      return TCL_ERROR;
    }
    if (mod_name.empty()) {
      printWrongUsageMsgHelperFn("-mod_name is not set");
      return TCL_ERROR;
    }
    if (version.empty()) {
      printWrongUsageMsgHelperFn("-version is not set");
      return TCL_ERROR;
    }

    // Validate each supplied parameter against the catalog definition before
    // building the instance. The GUI enforces these constraints interactively
    // (Qt validators / comboboxes); the batch (Tcl) path had no equivalent, so
    // an out-of-range or otherwise invalid value silently reached generation.
    const auto defParams = def->Parameters();
    // Effective value of every parameter (catalog default, overridden by any
    // supplied value) — used to evaluate dependency gating below.
    std::map<std::string, std::string> paramValues;
    for (auto* p : defParams) {
      if (p->GetType() == Value::Type::ParamIpVal) {
        paramValues[p->Name()] = static_cast<IPParameter*>(p)->GetSValue();
      }
    }
    for (const auto& sp : parameters) {
      paramValues[sp.Name()] = sp.GetSValue();
    }
    for (const auto& sparam : parameters) {
      FOEDAG::Value* catVal = nullptr;
      for (auto* p : defParams) {
        if (p->Name() == sparam.Name()) {
          catVal = p;
          break;
        }
      }
      if (catVal == nullptr || catVal->GetType() != Value::Type::ParamIpVal) {
        compiler->ErrorMessage(
            "Unknown parameter '-P" + sparam.Name() + "' for IP " + ip_name +
                ".\nKnown parameters:\n" + DescribeIPParameters(defParams),
            false);
        return TCL_ERROR;
      }
      auto* ipParam = static_cast<IPParameter*>(catVal);
      // Skip inactive fields (statically disabled, or gated off by a dependency
      // bool). The GUI keeps these at their default and never validates them.
      if (!ipParam->IsActive(paramValues)) continue;
      std::string err;
      if (!ipParam->Validate(sparam.GetSValue(), err)) {
        compiler->ErrorMessage(
            "Invalid value for parameter '" + sparam.Name() + "': " + err,
            false);
        return TCL_ERROR;
      }
    }

    // Advisories come last, once the command is known to be well formed:
    // warning about a preview IP and then rejecting the call for a missing
    // -mod_name would be noise about something that never happened.
    //
    // Both go to the console and to the flow log. A preview IP additionally
    // gets the notice stamped into its generated wrapper at ipgenerate time,
    // so the provenance outlives the session that configured it.
    auto advise = [compiler](const std::string& notice) {
      compiler->WarningMessage(notice + " Continuing.");
      if (GlobalSession && GlobalSession->CmdStack())
        LOG_OUTPUT("WARNING: " + notice + " Continuing.\n");
    };
    if (status.unverified)
      advise(IPGenerator::UnverifiedRequirementNotice(def));
    if (status.preview) advise(IPGenerator::PreviewNotice(def));

    IPInstance* instance =
        new IPInstance(ip_name, version, def, parameters, mod_name, out_location);
    instance->Generated(generated);
    if (!generator->AddIPInstance(instance)) {
      ok = false;
    }
    return ok ? TCL_OK : TCL_ERROR;
  };
  interp->registerCmd("configure_ip", configure_ip, this, 0);

  auto remove_ip = [](void* clientData, Tcl_Interp* interp, int argc,
                      const char* argv[]) -> int {
    IPGenerator* generator = (IPGenerator*)clientData;
    if (argc > 1) {
      generator->RemoveIPInstance(argv[1]);
    }

    return TCL_OK;
  };
  interp->registerCmd("remove_ip", remove_ip, this, 0);

  auto delete_ip = [](void* clientData, Tcl_Interp* interp, int argc,
                      const char* argv[]) -> int {
    IPGenerator* generator = (IPGenerator*)clientData;
    if (argc > 1) {
      generator->DeleteIPInstance(argv[1]);
    }

    return TCL_OK;
  };
  interp->registerCmd("delete_ip", delete_ip, this, 0);

  auto simulate_ip = [](void* clientData, Tcl_Interp* interp, int argc,
                        const char* argv[]) -> int {
    IPGenerator* generator = (IPGenerator*)clientData;
    if (argc < 2) {
      Tcl_AppendResult(interp, "Wrong arguments. Please read simulate_ip help",
                       nullptr);
      return TCL_ERROR;
    }
    const std::string ipName{argv[1]};
    WorkerThread* thread = new WorkerThread{
        {}, Compiler::Action::Analyze, generator->GetCompiler()};
    std::string message{};
    auto fn = [generator, &message](const std::string& ipName) -> bool {
      auto [ok, res] = generator->SimulateIpTcl(ipName);
      if (!ok) message = res;
      return ok;
    };
    bool res = thread->Start(fn, ipName);
    if (!res) Tcl_AppendResult(interp, message.c_str(), nullptr);
    return res ? TCL_OK : TCL_ERROR;
  };
  interp->registerCmd("simulate_ip", simulate_ip, this, nullptr);

  return true;
}

bool IPGenerator::AddIPInstance(IPInstance* instance) {
  bool status = true;
  const IPDefinition* def = instance->Definition();

  // Remove old IP Instance if an instance with the same ModuleName is passed
  auto isMatch = [instance](IPInstance* targetInstance) {
    return (targetInstance->ModuleName() == instance->ModuleName()) &&
           (targetInstance->Version() == instance->Version());
  };
  auto it = std::find_if(m_instances.begin(), m_instances.end(), isMatch);
  if (it != m_instances.end()) {
    m_instances.erase(it);
  }

  // Check parameters
  std::set<std::string> legalParams;

  for (Value* param : def->Parameters()) {
    legalParams.insert(param->Name());
  }
  std::queue<const Connector*> connectors;
  for (const Connector* conn : def->Connections()) {
    connectors.push(conn);
  }
  while (connectors.size()) {
    const Connector* conn = connectors.back();
    connectors.pop();
    switch (conn->GetType()) {
      case Connector::Type::Port: {
        Port* port = (Port*)conn;
        const Range& range = port->GetRange();
        for (const Value* val : {range.LRange(), range.RRange()}) {
          switch (val->GetType()) {
            case Value::Type::ParamInt: {
              Parameter* param = (Parameter*)val;
              legalParams.insert(param->Name());
              break;
            }
            case Value::Type::ParamString: {
              SParameter* param = (SParameter*)val;
              legalParams.insert(param->Name());
              break;
            }
            case Value::Type::ConstInt: {
              break;
            }
            case Value::Type::ParamIpVal: {
              IPParameter* param = (IPParameter*)val;
              legalParams.insert(param->Name());
              break;
            }
          }
        }
        break;
      }
      case Connector::Type::Interface: {
        Interface* intf = (Interface*)conn;
        for (Connector* sub : intf->Connections()) {
          connectors.push(sub);
        }
      }
    }
  }

  for (const SParameter& param : instance->Parameters()) {
    if (legalParams.find(param.Name()) == legalParams.end()) {
      GetCompiler()->ErrorMessage("Unknown parameter: " + param.Name());
      status = false;
    }
  }
  m_instances.push_back(instance);
  return status;
}

IPInstance* IPGenerator::GetIPInstance(const std::string& moduleName) {
  IPInstance* retVal{};
  // Search instances based off moduleName
  auto isMatch = [moduleName](IPInstance* instance) {
    return instance->ModuleName() == moduleName;
  };
  auto it = std::find_if(m_instances.begin(), m_instances.end(), isMatch);

  // return result
  if (it != m_instances.end()) {
    retVal = *it;
  }

  return retVal;
}

FOEDAG::Value* IPGenerator::GetCatalogParam(IPInstance* instance,
                                            const std::string& paramName) {
  // Searches a given instance's definition parameters which contain additional
  // meta data stored during catalog generation
  FOEDAG::Value* retVal{};

  // Search based off parameter name
  auto isMatch = [paramName](FOEDAG::Value* param) {
    return param->Name() == paramName;
  };
  if (instance && instance->Definition()) {
    auto params = instance->Definition()->Parameters();
    auto it = std::find_if(params.begin(), params.end(), isMatch);

    // return result
    if (it != params.end()) {
      retVal = *it;
    }
  }

  return retVal;
}

void IPGenerator::RemoveIPInstance(IPInstance* instance) {
  auto it = std::find(m_instances.begin(), m_instances.end(), instance);
  if (it != m_instances.end()) {
    m_instances.erase(it);
  }

  // search for stored configure/generate commands stored for this module and
  // remove them
  Compiler* compiler = GetCompiler();
  ProjectManager* projManager = nullptr;
  if (compiler && (projManager = compiler->ProjManager())) {
    // match if the command contains "ipgenerate -modules <moduleName>"
    std::string modName = "ipgenerate -modules " + instance->ModuleName();
    auto isMatch = [modName](const std::string& ipGenStr) {
      return (ipGenStr.find(modName) != std::string::npos);
    };

    // Remove any found matches
    auto cmds = projManager->ipInstanceCmdList();
    cmds.erase(std::remove_if(cmds.begin(), cmds.end(), isMatch), cmds.end());
    // Store the updated instance list
    projManager->setIpInstanceCmdList(cmds);
  }
}

void IPGenerator::RemoveIPInstance(const std::string& moduleName) {
  RemoveIPInstance(GetIPInstance(moduleName));
}

void IPGenerator::DeleteIPInstance(IPInstance* instance) {
  // Delete the build folder if it exists
  auto buildPath = GetBuildDir(instance);
  if (FileUtils::FileExists(buildPath) &&
      FileUtils::FileIsDirectory(buildPath)) {
    std::filesystem::remove_all(buildPath);
  }
  // Delete the cached json file
  auto filePath = GetCachePath(instance);
  if (FileUtils::FileExists(filePath)) {
    std::filesystem::remove_all(filePath);
  }

  RemoveIPInstance(instance);
}

void IPGenerator::DeleteIPInstance(const std::string& moduleName) {
  DeleteIPInstance(GetIPInstance(moduleName));
}

// Fingerprint of an RPM IP's shipped artifacts (the netlists/ dir beside its
// generator): relative path, size and mtime of every file, sorted. Embedded
// in the parameter-cache JSON so the byte-identity reuse check below also
// catches a re-authored netlist/stub — otherwise re-packaging an IP (an
// -rpm_ip authoring project's place stage does this automatically) would
// silently keep serving the stale generated copy. Deliberately scoped to netlists/ (empty string for IPs without one):
// fingerprinting every IP's whole version dir would force a spurious
// regenerate of all configured IPs after each tool reinstall, since the
// install re-copies the catalog and refreshes its mtimes.
static std::string NetlistArtifactsFingerprint(
    const std::filesystem::path& netlistsDir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(netlistsDir, ec)) return {};
  std::vector<std::string> entries;
  // error_code throughout: a fingerprinting hiccup must degrade to "changed
  // -> regenerate once", never abort generation.
  for (auto it = std::filesystem::recursive_directory_iterator(netlistsDir, ec);
       !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    // Skip the driver's transient write-if-changed temp files: a crash
    // between write and rename must not force a spurious regenerate.
    if (it->path().extension() == ".tmp") continue;
    const auto mtime = it->last_write_time(ec).time_since_epoch().count();
    const auto size = it->file_size(ec);
    entries.push_back(
        std::filesystem::relative(it->path(), netlistsDir, ec)
            .generic_string() +
        ":" + std::to_string(size) + ":" + std::to_string(mtime));
  }
  std::sort(entries.begin(), entries.end());
  std::string fingerprint;
  for (const auto& e : entries) {
    fingerprint += e + ";";
  }
  // The fingerprint is embedded in a JSON string literal: escape the two
  // characters that could break it (filenames with quotes/backslashes).
  std::string escaped;
  escaped.reserve(fingerprint.size());
  for (const char c : fingerprint) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

bool IPGenerator::Generate() {
  shareContext();

  bool status = true;
  Compiler* compiler = GetCompiler();
  std::vector<IPInstance*> instances{};

  if (compiler->IPGenOpt() == Compiler::IPGenerateOpt::List) {
    // Take a list of moduleNames and only generate those IPs
    std::vector<std::string> modules;
    StringUtils::tokenize(compiler->IPGenMoreOpt(), " ", modules);
    for (const auto& moduleName : modules) {
      IPInstance* inst = GetIPInstance(moduleName);
      if (inst) {
        instances.push_back(inst);
      }
    }
  } else {
    // Generate all IPs
    instances = m_instances;
  }

  for (IPInstance* inst : instances) {
    // Create output directory
    const std::filesystem::path& out_path = inst->OutputLocation();
    if (!std::filesystem::exists(out_path)) {
      std::filesystem::create_directories(out_path);
    }

    const IPDefinition* def = inst->Definition();
    switch (def->Type()) {
      case IPDefinition::IPType::Other: {
        break;
      }
      case IPDefinition::IPType::LiteXGenerator: {
        const std::filesystem::path executable = def->FilePath();
        std::filesystem::path jsonFile = GetCachePath(inst);
        std::stringstream previousbuffer;
        if (FileUtils::FileExists(jsonFile)) {
          std::ifstream previous(jsonFile);
          std::stringstream buffer;
          previousbuffer << previous.rdbuf();
        }

        // Create directory path if it doesn't exist otherwise the following
        // ofstream command will fail
        FileUtils::MkDirs(jsonFile.parent_path());
        std::ofstream jsonF(jsonFile);
        jsonF << "{" << std::endl;
        for (const auto& param : inst->Parameters()) {
          std::string value;
          // The configure_ip command loses type info because we go from full
          // json meta data provided by the ip_catalog generators to a single
          // -Pname=val argument in a tcl command line. As such, we'll use the
          // ip catalog's definition for parameter type info
          auto catalogParam = GetCatalogParam(inst, param.Name());
          if (catalogParam) {
            switch (catalogParam->GetType()) {
              case Value::Type::ParamIpVal: {
                value = param.GetSValue();
                auto type = ((IPParameter*)catalogParam)->GetParamType();
                if (type == IPParameter::ParamType::FilePath ||
                    type == IPParameter::ParamType::String) {
                  value = "\"" + value + "\"";
                }
                break;
              }
              case Value::Type::ParamString:
                value = param.GetSValue();
                value = "\"" + value + "\"";
                break;
              case Value::Type::ParamInt:
                value = param.GetSValue();
                break;
              case Value::Type::ConstInt:
                value = param.GetSValue();
            }
          }
          jsonF << "   \"" << param.Name() << "\": " << value << ","
                << std::endl;
        }
        jsonF << "   \"build_dir\": \"" << FileUtils::resolvePathStr(inst->OutputLocation().string()) << "\","
              << std::endl;
        jsonF << "   \"build_name\": \"" << inst->ModuleName() << "\","
              << std::endl;
        jsonF << "   \"build\": true," << std::endl;
        jsonF << "   \"json\": \"" << jsonFile.filename().string() << "\","
              << std::endl;
        // RPM IPs only (empty otherwise); the generators read the cache with
        // json.load + cfg.get, so the extra key is invisible to them.
        const std::string netlistsFingerprint = NetlistArtifactsFingerprint(
            def->FilePath().parent_path() / "netlists");
        if (!netlistsFingerprint.empty()) {
          jsonF << "   \"netlists_fingerprint\": \"" << netlistsFingerprint
                << "\"," << std::endl;
        }
        jsonF << "   \"json_template\": false" << std::endl;
        jsonF << "}" << std::endl;
        jsonF.close();
        std::stringstream newbuffer;
        if (FileUtils::FileExists(jsonFile)) {
          std::ifstream newfile(jsonFile);
          std::stringstream buffer;
          newbuffer << newfile.rdbuf();
        }
        if (newbuffer.str() == previousbuffer.str()) {
          m_compiler->Message("IP Generate, reusing IP " +
                              GetBuildDir(inst).string());
          continue;
        }

        // Find path to litex enabled python interpreter
        std::filesystem::path pythonPath = IPCatalog::getPythonPath(m_compiler->GetIPGenerator()->EnvsPath());
        if (pythonPath.empty()) {
          std::filesystem::path python3Path =
              FileUtils::LocateExecFile("python3");
          if (python3Path.empty()) {
            m_compiler->ErrorMessage(
                "IP Generate, unable to find python interpreter in local "
                "environment.\n");
            return false;
          } else {
            pythonPath = python3Path;
            m_compiler->ErrorMessage(
                "IP Generate, unable to find python interpreter in local "
                "environment, using system copy '" +
                python3Path.string() +
                "'. Some IP Catalog features might not work with this "
                "interpreter.\n");
          }
        }

        StringVector args{executable.string(), "--build", "--json",
                          FileUtils::GetFullPath(jsonFile).string()};
        std::ostringstream help;
        m_compiler->Message("IP Generate, generating IP " +
                            GetBuildDir(inst).string());
        if (FileUtils::ExecuteSystemCommand(pythonPath.string(), args, &help, environment())
                .code) {
          m_compiler->ErrorMessage("IP Generate, " + help.str());
          return false;
        }

        if (def->Availability().maturity ==
            IPAvailability::Maturity::Preview) {
          // The generators name the wrapper "<module>_<version>.v", with the
          // version taken from the catalog path - the same source GetMetaPath()
          // uses for the build directory.
          const auto meta = FOEDAG::getIpInfoFromPath(def->FilePath());
          StampWrapperComment(m_compiler, GetBuildDir(inst) / "src",
                              inst->ModuleName() + "_" + meta.version,
                              "// WARNING: " + PreviewNotice(def));
        }

        break;
      }
    }
  }
  return status;
}

std::pair<bool, std::string> IPGenerator::IsSimulateIpSupported(
    const std::string& name) const {
  auto it =
      std::find_if(m_instances.begin(), m_instances.end(),
                   [name](IPInstance* i) { return name == i->ModuleName(); });
  if (it == m_instances.end())
    return {false, "No IP generated with name " + name};

  IPInstance* inst{*it};
  auto path = GetSimDir(inst);

  if (!FileUtils::FileExists(path / "Makefile"))
    return {false, "Simulation not available for " + name};
  return {true, {}};
}

std::pair<bool, std::string> IPGenerator::SimulateIpTcl(
    const std::string& name) {
  auto it =
      std::find_if(m_instances.begin(), m_instances.end(),
                   [name](IPInstance* i) { return name == i->ModuleName(); });
  if (it == m_instances.end())
    return {false, "No IP generated with name " + name};

  IPInstance* inst{*it};
  auto path = GetSimDir(inst);

  auto [supported, message] = IsSimulateIpSupported(name);
  if (!supported) return {supported, message};

  auto artifactsPath{GetSimArtifactsDir(inst)};
  if (!FileUtils::MkDirs(artifactsPath)) {
    return {false, "Failed to create folder " + artifactsPath.string()};
  }

  std::string command = "make";
  StringVector args{"OUT_DIR=" + artifactsPath.string(), "MODULE_NAME=" + name};
  if (auto ret = FileUtils::ExecuteSystemCommand(
          command, args, m_compiler->GetOutStream(), {}, -1, path.string(),
          m_compiler->GetErrStream());
      ret.code != 0) {
    return {false, ret.message};
  }
  return {true, std::string{}};
}

void IPGenerator::SimulateIp(const std::string& name) {
  int returnVal{};
  auto resultStr =
      GlobalSession->TclInterp()->evalCmd("simulate_ip " + name, &returnVal);
  if (returnVal != TCL_OK) {
    if (m_compiler) m_compiler->ErrorMessage(resultStr);
  }
}

std::pair<bool, std::string> IPGenerator::OpenWaveForm(
    const std::string& name) {
  auto it =
      std::find_if(m_instances.begin(), m_instances.end(),
                   [name](IPInstance* i) { return name == i->ModuleName(); });
  if (it == m_instances.end())
    return {false, "No IP generated with name " + name};

  IPInstance* inst{*it};
  auto path = GetSimDir(inst);

  auto [supported, message] = IsSimulateIpSupported(name);
  if (!supported) return {supported, message};

  auto artifactsPath{GetSimArtifactsDir(inst)};
  StringVector extensions{".fst", ".vcd"};
  for (const auto& ext : extensions) {
    auto file = FileUtils::FindFileByExtension(artifactsPath, ext);
    if (!file.empty()) {
      const std::string cmd = "wave_open " + file.string();
      bool ok = GlobalSession->CmdStack()->push_and_exec(new Command(cmd));
      return {ok, "Command \'" + cmd + "\' failed."};
    }
  }
  return {false,
          "Waveform file does not exist at " + artifactsPath.string() +
              ".\nSupported files: " + StringUtils::join(extensions, ", ")};
}

// This will return the expected VLNV path for the given instance
std::filesystem::path IPGenerator::GetBuildDir(IPInstance* instance) const {
  auto projectIPsPath = GetProjectIPsPath(instance->ModuleName(), instance->Version());
  if (!projectIPsPath.empty())
    return GetMetaPath(projectIPsPath, instance);
  return {};
}

std::filesystem::path IPGenerator::GetSimDir(IPInstance* instance) const {
  return GetBuildDir(instance) / "sim";
}

std::filesystem::path IPGenerator::GetSimArtifactsDir(
    IPInstance* instance) const {
  std::filesystem::path dir{};
  auto projectIPsPath = GetProjectIPsPath();
  if (!projectIPsPath.empty())
    dir = GetMetaPath(projectIPsPath / "simulation", instance);
  return dir;
}

// This will return the path to this instance's cached json file
std::filesystem::path IPGenerator::GetCachePath(IPInstance* instance) const {
  std::filesystem::path dir{};

  if (m_compiler && m_compiler->ProjManager()) {
    std::filesystem::path ipPath = GetBuildDir(instance);
    auto def = instance->Definition();
    std::string ip_config_file =
        def->Name() + "_" + instance->ModuleName() + ".json";
    dir = ipPath / ip_config_file;
  }

  return dir;
}

std::filesystem::path IPGenerator::GetTmpCachePath(IPInstance* instance) const {
  std::filesystem::path dir{};
  if (m_compiler && m_compiler->ProjManager()) {
    auto ipPath = GetMetaPath(GetTmpPath(), instance);
    auto def = instance->Definition();
    std::string ip_config_file =
        def->Name() + "_" + instance->ModuleName() + ".json";
    dir = ipPath / ip_config_file;
  }
  return dir;
}

std::filesystem::path IPGenerator::GetTmpPath() const {
  auto projectIPsPath = GetProjectIPsPath();
  if (!projectIPsPath.empty()) return projectIPsPath / ".tmp";
  return {};
}

std::filesystem::path IPGenerator::GetProjectIPsPath(const std::string& moduleName, const std::string& version) const {
  if (auto it = m_ipOutputLocations.find(moduleName + "_" + version); it != m_ipOutputLocations.end()) {
    return it->second;
  }
  if (m_compiler && m_compiler->ProjManager()) {
    ProjectManager* projManager{m_compiler->ProjManager()};
    return ProjectManager::projectIPsPath(projManager->projectPath());
  }
  return {};
}

std::filesystem::path IPGenerator::GetProjectIPsPath() const {
  if (m_compiler && m_compiler->ProjManager()) {
    ProjectManager* projManager{m_compiler->ProjManager()};
    return ProjectManager::projectIPsPath(projManager->projectPath());
  }
  return {};
}

std::filesystem::path IPGenerator::GetMetaPath(
    const std::filesystem::path& base, IPInstance* inst) const {
  auto meta = FOEDAG::getIpInfoFromPath(inst->Definition()->FilePath());
  auto ipPath = base / meta.vendor / meta.library / meta.name / meta.version / inst->ModuleName();
  return ipPath;
}

// This will return a vector of paths to ./*.json and ./src/* in a given IP
// instance's build dir
std::vector<std::filesystem::path> IPGenerator::GetDesignFiles(
    IPInstance* instance) {
  std::vector<std::filesystem::path> paths{};

  // Get this IP's build path
  auto buildPath = GetBuildDir(instance);

  // Find any files in the ip src dir
  auto srcPath = buildPath / "src";
  if (FileUtils::FileExists(srcPath)) {
    for (const auto& entry : std::filesystem::directory_iterator(srcPath)) {
      paths.push_back(entry.path());
    }
  }

  return paths;
}

std::vector<std::filesystem::path> IPGenerator::GetDesignAndCacheFiles(
    IPInstance* instance) {
  std::vector<std::filesystem::path> paths{};
  paths += GetDesignFiles(instance);
  paths += GetCacheFiles(instance);
  return paths;
}

std::vector<std::filesystem::path> IPGenerator::GetCacheFiles(
    IPInstance* instance) {
  std::vector<std::filesystem::path> paths{};
  // Get this IP's build path
  auto buildPath = GetBuildDir(instance);
  if (FileUtils::FileExists(buildPath)) {
    // Find the ip cache json file
    for (const auto& entry : std::filesystem::directory_iterator(buildPath)) {
      if (entry.path().extension() == ".json") {
        paths.push_back(entry.path());
      }
    }
  }

  return paths;
}
