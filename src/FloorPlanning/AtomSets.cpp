#include "AtomSets.h"

#include "HierarhyElement.h"

#include <nlohmann_json/json.hpp>

#include <algorithm>
#include <fstream>
#include <regex>
#include <string_view>

namespace fp {

bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames, int& atomsPerTile)
{
    AtomResourceMap ignored;
    return loadAtomSets(path, atomNames, ignored, atomsPerTile);
}

bool loadAtomConflicts(const std::filesystem::path& path, std::vector<AtomConflict>& conflicts)
{
    std::ifstream stream(path);
    if (!stream) {
        return false;
    }
    nlohmann::json doc;
    try {
        stream >> doc;
    } catch (const std::exception&) {
        return false;
    }
    if (!doc.contains("conflicts") || !doc["conflicts"].is_array()) {
        return true;
    }
    for (const auto& entry : doc["conflicts"]) {
        if (!entry.is_object()) continue;
        conflicts.push_back({entry.value("atom", std::string{}),
                             entry.value("assigned_to", std::string{}),
                             entry.value("scopename_claims", std::string{})});
    }
    return true;
}

bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames,
                  AtomResourceMap& atomResources, int& atomsPerTile)
{
    std::ifstream stream(path);
    if (!stream) {
        return false;
    }
    nlohmann::json doc;
    try {
        stream >> doc;
    } catch (const std::exception&) {
        return false;
    }

    // Packing density (A.13.3). Absent or garbage leaves the caller's default alone.
    if (doc.contains("atoms_per_tile") && doc["atoms_per_tile"].is_number_integer()) {
        const int value = doc["atoms_per_tile"].get<int>();
        if (value > 0) {
            atomsPerTile = value;
        }
    }

    if (!doc.contains("atomsets") || !doc["atomsets"].is_object()) {
        return false;
    }
    for (auto it = doc["atomsets"].begin(); it != doc["atomsets"].end(); ++it) {
        std::vector<std::string> atoms;
        if (it.value().contains("atoms") && it.value()["atoms"].is_array()) {
            for (const auto& atom : it.value()["atoms"]) {
                if (atom.is_string()) {
                    atoms.push_back(atom.get<std::string>());
                }
            }
        }
        atomNames[it.key()] = std::move(atoms);

        // "resources": the cell types behind those atoms. Older files may not have it, and a
        // missing map is not a failure -- it costs the Atoms tab its numbers, nothing else.
        if (it.value().contains("resources") && it.value()["resources"].is_object()) {
            std::map<std::string, int> resources;
            for (auto res = it.value()["resources"].begin();
                 res != it.value()["resources"].end(); ++res) {
                if (res.value().is_number_integer()) {
                    resources[res.key()] = res.value().get<int>();
                }
            }
            atomResources[it.key()] = std::move(resources);
        }
    }
    return true;
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
    static std::map<std::string, std::regex> cache;
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

std::map<std::string, int> ownAtomCounts(const AtomNameMap& atomNames)
{
    // An entry's atom list is subtree-inclusive, so an atom is the instance's own exactly
    // when what follows "<path>." holds no further dot -- "dut.i.n0_$lut_Y" as opposed to
    // "dut.i.sub.n0_$lut_Y".
    std::map<std::string, int> counts;
    for (const auto& [path, atoms] : atomNames) {
        const std::size_t tail = path.size() + 1;
        int own = 0;
        for (const std::string& atom : atoms) {
            if ((atom.size() > tail) && (atom.find('.', tail) == std::string::npos)) {
                ++own;
            }
        }
        counts[path] = own;
    }
    return counts;
}

ResourceTally tallyResources(const std::set<std::string>& atomNames, int atomsPerTile,
                             const AtomTypeMap& atomTypes, const DspTileMap& dspTiles)
{
    ResourceTally tally;
    std::set<std::string> dspTileIds;
    for (const std::string& atom : atomNames) {
        const auto typeIt = atomTypes.find(atom);
        const std::string realType = (typeIt != atomTypes.end()) ? typeIt->second : "";
        const QString type = classifyAtomType(atom, realType);
        if (type == "dsp") {
            ++tally.dspAtoms;
            if (const auto tile = dspTiles.find(atom); tile != dspTiles.end()) {
                dspTileIds.insert(tile->second);
            } else if (countsAsDspTile(realType)) {
                ++tally.dsp;
            }
        } else if (type == "bram") {
            ++tally.bram;
        } else {
            ++tally.clbAtoms;
        }
    }
    tally.dsp += static_cast<int>(dspTileIds.size());
    const int perTile = (atomsPerTile > 0) ? atomsPerTile : 1;
    tally.clbTiles = (tally.clbAtoms + perTile - 1) / perTile;
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

namespace {

// Whether a net from a `driverType` leaf into a `sinkType` leaf stays inside one DSP-V4
// tile (dsp4_logical_map.v). A tile's results leave through ACC.Q, RSS.ACC_OUT, CO.Q, or
// an unregistered ALU output, and may feed another tile; inside a tile, an ALU output
// goes only to the ACC, RSS or CO register.
bool staysInDspTile(const std::string& driverType, const std::string& sinkType)
{
    if ((driverType == "QL_DSP4_ACC_DFFRE_64") || (driverType == "QL_DSP4_RSS") ||
        (driverType == "QL_DSP4_CO_DFFR_4")) {
        return false;
    }
    if (driverType.rfind("QL_DSP4_ALU_", 0) == 0) {
        return (sinkType == "QL_DSP4_ACC_DFFRE_64") || (sinkType == "QL_DSP4_RSS") ||
               (sinkType == "QL_DSP4_CO_DFFR_4");
    }
    return true;
}

void groupDspTiles(const nlohmann::json& cells, DspTileMap& outDspTiles)
{
    std::map<std::string, std::string> types;
    for (auto cell = cells.begin(); cell != cells.end(); ++cell) {
        const std::string type = cell.value().value("type", "");
        if (type.rfind("QL_DSP4_", 0) == 0) {
            types[cell.key()] = type;
        }
    }

    // Union-find, rooted at the smallest name so a tile's id does not depend on order.
    std::map<std::string, std::string> parent;
    for (const auto& [name, type] : types) {
        parent[name] = name;
    }
    auto find = [&parent](std::string name) {
        while (parent[name] != name) {
            name = parent[name] = parent[parent[name]];
        }
        return name;
    };

    auto bitsOf = [&cells](const std::string& name, const std::string& direction) {
        std::vector<int> bits;
        const auto& cell = cells[name];
        if (!cell.contains("port_directions") || !cell.contains("connections")) {
            return bits;
        }
        for (auto port = cell["port_directions"].begin(); port != cell["port_directions"].end();
             ++port) {
            if ((port.value() != direction) || !cell["connections"].contains(port.key())) {
                continue;
            }
            for (const auto& bit : cell["connections"][port.key()]) {
                if (bit.is_number_integer()) {
                    bits.push_back(bit.get<int>());
                }
            }
        }
        return bits;
    };

    std::map<int, std::string> drivers;
    for (const auto& [name, type] : types) {
        for (const int bit : bitsOf(name, "output")) {
            drivers[bit] = name;
        }
    }
    for (const auto& [sink, sinkType] : types) {
        for (const int bit : bitsOf(sink, "input")) {
            const auto driver = drivers.find(bit);
            if ((driver == drivers.end()) || !staysInDspTile(types[driver->second], sinkType)) {
                continue;
            }
            const std::string a = find(driver->second);
            const std::string b = find(sink);
            if (a != b) {
                parent[std::max(a, b)] = std::min(a, b);
            }
        }
    }
    for (const auto& [name, type] : types) {
        outDspTiles[name] = find(name);
    }
}

}  // namespace

bool loadAtomTypes(const std::filesystem::path& debugJsonPath, AtomTypeMap& outTypes)
{
    DspTileMap ignored;
    return loadAtomTypes(debugJsonPath, outTypes, ignored);
}

bool loadAtomTypes(const std::filesystem::path& debugJsonPath, AtomTypeMap& outTypes,
                   DspTileMap& outDspTiles)
{
    std::ifstream stream(debugJsonPath);
    if (!stream) {
        return false;
    }
    nlohmann::json doc;
    try {
        stream >> doc;
    } catch (const std::exception&) {
        return false;
    }

    if (!doc.contains("modules") || !doc["modules"].is_object()) {
        return false;
    }
    // write_json emits one top-level module per invocation here (floorplanning_post_synth.tcl
    // passes -selected, restricting it to the flattened design), but iterate rather than
    // assume a single key: a differently-configured write_json is still just more cells.
    for (const auto& module : doc["modules"]) {
        if (!module.contains("cells") || !module["cells"].is_object()) {
            continue;
        }
        for (auto cell = module["cells"].begin(); cell != module["cells"].end(); ++cell) {
            if (cell.value().contains("type") && cell.value()["type"].is_string()) {
                outTypes[cell.key()] = cell.value()["type"].get<std::string>();
            }
        }
        groupDspTiles(module["cells"], outDspTiles);
    }
    return true;
}

}  // namespace fp
