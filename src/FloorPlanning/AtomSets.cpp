#include "AtomSets.h"

#include "HierarhyElement.h"

#include <nlohmann_json/json.hpp>

#include <fstream>

namespace fp {

bool loadAtomSets(const std::filesystem::path& path, AtomNameMap& atomNames, int& atomsPerTile)
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

}  // namespace fp
