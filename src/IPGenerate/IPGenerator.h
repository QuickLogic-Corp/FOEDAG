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

#ifndef IPGENERATOR_H
#define IPGENERATOR_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cassert>

#include "nlohmann_json/json.hpp"

#include "IPGenerate/IPCatalog.h"

namespace FOEDAG {

class TclInterpreter;
class Compiler;
class IPInstance;

class IPGenerator {
 public:
  // Availability of one catalog IP against a device.
  //
  // Every field is filled in for every IP. Because nothing may be hidden
  // without a stated reason, `reason` is never empty - not even for a plainly
  // available IP.
  struct IPStatus {
    bool available{true};   // may be configured
    bool listed{true};      // appears in the default `ip_catalog` listing
    bool preview{false};    // configure_ip warns about it, then proceeds
    bool unverified{false}; // fabric requirement could not be read; see below
    std::string state;      // "production" | "preview" | "unavailable"
    std::string reason;     // never empty; reads as a predicate on the IP name
  };

  // The device facts the availability rule needs. Hoisted into a value so a
  // listing evaluates one IP after another without re-reading and re-parsing
  // the device config.json once per IP.
  struct DeviceFacts {
    bool valid{false};       // a device is actually selected
    std::string name;        // device_variant.devicename
    std::string dspVersion;  // QLDeviceManager::deviceDSPVersion()
  };
  // Reads the device currently selected in QLDeviceManager. When no device is
  // selected the version field is left empty rather than defaulted, so the
  // "1_0" fallback for devices without a DSP_TYPE can never stand in for a
  // device that is not there.
  static DeviceFacts CurrentDeviceFacts();

  // Single source of truth for whether a catalog IP can be used, shared by the
  // ip_catalog listing, the ip_catalog <name> query and configure_ip so the
  // three surfaces cannot drift.
  //
  // Gating is entirely data driven: it comes from the IP's optional
  // ip_manifest.json, never from the IP's name. See the fail-open contract on
  // IPAvailability (IPCatalog.h) for the rules this implements - in
  // particular, an IP whose requirement could not be read comes back
  // available and listed but with `unverified` set, not silently ungated.
  // `catalog` is consulted only to check that a suggested alternative exists.
  static IPStatus EvaluateAvailability(const IPDefinition* def,
                                       IPCatalog* catalog,
                                       const DeviceFacts& device);
  // As above, against the device currently selected. Convenience for the
  // single-IP callers; a loop should hoist CurrentDeviceFacts() instead.
  static IPStatus EvaluateAvailability(const IPDefinition* def,
                                       IPCatalog* catalog);

  // Warning text for a preview IP, shared by the console, the flow log and the
  // comment stamped into the generated wrapper so the three cannot drift.
  static std::string PreviewNotice(const IPDefinition* def);
  // Warning text for an IP whose fabric requirement could not be read.
  static std::string UnverifiedRequirementNotice(const IPDefinition* def);

  // Prepends `line` as a comment to a generated wrapper source, looked up as
  // <srcDir>/<baseName>.v and .sv. Returns false when nothing matched; the
  // generators name the file "<module>_<version>", and getting that wrong made
  // this an undetectable no-op, so the miss is now both returned and warned
  // about. Public so a test can exercise it against a real file.
  static bool StampWrapperComment(Compiler* compiler,
                                  const std::filesystem::path& srcDir,
                                  const std::string& baseName,
                                  const std::string& line);

  IPGenerator(const std::filesystem::path& installDir, IPCatalog* catalog, Compiler* compiler);
  virtual ~IPGenerator() {}

  void setIpOutputLocation(const std::string& moduleName, const std::string& version, const std::filesystem::path& ipOutputLocation);
  void shareContext();
  const std::map<std::string, std::string>& environment() const { return m_environment; }
  
  std::filesystem::path EnvsPath() const;
  std::filesystem::path IPCatalogPath() const;
  // The installed catalog root the tool ships (dev/IP_Catalog in the aurora
  // layout) — the one definition of that path, shared by the Tcl lazy load
  // and the GUI catalog tree.
  std::filesystem::path DefaultIPCatalogPath() const;
  // The project-local user catalog (<project>/IP_Catalog), searched in
  // addition to the installed one. Empty when no project is open. Authored
  // RPM IPs are packaged here by default (package_rpm_ip).
  std::filesystem::path ProjectUserCatalogPath() const;
  // Lazily load the catalog roots the tool knows about (installed first,
  // then project-local; a name found in both roots is a load error), each at
  // most once per session. Called by ip_catalog/configure_ip; explicit
  // add_litex_ip_catalog calls always rescan. Replaces the old "load the
  // default iff the catalog is empty" guard, under which an early explicit
  // add_litex_ip_catalog silently suppressed the shipped catalog.
  void LoadDefaultCatalogs();

  IPCatalog* Catalog() { return m_catalog; }
  Compiler* GetCompiler() { return m_compiler; }
  bool RegisterCommands(TclInterpreter* interp, bool batchMode);
  std::vector<IPInstance*> IPInstances() { return m_instances; }
  bool AddIPInstance(IPInstance* instance);
  IPInstance* GetIPInstance(const std::string& moduleName);
  FOEDAG::Value* GetCatalogParam(IPInstance* instance,
                                 const std::string& paramName);
  void RemoveIPInstance(IPInstance* instance);
  void RemoveIPInstance(const std::string& moduleName);
  void DeleteIPInstance(IPInstance* instance);
  void DeleteIPInstance(const std::string& moduleName);
  void ResetIPList() {
    m_instances.erase(m_instances.begin(), m_instances.end());
  }
  bool Generate();
  std::pair<bool, std::string> IsSimulateIpSupported(
      const std::string& name) const;
  void SimulateIp(const std::string& name);
  std::pair<bool, std::string> OpenWaveForm(const std::string& name);
  std::filesystem::path GetBuildDir(IPInstance* instance) const;
  std::filesystem::path GetSimDir(IPInstance* instance) const;
  std::filesystem::path GetSimArtifactsDir(IPInstance* instance) const;
  std::filesystem::path GetCachePath(IPInstance* instance) const;
  std::filesystem::path GetTmpCachePath(IPInstance* instance) const;
  std::filesystem::path GetTmpPath() const;
  std::filesystem::path GetProjectIPsPath(const std::string& moduleName, const std::string& version) const;
  std::filesystem::path GetProjectIPsPath() const;
  std::filesystem::path GetMetaPath(const std::filesystem::path& base,
                                    IPInstance* inst) const;
  std::vector<std::filesystem::path> GetDesignFiles(IPInstance* instance);
  std::vector<std::filesystem::path> GetDesignAndCacheFiles(
      IPInstance* instance);
  std::vector<std::filesystem::path> GetCacheFiles(IPInstance* instance);

 protected:
  std::pair<bool, std::string> SimulateIpTcl(const std::string& name);

 protected:
  IPCatalog* m_catalog = nullptr;
  Compiler* m_compiler = nullptr;
  std::vector<IPInstance*> m_instances;
  std::map<std::string, std::string> m_environment;

private:
  std::filesystem::path m_installDir;
  std::map<std::string, std::filesystem::path> m_ipOutputLocations;

  void dumpDeviceInfo(const std::filesystem::path&);
  void dumpParameterModifications(const std::filesystem::path&);
  void saveJsonFile(const nlohmann::json& data, const std::filesystem::path& filepath);
};

}  // namespace FOEDAG

#endif
