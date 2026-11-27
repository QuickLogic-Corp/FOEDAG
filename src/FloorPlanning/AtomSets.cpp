#include "AtomSets.h"

#include "HierarhyElement.h"

#include <nlohmann_json/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <regex>
#include <string_view>

namespace fp {

namespace {

std::string netlistSha256Of(const nlohmann::json& doc)
{
    const auto it = doc.find("netlist_sha256");
    return (it != doc.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

}  // namespace

bool loadOwnerTable(const std::filesystem::path& path, OwnerTable& table, std::string& error)
{
    table = OwnerTable{};
    std::ifstream stream(path);
    if (!stream) {
        error = "there is no owner table " + path.string() + " -- run synthesis first";
        return false;
    }
    nlohmann::json doc;
    try {
        stream >> doc;
    } catch (const std::exception& e) {
        error = "the owner table " + path.string() + " is not valid JSON: " + e.what();
        return false;
    }
    const auto version = doc.find("schema_version");
    if (!doc.is_object() || version == doc.end() || *version != 2) {
        error = "the owner table " + path.string() +
                " is not schema version 2 -- re-run synthesis with the matching Yosys plugin";
        return false;
    }
    table.netlistSha256 = netlistSha256Of(doc);
    if (table.netlistSha256.empty()) {
        error = "the owner table " + path.string() + " has no netlist_sha256";
        return false;
    }
    const auto top = doc.find("top");
    table.top = (top != doc.end() && top->is_string()) ? top->get<std::string>() : "";

    auto dotted = [](const nlohmann::json& segments, std::size_t count) {
        std::string path;
        for (std::size_t i = 0; i < count; ++i) {
            if (segments[i].is_string()) {
                path += (path.empty() ? "" : ".") + segments[i].get<std::string>();
            }
        }
        return path;
    };

    std::set<std::string> instances;
    if (const auto list = doc.find("instances"); list != doc.end() && list->is_array()) {
        for (const auto& entry : *list) {
            const auto segments = entry.find("path");
            if (entry.is_object() && segments != entry.end() && segments->is_array()) {
                instances.insert(dotted(*segments, segments->size()));
            }
        }
    }
    if (const auto atoms = doc.find("atoms"); atoms != doc.end() && atoms->is_array()) {
        for (const auto& atom : *atoms) {
            if (!atom.is_object() || !atom.contains("name") || !atom["name"].is_string()) {
                continue;
            }
            const std::string name = atom["name"].get<std::string>();
            const std::string type = atom.value("type", "");
            if (type == "$const") {
                continue;
            }
            table.atomTypes[name] = type;
            std::set<std::string> scopes;
            if (const auto owners = atom.find("owners"); owners != atom.end() && owners->is_array()) {
                if (owners->size() > 1) {
                    std::vector<std::string>& paths = table.mergedAtomOwners[name];
                    for (const auto& owner : *owners) {
                        if (owner.is_array()) {
                            paths.push_back(dotted(owner, owner.size()));
                        }
                    }
                }
                for (const auto& owner : *owners) {
                    if (!owner.is_array()) {
                        continue;
                    }
                    for (std::size_t n = 1; n <= owner.size(); ++n) {
                        scopes.insert(dotted(owner, n));
                    }
                    if (!owner.empty()) {
                        ++table.ownAtomCounts[dotted(owner, owner.size())];
                    }
                }
            }
            if (!table.top.empty()) {
                scopes.insert(table.top);
            }
            for (const std::string& scope : scopes) {
                table.atomNames[scope].push_back(name);
                ++table.atomResources[scope][type];
                instances.insert(scope);
            }
        }
    }
    instances.erase(table.top);
    instances.erase("");
    table.instances.assign(instances.begin(), instances.end());
    return true;
}

std::string readNetlistSha256(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream) {
        return {};
    }
    try {
        nlohmann::json doc;
        stream >> doc;
        return netlistSha256Of(doc);
    } catch (const std::exception&) {
        return {};
    }
}

bool floorplanReloadNeeded(int loadedTier, const std::string& loadedSha256, int diskTier,
                           const std::string& diskSha256)
{
    return diskSha256 != loadedSha256 || diskTier > loadedTier;
}

std::set<std::string> atomNamesFor(const AtomNameMap& atomNames, const std::string& path)
{
    if (isElementPattern(path)) {
        std::set<std::string> names;
        for (const auto& [candidate, atoms] : atomNames) {
            if (elementPatternCovers(path, candidate)) {
                names.insert(atoms.begin(), atoms.end());
            }
        }
        return names;
    }

    if (const auto it = atomNames.find(path); it != atomNames.end()) {
        return {it->second.begin(), it->second.end()};
    }

    // Linear scan rather than a lower_bound range: the map is ordered by NaturalLess, whose
    // collation is not the plain byte order a prefix range would need.
    std::set<std::string> names;
    const std::string prefix = path + ".";
    for (const auto& [candidate, atoms] : atomNames) {
        if (candidate.compare(0, prefix.size(), prefix) == 0) {
            names.insert(atoms.begin(), atoms.end());
        }
    }
    return names;
}

bool isElementPattern(const std::string& element)
{
    return element.find('*') != std::string::npos;
}

namespace {

// generate_floorplanning.py's glob rules, applied to instance paths: "[*]" is any index,
// any other "*" is any text, dots included. The trailing ".*" means "and its subtree",
// which elementPatternCovers() handles by walking up the path instead.
const std::regex& patternRegex(const std::string& pattern)
{
    // The batch checker (compile thread) and the panel (GUI thread) both use it. A map node
    // never moves, so the reference returned stays valid after the lock is released.
    static std::map<std::string, std::regex> cache;
    static std::mutex mutex;
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto it = cache.find(pattern); it != cache.end()) {
        return it->second;
    }
    std::string base = pattern;
    if ((base.size() >= 2) && (base.compare(base.size() - 2, 2, ".*") == 0)) {
        base.resize(base.size() - 2);
    }
    std::string expr = "^";
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (base.compare(i, 3, "[*]") == 0) {
            expr += "\\[\\d+\\]";
            i += 2;
        } else if (base[i] == '*') {
            expr += ".*";
        } else {
            if (std::string_view("\\^$.|?+()[]{}").find(base[i]) != std::string_view::npos) {
                expr += '\\';
            }
            expr += base[i];
        }
    }
    expr += "$";
    return cache.emplace(pattern, std::regex(expr)).first->second;
}

}  // namespace

bool elementPatternMatches(const std::string& pattern, const std::string& path)
{
    return std::regex_match(path, patternRegex(pattern));
}

bool elementPatternCovers(const std::string& pattern, const std::string& path)
{
    const std::regex& re = patternRegex(pattern);
    std::string candidate = path;
    while (true) {
        if (std::regex_match(candidate, re)) {
            return true;
        }
        const std::size_t dot = candidate.rfind('.');
        if (dot == std::string::npos) {
            return false;
        }
        candidate.resize(dot);
    }
}

ResourceTally tallyResources(const std::set<std::string>& atomNames, int atomsPerTile,
                             const AtomTypeMap& atomTypes)
{
    ResourceTally tally;
    tally.atomsPerTile = (atomsPerTile > 0) ? atomsPerTile : 1;
    for (const std::string& atom : atomNames) {
        const auto typeIt = atomTypes.find(atom);
        const std::string realType = (typeIt != atomTypes.end()) ? typeIt->second : "";
        const QString type = classifyAtomType(atom, realType);
        if (type == "dsp") {
            ++tally.dspAtoms;
            if (countsAsDspTile(realType)) {
                ++tally.dsp;
            }
        } else if (type == "bram") {
            ++tally.bram;
        } else {
            ++tally.clbAtoms;
        }
    }
    tally.clbTiles = (tally.clbAtoms + tally.atomsPerTile - 1) / tally.atomsPerTile;
    return tally;
}

std::map<std::string, int> tallyAtomResources(const AtomResourceMap& atomResources,
                                              const std::set<std::string>& paths)
{
    // Every entry the selection covers: named by it, or sitting under something it named.
    std::set<std::string> covered;
    for (const auto& [entry, resources] : atomResources) {
        for (const std::string& path : paths) {
            if (entry == path || entry.compare(0, path.size() + 1, path + ".") == 0) {
                covered.insert(entry);
                break;
            }
        }
    }

    std::map<std::string, int> totals;
    for (const std::string& entry : covered) {
        // Drop it if an ancestor of it is covered too: that ancestor's counts already
        // include this one's, and adding both charges for the same atoms twice.
        bool nested = false;
        for (std::size_t dot = entry.rfind('.'); dot != std::string::npos;
             dot = entry.rfind('.', dot - 1)) {
            if (covered.count(entry.substr(0, dot)) != 0) {
                nested = true;
                break;
            }
            if (dot == 0) {
                break;
            }
        }
        if (nested) {
            continue;
        }
        for (const auto& [type, count] : atomResources.at(entry)) {
            totals[type] += count;
        }
    }
    return totals;
}

}  // namespace fp
