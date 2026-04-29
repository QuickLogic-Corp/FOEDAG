#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp {

// Natural (human) sort comparator: compares runs of digits numerically so that
// e.g. x0[9] < x0[10] instead of the lexicographic x0[10] < x0[9].
struct NaturalLess {
  bool operator()(const std::string& a, const std::string& b) const {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
      const bool aDigit = (a[i] >= '0' && a[i] <= '9');
      const bool bDigit = (b[j] >= '0' && b[j] <= '9');
      if (aDigit && bDigit) {
        // Compare numeric runs
        size_t ai = i, bi = j;
        while (ai < a.size() && a[ai] >= '0' && a[ai] <= '9') ++ai;
        while (bi < b.size() && b[bi] >= '0' && b[bi] <= '9') ++bi;
        const size_t aLen = ai - i, bLen = bi - j;
        if (aLen != bLen) return aLen < bLen; // fewer digits = smaller number
        const int cmp = a.compare(i, aLen, b, j, bLen);
        if (cmp != 0) return cmp < 0;
        i = ai; j = bi;
      } else {
        if (a[i] != b[j]) return a[i] < b[j];
        ++i; ++j;
      }
    }
    return a.size() < b.size();
  }
};

class PostSynthVerilogNameBridge {
public:
  using NaturalStringSet = std::set<std::string, NaturalLess>;

  bool loadVprNetlist(const std::filesystem::path&);
  void setVprNetlist(const std::set<std::string>& elements);
  bool loadRtlSources(const std::vector<std::filesystem::path>&);

  // Expand a user RTL name/path to all matching VPR atom names.
  // Matches: exact, "path." prefix (instance), "name[" prefix (bus).
  // Returns empty set if nothing matched.
  NaturalStringSet resolveToVprNames(const std::string& userPath) const;

  // Returns true if userPath is a known instance path from the RTL hierarchy.
  bool isKnownInstPath(const std::string& path) const { return m_instPaths.count(path) > 0; }

  // Returns all RTL instance paths built from loadRtlSources(), naturally sorted.
  NaturalStringSet instPaths() const {
    NaturalStringSet result;
    for (const auto& [path, _] : m_instPaths)
      result.insert(path);
    return result;
  }

  std::string error() const { return m_error; }

private:
  NaturalStringSet                               m_vprNames;   // VPR atom names (constants excluded)
  std::map<std::string, std::string, NaturalLess> m_instPaths;  // full inst path → module type

  void buildInstPaths(
      const std::string& moduleName,
      const std::string& pathPrefix,
      const std::map<std::string, std::vector<std::pair<std::string, std::string>>>& modules,
      std::set<std::string>& callStack);

  std::string m_error;
};

}  // namespace fp
