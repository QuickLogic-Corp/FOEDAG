#pragma once

#include <string>
#include <set>

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
