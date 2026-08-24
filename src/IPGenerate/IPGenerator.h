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
  // Every field is filled in for every IP. The governing rule of the IP
  // availability model is that nothing is hidden without a stated reason and
  // nothing that would build is rejected, so `reason` is never empty - not
  // even for a plainly available IP.
  struct IPStatus {
    bool available{true};  // may be configured
    bool listed{true};     // appears in the default `ip_catalog` listing
    bool preview{false};   // configure_ip warns about it, then proceeds
    std::string state;     // "production" | "preview" | "unavailable"
    std::string reason;    // never empty; reads as a predicate on the IP name
  };

  // Single source of truth for whether a catalog IP can be used, shared by the
  // ip_catalog listing, the ip_catalog <name> query and configure_ip so the
  // three surfaces cannot drift.
  //
  // Gating is entirely data driven: it comes from the IP's optional
  // ip_manifest.json (see IPAvailability), never from the IP's name. An IP
  // with no manifest, or with a manifest this build cannot make sense of, is
  // available - a manifest can only make an IP infeasible on a given device,
  // and only with a reason that names the requirement, the device and the way
  // out. `catalog` is consulted only to check that a suggested alternative
  // really exists.
  //
  // The device facts are arguments rather than lookups so the rule can be
  // exercised on its own. `haveDevice` false means no device is selected: the
  // IP is then listed and annotated instead of being judged against a
  // default-constructed target.
  static IPStatus EvaluateAvailability(const IPDefinition* def,
                                       IPCatalog* catalog, bool haveDevice,
                                       const std::string& deviceName,
                                       const std::string& deviceDspVersion);
  // As above, against the device currently selected in QLDeviceManager.
  static IPStatus EvaluateAvailability(const IPDefinition* def,
                                       IPCatalog* catalog);

  // Warning text for a preview IP, shared by the console, the flow log and the
  // comment stamped into the generated wrapper so the three cannot drift.
  static std::string PreviewNotice(const IPDefinition* def);

  IPGenerator(const std::filesystem::path& installDir, IPCatalog* catalog, Compiler* compiler);
  virtual ~IPGenerator() {}

  void setIpOutputLocation(const std::string& moduleName, const std::string& version, const std::filesystem::path& ipOutputLocation);
  void shareContext();
  const std::map<std::string, std::string>& environment() const { return m_environment; }
  
  std::filesystem::path EnvsPath() const;
  std::filesystem::path IPCatalogPath() const;

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
