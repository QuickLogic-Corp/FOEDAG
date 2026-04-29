#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp {

class PostSynthVerilogNameBridge {
public:
  bool loadVprNetlist(const std::filesystem::path&);
  void setVprNetlist(const std::set<std::string>& elements);
  bool loadRtlSources(const std::vector<std::filesystem::path>&);

  // Expand a user RTL name/path to all matching VPR atom names.
  // Matches: exact, "path." prefix (instance), "name[" prefix (bus).
  // Returns empty set if nothing matched.
  std::set<std::string> resolveToVprNames(const std::string& userPath) const;

  // Returns true if userPath is a known instance path from the RTL hierarchy.
  bool isKnownInstPath(const std::string& path) const { return m_instPaths.count(path) > 0; }

  // Returns all RTL instance paths built from loadRtlSources().
  std::set<std::string> instPaths() const {
    std::set<std::string> result;
    for (const auto& [path, _] : m_instPaths)
      result.insert(path);
    return result;
  }

  std::string error() const { return m_error; }

private:
  std::set<std::string>              m_vprNames;   // VPR atom names (constants excluded)
  std::map<std::string, std::string> m_instPaths;  // full inst path → module type

  void buildInstPaths(
      const std::string& moduleName,
      const std::string& pathPrefix,
      const std::map<std::string, std::vector<std::pair<std::string, std::string>>>& modules,
      std::set<std::string>& callStack);

  std::string m_error;
};

}  // namespace fp
