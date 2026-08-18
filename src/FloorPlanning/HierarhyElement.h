#pragma once

#include <string>
#include <set>
#include <map>

#include <QDebug>
#include <QString>

namespace fp {

// Classifies an atom by the hard-block primitive embedded in its name (see
// dspv2_sim.v / brams_final_map.v in the device data): every DSP primitive variant
// is named "QL_DSPV2*", and every BRAM macro techmaps down to "TDP_ECC36K". Anything
// else (luts, adder_carry, sdffre, ...) packs into ordinary CLB fabric.
//
// Shared between the Atom List column (SynthResourceHierarchyWidget) and Partition's
// running clb/dsp/bram counts, so the two can never disagree on what an atom is.
inline QString classifyAtomType(const std::string& atomName) {
    if (atomName.find("QL_DSPV2") != std::string::npos) return "dsp";
    if (atomName.find("TDP_ECC36K") != std::string::npos) return "bram";
    return "clb";
}

// [aurora2#1725 stage P7] One instance's row from design_resources.json.
//
// Only ever consulted for instances that have NO atoms of their own -- see
// Partition::addElement(). Once synthesis has run, atomsets.json gives the real atoms and
// those are counted directly; this exists for the window before that, where the atom-based
// path has nothing at all to count and a user is nonetheless drawing regions.
// design_resources.json also carries lut/ff/carry, which are not read here: the panel's own
// atom-based tally already covers those post-synthesis.
struct DesignResourceEntry {
    int clbEst = 0;      // already in TILES, the unit Partition::*RequiredCount() reports
    int dsp = 0;
    int bram = 0;

    // [aurora2#1725 stage P7] Tier 3 only: CLB tiles the placer actually used for this
    // instance. Absent at tiers 1 and 2, where CLBs do not exist yet -- hence the explicit
    // flag rather than a sentinel, so "not measured" cannot be read as "measured zero".
    int clbActual = 0;
    bool hasClbActual = false;
    // The instance shares at least one cluster with an instance outside its own subtree, so
    // clbActual counts tiles it does not solely own and summing such rows over-counts.
    bool clbActualShared = false;
};

// [aurora2#1725 stage P7] design_resources.json as a whole. `tier` is not decoration: all
// three tiers share a shape, so it is the only thing distinguishing an estimate from a
// measurement, and A.13.5 requires it be surfaced rather than silently absorbed.
struct DesignResources {
    int tier = 0;                 // 0 = no file loaded, else 1..3
    std::string tierName;
    std::map<std::string, DesignResourceEntry> instances;

    bool isEstimate() const { return tier == 1; }
    bool valid() const { return tier > 0; }
};

// [aurora2#1725 stage P4] One instance's grade from validation.json, as the FloorPlanning
// trees render it: greyed out when synthesis deleted it, flagged when its atom set is only
// partially trustworthy. Lives here rather than on a widget because both the netlist tree and
// the partition tree take it, and FloorPlanningWidget only forward-declares those.
struct InstanceVerdict {
    std::string verdict;   // "complete" | "partial" | "deleted" | "unknown"
    std::string reason;    // why it was deleted, or which check failed
};

class HierarhyElement {
public:
    HierarhyElement(const std::string& path): path(path) {}
    HierarhyElement(const std::string& path, bool isLeaf): path(path), isLeaf(isLeaf) {}
    HierarhyElement(const std::string& path, bool isLeaf, std::set<std::string> vprNames)
        : path(path), isLeaf(isLeaf), vprNames(std::move(vprNames)) {}

    bool operator==(const HierarhyElement& rhs) const {
        return ((path == rhs.path) && (isLeaf == rhs.isLeaf));
    }

    std::string path;          // user RTL name
    bool isLeaf = true;
    std::set<std::string> vprNames;  // resolved VPR atom names

    bool operator<(const HierarhyElement& rhs) const { return path < rhs.path; }
};

class HierarhyElements {
public:
    bool empty() const { return m_elements.empty(); }
    bool operator==(const HierarhyElements& rhs) const {
        if (size() != rhs.size()) {
            return false;
        }
        return m_elements == rhs.m_elements;
    }

    const std::set<HierarhyElement>& data() const { return m_elements; }

    std::size_t size() const { return m_elements.size(); }

    void clear() { m_elements.clear(); }

    bool contains(const std::string& path) const {
        return m_elements.find(HierarhyElement{path}) != m_elements.end();
    }

    std::set<HierarhyElement>::const_iterator begin() const { return m_elements.begin(); }
    std::set<HierarhyElement>::const_iterator end()   const { return m_elements.end(); }

    void insert(const std::set<HierarhyElement>& elements) {
        m_elements.insert(elements.begin(), elements.end());
    }
    void insert(const HierarhyElement& element) {
        m_elements.insert(element);
    }

private:
    std::set<HierarhyElement> m_elements;
};

}  // namespace fp
