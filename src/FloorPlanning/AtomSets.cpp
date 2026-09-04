#include "AtomSets.h"

#include "HierarhyElement.h"

#include <nlohmann_json/json.hpp>

#include <fstream>

namespace fp {

bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames, int& atomsPerTile)
{
    AtomResourceMap ignored;
    return loadAtomSets(path, atomNames, ignored, atomsPerTile);
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

ResourceTally tallyResources(const std::set<std::string>& atomNames, int atomsPerTile)
{
    ResourceTally tally;
    for (const std::string& atom : atomNames) {
        const QString type = classifyAtomType(atom);
        if (type == "dsp") {
            ++tally.dsp;
        } else if (type == "bram") {
            ++tally.bram;
        } else {
            ++tally.clbAtoms;
        }
    }
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

bool loadAtomTypes(const std::filesystem::path& debugJsonPath, AtomTypeMap& outTypes)
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
    }
    return true;
}

}  // namespace fp
